#pragma once

// Scenario configuration (Clausewitz format):
//
// scenario = {
//     name = "ENG 1857"   start_date = 1857.1.1   seed = 12345
//     countries = { UCT ENG }          # or { ALL }
//     include_subjects = yes           # overlord tree from history (2 levels, like market_setup)
//     include_market_members = yes     # countries whose history sets market_leader to a selected tag
//     players = { }                    # tags with is_ai = no
//     player_event_choice = ai_chance  # or first_option
//     on_actions = { "_eoanbr_constants_on_startup.txt" "_eoanbr_eco.txt" }
//     events = { "00_economy.txt" }
//     skip_effects = { econ_rerender_gdp_graph ... }
//     overrides = { global.wage_change_cap = 0.02  ENG.treasury = 1000 }   # applied after startup
//     history = "../fantasy/british_isles"   # own history/countries + history/states (relative to this file)
//     states = { 952 128 }  state_owner = ENG   # fantasy setup: only these states, all owned by ENG
//     pins = { ENG.trade@token:food = 5571 }   # held for the whole run: script writes keep this value
//     engine = { ENG = { manpower_k = 500 } }                                # unmodelled engine values
//     units = { ENG = { infantry = 40 } }                                    # battalions by type
//     ships = { ENG = { lineship = 12 } }                                    # ships by type
//     ideas = { ENG = { steady_growth = yes very_low_taxes = no } }          # on top of history: added / removed
//     techs = { ENG = { tech_fish_fertilizer = yes } }                       # same
//     include = "save_inputs_1857.txt"                                       # more of these keys, from another file
//     difficulty = easy                                                      # adds diff_easy_player / diff_easy_ai
//     semantics = { overflow = wrap32 mul_rounding = truncate div_by_zero = keep_target ... }
// }

#include "pdx/semantics.h"
#include "pdx/symbols.h"
#include "world/calendar.h"

#include <string>
#include <utility>
#include <vector>

namespace loader {

struct Scenario {
    std::string path;
    std::string name = "unnamed";
    world::Date start_date{1857, 5, 11};
    uint64_t seed = 12345;
    std::vector<pdx::Sym> countries;
    bool all_countries = false;
    bool include_subjects = true;
    bool include_market_members = true;
    std::vector<pdx::Sym> players;
    bool player_first_option = false;
    std::vector<std::string> on_action_files;
    std::vector<std::string> event_files;
    std::vector<pdx::Sym> skip_effects;
    std::vector<std::pair<std::string, std::string>> overrides; // "ENG.treasury" -> "1000"
    std::vector<std::pair<std::string, std::string>> pins;      // same form; applied before on_startup
    std::string history_dir;              // fantasy setup: folder with history/countries + history/states
    std::vector<int32_t> states;          // fantasy setup: load exactly these state ids
    pdx::Sym state_owner = pdx::kNoSym;   // fantasy setup: every loaded state belongs to this tag
    struct ScopedValues {
        pdx::Sym tag = pdx::kNoSym;
        std::vector<std::pair<pdx::Sym, std::string>> values;
    };
    std::vector<ScopedValues> engine;
    std::vector<ScopedValues> units;
    std::vector<ScopedValues> ships;
    std::string difficulty;               // the game's setting: the engine applies the static diff_<this>_player / _ai
    std::vector<ScopedValues> ideas;
    std::vector<ScopedValues> techs;
    pdx::Semantics semantics;
};

bool load_scenario(const std::string& path, Scenario& out, std::string& error);

// Applies `key = value` semantics overrides (same keys as the scenario's semantics block), e.g. from the CLI
// `--sem div_by_zero=zero`.
bool apply_semantics_text(const std::string& text, pdx::Semantics& s, std::string& error);

} // namespace loader
