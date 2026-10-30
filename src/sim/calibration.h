#pragma once

// Game-vs-simulator comparison using the debug effects in toi/common/scripted_effects/_economy_sim_probes.txt:
//   eco_sim_probes       -> "EOPROBE|name|value" lines   (engine-behaviour probes; `calibrate`)
//   eco_sim_parity_dump  -> "EOSIM|key|value" lines      (eco snapshot; `parity`)
// The same effect runs in the simulator, so keys line up one-to-one.

#include "sim/simulation.h"

#include <string>
#include <utility>
#include <vector>

namespace sim {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

// Extracts the `block`-th (1-based) BEGIN..END block of `prefix|key|value` lines from text lines.
bool extract_block(const std::vector<std::string>& lines, const std::string& prefix, int block, KeyValues& out,
                   std::string& error);
bool read_lines(const std::string& path, std::vector<std::string>& out, std::string& error);

// Default game.log location for a mod root (<Documents>/Paradox Interactive/Hearts of Iron IV/logs/game.log).
std::string default_game_log(const std::string& mod_root);

// Markdown reports.
std::string calibration_report(const KeyValues& game, const KeyValues& sim);
std::string parity_report(const KeyValues& game, const KeyValues& sim, size_t max_rows);

} // namespace sim
