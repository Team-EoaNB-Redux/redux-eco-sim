// World effects: flags, ideas, dynamic modifiers, techs, country stats, events, native resources,
// buildings, guarantees. Modifier recomputation is delegated through Interp::hooks.

#include "builtins/common.h"

#include <algorithm>

namespace pdx {

namespace {

using sim::DiagKind;

world::Country* self_country(Ctx& c) {
    const ScopeRef s = c.self();
    return s.is_country() ? c.w.country(s.id) : nullptr;
}

void mark_dirty(Ctx& c, ScopeRef s) {
    if (c.in.hooks.modifiers_dirty) c.in.hooks.modifiers_dirty(s);
}

// A name argument that may be `var:x` (a token variable) or a literal name.
struct NameArg {
    Sym name = kNoSym;
    bool from_var = false;
    VarRef var;
};

bool compile_name_arg(Interp& in, SrcLoc l, std::string_view v, NameArg& out) {
    if (v.size() > 4 && v.substr(0, 4) == "var:") {
        out.from_var = true;
        return in.compile_ref(v.substr(4), l, out.var);
    }
    out.name = intern(v);
    return true;
}

Sym eval_name_arg(Ctx& c, const NameArg& a, SrcLoc l) {
    if (!a.from_var) return a.name;
    const Value v = c.in.read(c, a.var, l);
    if (v.kind != Kind::Token) {
        c.in.diag(DiagKind::TokenExpected, l, "'" + a.var.text + "' does not hold a token");
        return kNoSym;
    }
    return v.token_sym();
}

// ---- flags ----

enum class FlagScope : uint8_t { Country, State, Global };

struct SetFlag : EffectOp {
    FlagScope fs = FlagScope::Country;
    bool clear = false;
    Sym flag = kNoSym;
    int64_t value = 1;
    void run(Ctx& c) const override {
        auto* flags = c.w.flags(fs == FlagScope::Global ? ScopeRef::global() : c.self());
        if (!flags) return;
        if (clear) {
            flags->erase(flag);
            return;
        }
        world::Flag fl;
        fl.value = value;
        fl.set_date = c.w.date;
        (*flags)[flag] = fl;
    }
};

template <FlagScope FS, bool Clear>
std::unique_ptr<EffectOp> make_set_flag(Interp&, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<SetFlag>();
    e->fs = FS;
    e->clear = Clear;
    if (!n.is_block) {
        e->flag = n.value;
        return e;
    }
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (bi::key_is(*c, "flag")) e->flag = c->value;
        else if (bi::key_is(*c, "value")) {
            Fixed v;
            Fixed::parse(sym_view(c->value), v);
            e->value = v.whole_trunc();
        }
    }
    return e;
}

// ---- ideas ----

struct IdeasEffect : EffectOp {
    std::vector<NameArg> add, remove;
    void run(Ctx& c) const override {
        world::Country* co = self_country(c);
        if (!co) {
            c.in.diag(DiagKind::InvalidScope, loc, "idea effect outside country scope");
            return;
        }
        for (const auto& a : remove) {
            const Sym s = eval_name_arg(c, a, loc);
            co->ideas.erase(std::remove(co->ideas.begin(), co->ideas.end(), s), co->ideas.end());
        }
        for (const auto& a : add) {
            const Sym s = eval_name_arg(c, a, loc);
            if (s != kNoSym && std::find(co->ideas.begin(), co->ideas.end(), s) == co->ideas.end()) {
                co->ideas.push_back(s);
            }
        }
        mark_dirty(c, c.self());
    }
};

void collect_names(Interp& in, const ScriptFile& f, const Node& n, std::vector<NameArg>& out) {
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) {
        NameArg a;
        if (compile_name_arg(in, l, sym_view(n.value), a)) out.push_back(std::move(a));
        return;
    }
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        NameArg a;
        if (compile_name_arg(in, l, sym_view(c->key == kNoSym ? c->value : c->key), a)) out.push_back(std::move(a));
    }
}

std::unique_ptr<EffectOp> make_add_ideas(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<IdeasEffect>();
    collect_names(in, f, n, e->add);
    return e;
}
std::unique_ptr<EffectOp> make_remove_ideas(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<IdeasEffect>();
    collect_names(in, f, n, e->remove);
    return e;
}
std::unique_ptr<EffectOp> make_swap_ideas(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<IdeasEffect>();
    const SrcLoc l = in.loc(f, n);
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        NameArg a;
        if (!compile_name_arg(in, l, sym_view(c->value), a)) continue;
        if (bi::key_is(*c, "remove_idea")) e->remove.push_back(std::move(a));
        else if (bi::key_is(*c, "add_idea")) e->add.push_back(std::move(a));
    }
    return e;
}

// ---- dynamic modifiers ----

struct DynModEffect : EffectOp {
    enum class Mode : uint8_t { Add, Remove, ForceUpdate } mode = Mode::Add;
    NameArg name;
    bool has_scope = false;
    Operand scope;
    bool has_days = false;
    Operand days;
    void run(Ctx& c) const override {
        const ScopeRef self = c.self();
        if (mode == Mode::ForceUpdate) {
            if (c.in.hooks.refresh_dynamic_modifiers) c.in.hooks.refresh_dynamic_modifiers(self);
            return;
        }
        auto* list = c.w.dynamic_modifiers(self);
        if (!list) {
            c.in.diag(DiagKind::InvalidScope, loc, "dynamic modifier effect outside country/state scope");
            return;
        }
        const Sym n = eval_name_arg(c, name, loc);
        const ScopeRef var_scope = has_scope ? c.in.eval(c, scope, loc).as_scope() : ScopeRef::none();
        auto same = [&](const world::DynamicModifierInst& d) { return d.name == n && (!has_scope || d.var_scope == var_scope); };
        if (mode == Mode::Remove) {
            list->erase(std::remove_if(list->begin(), list->end(), same), list->end());
        } else {
            auto it = std::find_if(list->begin(), list->end(), same);
            world::DynamicModifierInst inst;
            inst.name = n;
            inst.var_scope = var_scope;
            if (has_days) inst.expires_ordinal = c.w.date.ordinal() + c.in.eval(c, days, loc).num.whole_trunc();
            if (it == list->end()) list->push_back(inst);
            else *it = inst;
        }
        mark_dirty(c, self);
        if (c.in.hooks.refresh_dynamic_modifiers && mode == Mode::Add) c.in.hooks.refresh_dynamic_modifiers(self);
    }
};

template <DynModEffect::Mode M>
std::unique_ptr<EffectOp> make_dynmod(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<DynModEffect>();
    e->mode = M;
    const SrcLoc l = in.loc(f, n);
    if (M == DynModEffect::Mode::ForceUpdate) return e;
    if (!n.is_block) {
        compile_name_arg(in, l, sym_view(n.value), e->name);
        return e;
    }
    const Node* mod = bi::find_child(f, n, "modifier");
    if (!mod) {
        in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " needs modifier =");
        return nullptr;
    }
    compile_name_arg(in, l, sym_view(mod->value), e->name);
    if (const Node* s = bi::find_child(f, n, "scope")) e->has_scope = in.compile_operand(f, *s, e->scope);
    if (const Node* d = bi::find_child(f, n, "days")) e->has_days = in.compile_operand(f, *d, e->days);
    return e;
}

// ---- country stats ----

struct StatEffect : EffectOp {
    enum class Stat : uint8_t { Stability, WarSupport, PoliticalPower, ResearchSlots, SetResearchSlots } stat = Stat::Stability;
    Operand amount;
    void run(Ctx& c) const override {
        world::Country* co = self_country(c);
        if (!co) return;
        const Fixed a = c.in.eval(c, amount, loc).num;
        const Fixed one = Fixed::from_int(1);
        auto clamp01 = [&](Fixed v) { return v < Fixed{} ? Fixed{} : (v > one ? one : v); };
        switch (stat) {
        case Stat::Stability: co->stability = clamp01(c.in.arith(c, '+', co->stability, a, loc)); break;
        case Stat::WarSupport: co->war_support = clamp01(c.in.arith(c, '+', co->war_support, a, loc)); break;
        case Stat::PoliticalPower: co->political_power = c.in.arith(c, '+', co->political_power, a, loc); break;
        case Stat::ResearchSlots: co->research_slots += static_cast<int32_t>(a.whole_trunc()); break;
        case Stat::SetResearchSlots: co->research_slots = static_cast<int32_t>(a.whole_trunc()); break;
        }
    }
};

template <StatEffect::Stat S>
std::unique_ptr<EffectOp> make_stat(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<StatEffect>();
    e->stat = S;
    if (!in.compile_operand(f, n, e->amount)) return nullptr;
    return e;
}

// ---- technology ----

struct SetTechnology : EffectOp {
    std::vector<NameArg> techs;
    void run(Ctx& c) const override {
        world::Country* co = self_country(c);
        if (!co) return;
        for (const auto& t : techs) {
            const Sym s = eval_name_arg(c, t, loc);
            if (s != kNoSym && co->techs.insert(s).second) co->tech_order.push_back(s);
        }
        mark_dirty(c, c.self());
    }
};

std::unique_ptr<EffectOp> make_set_technology(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<SetTechnology>();
    const SrcLoc l = in.loc(f, n);
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (c->key == kNoSym || bi::key_is(*c, "popup")) continue;
        Fixed v;
        if (Fixed::parse(sym_view(c->value), v) && v.raw() <= 0) continue; // `tech = 0` does not grant it
        NameArg a;
        if (compile_name_arg(in, l, sym_view(c->key), a)) e->techs.push_back(std::move(a));
    }
    return e;
}

// ---- events ----

struct FireEvent : EffectOp {
    Sym id = kNoSym;
    bool has_days = false;
    Operand days;
    void run(Ctx& c) const override {
        const int64_t d = has_days ? c.in.eval(c, days, loc).num.whole_trunc() : 0;
        if (c.in.hooks.fire_event) {
            c.in.hooks.fire_event(c, id, d, loc);
        } else {
            c.in.diag(DiagKind::EventNotLoaded, loc, "event " + sym_str(id) + " fired but no event system is attached");
        }
    }
};

std::unique_ptr<EffectOp> make_event(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<FireEvent>();
    if (!n.is_block) {
        e->id = n.value;
        return e;
    }
    if (const Node* id = bi::find_child(f, n, "id")) e->id = id->value;
    if (const Node* d = bi::find_child(f, n, "days")) e->has_days = in.compile_operand(f, *d, e->days);
    if (e->id == kNoSym) {
        in.diag(DiagKind::BadParameter, in.loc(f, n), "event effect without id");
        return nullptr;
    }
    return e;
}

// ---- native resources / buildings ----

struct AddResource : EffectOp {
    NameArg type;
    Operand amount;
    bool has_state = false;
    Operand state;
    void run(Ctx& c) const override {
        ScopeRef target = has_state ? c.in.eval(c, state, loc).as_scope() : c.self();
        if (!target.is_state() || !c.w.state(target.id)) {
            c.in.diag(DiagKind::InvalidScope, loc, "add_resource needs a state scope");
            return;
        }
        const Sym r = eval_name_arg(c, type, loc);
        const Fixed a = c.in.eval(c, amount, loc).num;
        if (c.in.hooks.add_resource) {
            c.in.hooks.add_resource(target, r, a);
        } else {
            world::State* st = c.w.state(target.id);
            st->resources[r] = c.in.arith(c, '+', st->resources[r], a, loc);
        }
    }
};

std::unique_ptr<EffectOp> make_add_resource(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<AddResource>();
    const SrcLoc l = in.loc(f, n);
    const Node* type = bi::find_child(f, n, "type");
    const Node* amount = bi::find_child(f, n, "amount");
    if (!type || !amount) {
        in.diag(DiagKind::BadParameter, l, "add_resource needs type and amount");
        return nullptr;
    }
    compile_name_arg(in, l, sym_view(type->value), e->type);
    in.compile_operand(f, *amount, e->amount);
    if (const Node* s = bi::find_child(f, n, "state")) e->has_state = in.compile_operand(f, *s, e->state);
    return e;
}

struct AddBuilding : EffectOp {
    Sym type = kNoSym;
    Operand level;
    void run(Ctx& c) const override {
        const ScopeRef s = c.self();
        world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
        if (!st) {
            c.in.diag(DiagKind::InvalidScope, loc, "add_building_construction outside state scope");
            return;
        }
        st->buildings[type] += static_cast<int32_t>(c.in.eval(c, level, loc).num.whole_trunc());
        if (st->buildings[type] < 0) st->buildings[type] = 0;
    }
};

std::unique_ptr<EffectOp> make_add_building(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<AddBuilding>();
    const Node* type = bi::find_child(f, n, "type");
    const Node* level = bi::find_child(f, n, "level");
    if (!type || !level) {
        in.diag(DiagKind::BadParameter, in.loc(f, n), "add_building_construction needs type and level");
        return nullptr;
    }
    e->type = type->value;
    in.compile_operand(f, *level, e->level);
    return e;
}

struct GiveGuarantee : EffectOp {
    Operand target;
    void run(Ctx& c) const override {
        world::Country* co = self_country(c);
        const ScopeRef t = c.in.eval(c, target, loc).as_scope();
        world::Country* other = t.is_country() ? c.w.country(t.id) : nullptr;
        if (co && other) other->guaranteed_by.insert(co->index);
    }
};

std::unique_ptr<EffectOp> make_give_guarantee(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<GiveGuarantee>();
    if (!in.compile_operand(f, n, e->target)) return nullptr;
    return e;
}

} // namespace

void register_effects_world(Registry& r) {
    r.effect("set_country_flag", make_set_flag<FlagScope::Country, false>);
    r.effect("clr_country_flag", make_set_flag<FlagScope::Country, true>);
    r.effect("set_state_flag", make_set_flag<FlagScope::State, false>);
    r.effect("clr_state_flag", make_set_flag<FlagScope::State, true>);
    r.effect("set_global_flag", make_set_flag<FlagScope::Global, false>);
    r.effect("clr_global_flag", make_set_flag<FlagScope::Global, true>);
    r.effect("add_ideas", make_add_ideas);
    r.effect("remove_ideas", make_remove_ideas);
    r.effect("swap_ideas", make_swap_ideas);
    r.effect("add_dynamic_modifier", make_dynmod<DynModEffect::Mode::Add>);
    r.effect("remove_dynamic_modifier", make_dynmod<DynModEffect::Mode::Remove>);
    r.effect("force_update_dynamic_modifier", make_dynmod<DynModEffect::Mode::ForceUpdate>);
    r.effect("add_stability", make_stat<StatEffect::Stat::Stability>);
    r.effect("add_war_support", make_stat<StatEffect::Stat::WarSupport>);
    r.effect("add_political_power", make_stat<StatEffect::Stat::PoliticalPower>);
    r.effect("add_research_slot", make_stat<StatEffect::Stat::ResearchSlots>);
    r.effect("set_research_slots", make_stat<StatEffect::Stat::SetResearchSlots>);
    r.effect("set_technology", make_set_technology);
    r.effect("country_event", make_event);
    r.effect("state_event", make_event);
    r.effect("news_event", make_event, BuiltinStatus::Approx);
    r.effect("add_resource", make_add_resource);
    r.effect("add_building_construction", make_add_building, BuiltinStatus::Approx);
    r.effect("give_guarantee", make_give_guarantee);
}

} // namespace pdx
