#include "sim/modifiers.h"

#include <algorithm>

namespace sim {

using pdx::Fixed;
using pdx::ScopeRef;
using pdx::Sym;

ModifierEngine::ModifierEngine(const loader::ModData& d, world::World& w, pdx::Interp& in) : d_(d), w_(w), in_(in) {}

void ModifierEngine::install_hooks() {
    in_.hooks.modifier_value = [this](ScopeRef s, Sym m) { return value(s, m); };
    in_.hooks.modifier_defined = [this](Sym m) { return defined(m); };
    in_.hooks.modifiers_dirty = [this](ScopeRef s) { mark_dirty(s); };
    in_.hooks.refresh_dynamic_modifiers = [this](ScopeRef s) { refresh_dynamic(s); };
}

template <typename F>
void ModifierEngine::for_static_sources(ScopeRef s, F&& fn) {
    static const Sym s_peace = pdx::intern("country_is_at_peace"), s_war = pdx::intern("country_is_at_war");
    if (s.is_country()) {
        const world::Country* co = w_.country(s.id);
        if (!co) return;
        for (Sym idea : co->ideas) {
            auto it = d_.idea_modifiers.find(idea);
            if (it != d_.idea_modifiers.end()) fn("idea " + pdx::sym_str(idea), it->second);
        }
        for (Sym tech : co->tech_order) {
            auto it = d_.techs.find(tech);
            if (it != d_.techs.end() && !it->second.modifiers.empty()) fn("tech " + pdx::sym_str(tech), it->second.modifiers);
        }
        const Sym stat = co->at_war_with.empty() ? s_peace : s_war;
        auto it = d_.static_modifiers.find(stat);
        if (it != d_.static_modifiers.end()) fn("static " + pdx::sym_str(stat), it->second);
    } else if (s.is_state()) {
        const world::State* st = w_.state(s.id);
        if (!st) return;
        auto it = d_.state_categories.find(st->category);
        if (it != d_.state_categories.end()) fn("state_category " + pdx::sym_str(st->category), it->second);
    }
}

void ModifierEngine::rebuild(ScopeRef s, ScopeCache& c) {
    c.sums.clear();
    const pdx::Semantics& sem = in_.semantics();
    pdx::OpStatus os;
    for_static_sources(s, [&](const std::string&, const std::vector<loader::ModEntry>& mods) {
        for (const auto& m : mods) c.sums[m.name] = Fixed::add(c.sums[m.name], m.value, sem, os);
    });
    if (!c.dyn_valid) eval_dynamic(s, c);
    for (const auto& [name, v] : c.dyn) c.sums[name] = Fixed::add(c.sums[name], v, sem, os);
    c.dirty = false;
}

bool ModifierEngine::test_trigger(const loader::DynamicModifierDef& def, const pdx::Node* node, ScopeRef s,
                                  std::unordered_map<Sym, std::unique_ptr<pdx::TriggerBlock>>& cache) {
    if (!node || !node->is_block) return true;
    auto& block = cache[def.name];
    if (!block) block = std::make_unique<pdx::TriggerBlock>(in_.compile_triggers(*def.file, *node));
    return in_.test(*block, s);
}

void ModifierEngine::eval_dynamic(ScopeRef s, ScopeCache& c) {
    c.dyn.clear();
    c.dyn_valid = true;
    const auto* list = w_.dynamic_modifiers(s);
    if (!list) return;
    const pdx::Semantics& sem = in_.semantics();
    pdx::OpStatus os;
    for (const auto& inst : *list) {
        auto it = d_.dynamic_modifiers.find(inst.name);
        if (it == d_.dynamic_modifiers.end()) {
            in_.diagnostics().report(DiagKind::UnknownEffect, "<dynamic_modifiers>", 0,
                                     "dynamic modifier '" + pdx::sym_str(inst.name) + "' is not defined");
            continue;
        }
        const loader::DynamicModifierDef& def = it->second;
        if (!test_trigger(def, def.enable, s, enable_cache_)) continue;
        const ScopeRef var_scope = inst.var_scope.valid() ? inst.var_scope : s;
        world::VarStore temps;
        pdx::Ctx ctx(in_, w_);
        ctx.scopes.push_back(var_scope);
        ctx.root = var_scope;
        ctx.temps = &temps;
        for (const auto& e : def.entries) {
            Fixed v = e.value;
            if (e.is_var) v = in_.read(ctx, e.var, pdx::SrcLoc{def.file->file_id, 0}).num;
            c.dyn[e.name] = Fixed::add(c.dyn[e.name], v, sem, os);
        }
    }
}

Fixed ModifierEngine::value(ScopeRef s, Sym m) {
    ScopeCache& c = cache(s);
    if (c.dirty) rebuild(s, c);
    auto it = c.sums.find(m);
    return it == c.sums.end() ? Fixed{} : it->second;
}

void ModifierEngine::mark_dirty(ScopeRef s) { cache(s).dirty = true; }

void ModifierEngine::mark_all_dirty() {
    for (auto& [k, c] : caches_) c.dirty = true;
}

void ModifierEngine::refresh_dynamic(ScopeRef s) {
    ScopeCache& c = cache(s);
    eval_dynamic(s, c);
    c.dirty = true;
}

void ModifierEngine::daily_refresh() {
    const int64_t today = w_.date.ordinal();
    auto process = [&](ScopeRef s, std::vector<world::DynamicModifierInst>& list) {
        if (list.empty()) return;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const world::DynamicModifierInst& inst) {
                                      if (inst.expires_ordinal >= 0 && inst.expires_ordinal <= today) return true;
                                      auto it = d_.dynamic_modifiers.find(inst.name);
                                      return it != d_.dynamic_modifiers.end() && it->second.remove_trigger &&
                                             test_trigger(it->second, it->second.remove_trigger, s, remove_cache_);
                                  }),
                   list.end());
        refresh_dynamic(s);
    };
    for (auto& co : w_.countries) {
        if (co.exists) process(ScopeRef::country(co.index), co.dynamic_modifiers);
    }
    for (auto& st : w_.states) {
        if (st.exists) process(ScopeRef::state(st.id), st.dynamic_modifiers);
    }
}

std::vector<ModifierEngine::Source> ModifierEngine::breakdown(ScopeRef s, Sym m) {
    std::vector<Source> out;
    for_static_sources(s, [&](const std::string& name, const std::vector<loader::ModEntry>& mods) {
        for (const auto& e : mods) {
            if (e.name == m) out.push_back({name, e.value});
        }
    });
    ScopeCache& c = cache(s);
    if (!c.dyn_valid) eval_dynamic(s, c);
    auto it = c.dyn.find(m);
    if (it != c.dyn.end()) out.push_back({"dynamic modifiers", it->second});
    return out;
}

} // namespace sim
