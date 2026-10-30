#pragma once

// History import (history/states, history/countries, map/definition.csv).
//
// HOI4 applies undated history entries first, then dated blocks (<= start date) in chronological order.
// Structural state keys (owner, controller, cores, buildings, victory points) are applied natively; every
// other entry (set_variable, add_ideas, set_technology, set_tech_* scripted effects, set_autonomy, ...) is
// executed by the interpreter as a real effect, so history-seeded eco variables (e.g. base_wage via
// set_tech_*, <industry>_size, starting_pm@) are produced exactly as in game.

#include "loader/mod_data.h"
#include "loader/scenario.h"
#include "world/world.h"

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace loader {

struct HistoryPrescan {
    struct CountryInfo {
        const pdx::ScriptFile* file = nullptr;
        std::vector<Sym> subjects;
        Sym market_leader = pdx::kNoSym;
    };
    std::unordered_map<Sym, CountryInfo> countries;
    struct StateInfo {
        const pdx::ScriptFile* file = nullptr;
        const pdx::Node* block = nullptr; // the `state = { }` block
        Sym owner = pdx::kNoSym;
        Sym controller = pdx::kNoSym;
    };
    std::map<int32_t, StateInfo> states; // ordered by id
    std::unordered_map<int32_t, bool> coastal_provinces;
};

// `history_root` holds history/countries + history/states (default: the mod root). With a custom folder that
// gives UCT no state, the mod's own UCT states are added (UCT must exist: it hosts the eco ticks).
void prescan_history(pdx::Interp& in, const std::string& root, const world::Date& start, HistoryPrescan& out,
                     const std::string& history_root = "");

// Scenario tag selection: listed tags (+UCT), then subjects (2 levels), then market members.
std::unordered_set<Sym> select_countries(const Scenario& sc, const HistoryPrescan& p);

// Fantasy setups: load exactly these states (any owner) and/or hand every loaded state to one country.
struct StateSetup {
    std::unordered_set<int32_t> only_states; // empty = the selected countries' states (UCT's always load)
    Sym force_owner = pdx::kNoSym;           // owner, controller and sole core of every loaded state
};

// Builds the world: all tag shells, the selected countries' states, then runs their history.
void apply_history(pdx::Interp& in, world::World& w, const ModData& d, const HistoryPrescan& p,
                   const std::unordered_set<Sym>& selected, const world::Date& start, const StateSetup& setup = {});

} // namespace loader
