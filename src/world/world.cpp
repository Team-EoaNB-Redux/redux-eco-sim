#include "world/world.h"

#include <algorithm>

namespace world {

std::string World::display_name(ScopeRef s) const {
    if (const Country* c = s.is_country() ? country(s.id) : nullptr) return localised(pdx::sym_str(c->tag));
    if (const State* st = s.is_state() ? state(s.id) : nullptr)
        return localised(st->name != pdx::kNoSym ? pdx::sym_str(st->name) : "STATE_" + std::to_string(s.id));
    return "";
}

int32_t World::ensure_country(Sym tag) {
    const int32_t existing = country_index(tag);
    if (existing >= 0) {
        return existing;
    }
    Country c;
    c.tag = tag;
    c.original_tag = tag;
    c.index = static_cast<int32_t>(countries.size());
    countries.push_back(std::move(c));
    tag_index_[tag] = countries.back().index;
    pdx::register_country_code(countries.back().index, pdx::tag_code(pdx::sym_str(tag)));
    return countries.back().index;
}

State& World::ensure_state(int32_t id) {
    if (id >= static_cast<int32_t>(states.size())) {
        states.resize(static_cast<size_t>(id) + 1);
    }
    State& s = states[static_cast<size_t>(id)];
    if (!s.exists) {
        s.id = id;
        s.exists = true;
    }
    return s;
}

VarStore* World::vars(ScopeRef s) {
    switch (s.type) {
    case pdx::ScopeType::Global: return &global_vars;
    case pdx::ScopeType::Country: {
        Country* c = country(s.id);
        return c ? &c->vars : nullptr;
    }
    case pdx::ScopeType::State: {
        State* st = state(s.id);
        return st ? &st->vars : nullptr;
    }
    case pdx::ScopeType::None:
    default: return nullptr;
    }
}

std::unordered_map<Sym, Flag>* World::flags(ScopeRef s) {
    switch (s.type) {
    case pdx::ScopeType::Global: return &global_flags;
    case pdx::ScopeType::Country: {
        Country* c = country(s.id);
        return c ? &c->flags : nullptr;
    }
    case pdx::ScopeType::State: {
        State* st = state(s.id);
        return st ? &st->flags : nullptr;
    }
    default: return nullptr;
    }
}

std::vector<DynamicModifierInst>* World::dynamic_modifiers(ScopeRef s) {
    if (s.is_country()) {
        Country* c = country(s.id);
        return c ? &c->dynamic_modifiers : nullptr;
    }
    if (s.is_state()) {
        State* st = state(s.id);
        return st ? &st->dynamic_modifiers : nullptr;
    }
    return nullptr;
}

bool World::scope_exists(ScopeRef s) const {
    switch (s.type) {
    case pdx::ScopeType::Global: return true;
    case pdx::ScopeType::Country: return country(s.id) != nullptr;
    case pdx::ScopeType::State: return state(s.id) != nullptr;
    default: return false;
    }
}

void World::rebuild_state_lists() {
    for (auto& c : countries) {
        c.owned_states.clear();
        c.controlled_states.clear();
        c.core_states.clear();
    }
    for (const auto& s : states) {
        if (!s.exists) {
            continue;
        }
        if (Country* o = country(s.owner)) {
            o->owned_states.push_back(s.id);
        }
        if (Country* c = country(s.controller)) {
            c->controlled_states.push_back(s.id);
        }
        for (int32_t core : s.cores) {
            if (Country* c = country(core)) {
                c->core_states.push_back(s.id);
            }
        }
    }
    for (auto& c : countries) {
        c.exists = !c.owned_states.empty();
    }
}

std::vector<int32_t> World::existing_countries() const {
    std::vector<int32_t> out;
    for (const auto& c : countries) {
        if (c.exists) {
            out.push_back(c.index);
        }
    }
    return out;
}

std::vector<int32_t> World::existing_states() const {
    std::vector<int32_t> out;
    for (const auto& s : states) {
        if (s.exists) {
            out.push_back(s.id);
        }
    }
    return out;
}

std::string World::scope_name(ScopeRef s) const {
    switch (s.type) {
    case pdx::ScopeType::Global: return "global";
    case pdx::ScopeType::Country: {
        const Country* c = country(s.id);
        return c ? pdx::sym_str(c->tag) : "country#" + std::to_string(s.id);
    }
    case pdx::ScopeType::State: return "state " + std::to_string(s.id);
    default: return "none";
    }
}

} // namespace world
