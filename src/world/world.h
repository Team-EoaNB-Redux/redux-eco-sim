#pragma once

// The mocked HOI4 world: just enough engine state for the economy's non-variable checks.
// Countries are indexed densely (index = position in the country_tags load order); states are indexed by
// their HOI4 state id (holes allowed). Every tag from common/country_tags exists as a Country shell, but
// only those owning states (or explicitly loaded) have `exists = true`, as in the game.

#include "pdx/symbols.h"
#include "pdx/value.h"
#include "world/calendar.h"
#include "world/rng.h"
#include "world/var_store.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {

using pdx::Fixed;
using pdx::ScopeRef;

struct Flag {
    int64_t value = 1;
    Date set_date;
};

struct DynamicModifierInst {
    Sym name = pdx::kNoSym;
    ScopeRef var_scope; // `scope =` parameter: where variable-valued entries are read; none = the owner
    int64_t expires_ordinal = -1; // -1 = permanent (`days =` sets an expiry)
};

struct Country {
    Sym tag = pdx::kNoSym;
    int32_t index = -1;
    bool exists = false;
    bool is_ai = true;
    Sym original_tag = pdx::kNoSym;

    int32_t capital = -1; // state id
    int32_t overlord = -1; // country index
    std::vector<int32_t> subjects;

    std::vector<Sym> ideas; // insertion order kept (deterministic modifier sums)
    std::unordered_set<Sym> techs;
    std::vector<Sym> tech_order; // research order, for the researched_techs array
    std::unordered_map<Sym, Flag> flags;
    std::vector<DynamicModifierInst> dynamic_modifiers;

    std::unordered_set<int32_t> at_war_with;
    std::unordered_set<int32_t> embargoing; // countries this one embargoes
    std::unordered_set<int32_t> guaranteed_by;
    int32_t faction = -1;

    std::unordered_map<Sym, int32_t> battalions; // by unit type
    std::unordered_map<Sym, int32_t> ships;      // by ship type
    std::unordered_map<Sym, int32_t> planes;

    Fixed stability = Fixed::from_raw(50000);   // 0.5
    Fixed war_support = Fixed::from_raw(50000); // 0.5
    Fixed political_power;
    int32_t research_slots = 3;

    // Engine values the simulator does not model (manpower pools, divisions, doctrine levels, ...).
    // Game variables with no model read from here (default 0); scenarios can set them.
    std::unordered_map<Sym, Fixed> engine;

    VarStore vars;

    // Derived lists (rebuilt by World::rebuild_state_lists), sorted by state id.
    std::vector<int32_t> owned_states;
    std::vector<int32_t> controlled_states;
    std::vector<int32_t> core_states;
};

struct State {
    int32_t id = -1;
    bool exists = false;
    Sym name = pdx::kNoSym;
    int32_t owner = -1;      // country index
    int32_t controller = -1; // country index
    std::vector<int32_t> cores; // country indices
    Fixed manpower;          // raw state manpower (people)
    Sym category = pdx::kNoSym;
    Sym continent = pdx::kNoSym;
    bool coastal = false;
    std::unordered_map<Sym, Fixed> resources; // native map resources (resource@x)
    std::unordered_map<Sym, int32_t> buildings;
    std::unordered_map<Sym, int32_t> damaged_buildings;
    Fixed resistance;
    std::vector<int32_t> provinces;
    std::unordered_map<Sym, Flag> flags;
    std::vector<DynamicModifierInst> dynamic_modifiers;
    std::unordered_map<Sym, Fixed> engine; // see Country::engine

    VarStore vars;
};

class World {
public:
    VarStore global_vars;
    std::unordered_map<Sym, Flag> global_flags;
    std::vector<Country> countries;
    std::vector<State> states; // indexed by state id
    Date date;
    Rng rng;

    // ---- lookups ----
    int32_t country_index(Sym tag) const {
        auto it = tag_index_.find(tag);
        return it == tag_index_.end() ? -1 : it->second;
    }
    Country* country(int32_t index) {
        return (index >= 0 && index < static_cast<int32_t>(countries.size())) ? &countries[static_cast<size_t>(index)] : nullptr;
    }
    const Country* country(int32_t index) const {
        return (index >= 0 && index < static_cast<int32_t>(countries.size())) ? &countries[static_cast<size_t>(index)] : nullptr;
    }
    State* state(int32_t id) {
        return (id > 0 && id < static_cast<int32_t>(states.size()) && states[static_cast<size_t>(id)].exists)
                   ? &states[static_cast<size_t>(id)] : nullptr;
    }
    const State* state(int32_t id) const {
        return (id > 0 && id < static_cast<int32_t>(states.size()) && states[static_cast<size_t>(id)].exists)
                   ? &states[static_cast<size_t>(id)] : nullptr;
    }

    // Creates the tag shell if missing; returns its index.
    int32_t ensure_country(Sym tag);
    State& ensure_state(int32_t id);

    // Variable storage of a scope (nullptr for an invalid scope).
    VarStore* vars(ScopeRef s);
    // Flags of a scope (nullptr for an invalid scope).
    std::unordered_map<Sym, Flag>* flags(ScopeRef s);
    std::vector<DynamicModifierInst>* dynamic_modifiers(ScopeRef s);
    bool scope_exists(ScopeRef s) const;

    // Recomputes owned/controlled/core state lists and `exists` (owns at least one state).
    void rebuild_state_lists();

    // Iteration helpers (deterministic order).
    std::vector<int32_t> existing_countries() const;
    std::vector<int32_t> existing_states() const;

    std::string scope_name(ScopeRef s) const; // "ENG", "state 952", "global"

    // English localisation (localisation/english/*.yml): key -> text. Display only; never affects the sim.
    std::unordered_map<std::string, std::string> localisation;
    std::string localised(const std::string& key) const {
        auto it = localisation.find(key);
        return it == localisation.end() || it->second.empty() ? key : it->second;
    }
    // [X.GetName]: localised country name ("Britain") / state name ("London").
    std::string display_name(ScopeRef s) const;

private:
    std::unordered_map<Sym, int32_t> tag_index_;
};

} // namespace world
