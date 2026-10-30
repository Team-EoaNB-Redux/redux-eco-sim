#include "sim/calibration.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace sim {

namespace fs = std::filesystem;

namespace {

bool parse_number(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '"')) s.pop_back();
    return s;
}

// What each probe tells us, and which knob it maps to.
const std::map<std::string, std::string>& probe_hints() {
    static const std::map<std::string, std::string> h = {
        {"div_1_3", "0.33333 = truncating division (div_rounding = truncate)"},
        {"div_1_3_times_3", "0.99999 = truncation at every step"},
        {"div_neg1_3", "-0.33333 = truncation toward zero; -0.33334 = floor"},
        {"div_2_3", "0.66666 = truncate; 0.66667 = round (div_rounding = half_away)"},
        {"mul_tiny_half", "0 = truncating multiply; 0.00001 = rounding (mul_rounding)"},
        {"div_by_zero", "0 -> div_by_zero = zero; 5 -> keep_target; huge -> saturate"},
        {"mathexpr_div_by_zero", "math expressions may differ from divide_temp_variable"},
        {"mod_neg7_3", "2 = Euclidean (implemented); -1 = C remainder"},
        {"overflow_add", "-2147483648 = wrap32; 2147483647.99999 = saturate; 2147483648 = none"},
        {"overflow_mul", "1e10 does not fit in 32-bit whole: shows the wrap/saturate rule for multiply"},
        {"round_neg_2_5", "-3 = halves away from zero"},
        {"fmt_default_2_3", "2.3 = trailing zeros trimmed (text_default_decimals = -1)"},
        {"fmt_default_7", "7 = integers print without decimals"},
        {"fmt_1_2_36", "2.3 = |N truncates; 2.4 = |N rounds (textfmt)"},
        {"fmt_1_neg2_36", "-2.3 = truncate toward zero"},
        {"fmt_0_2_7", "2 = truncate; 3 = round"},
        {"pow_2_half", "fractional pow approximation (1.41421 exact)"},
        {"pow_1_1_cubed", "1.331 = integer pow by repeated multiply"},
        {"log10_100", "2 = log base 10"},
        {"clamp_max_then_min", "6 = clamp bounds apply in written order"},
        {"temp_shadows_var", "2 = temp_first; 1 = scope_first"},
        {"ghost_read", "42 = elements past ^num are kept"},
        {"ghost_num", "1 = writing arr^5 does not change ^num"},
        {"stale_after_remove", "3 = removed tail element stays readable; 0 = cleared"},
        {"ghost_after_clear", "0 = clear_array clears element variables; 42 = they survive"},
        {"country_value", "numeric value of a country in a variable (raw 2^30 + tag bytes; must be > 10500)"},
        {"state_value", "numeric value of a state in a variable (raw id - 2^30)"},
        {"token_value_is_number", "numeric value of a token in a variable"},
        {"bare_at_known_token", "7 = x@known_token reads the token-indexed variable (bare_at_target)"},
        {"bare_at_temp_holding_token", "3 = bare @name reads a variable holding a token; 0 = literal name"},
    };
    return h;
}

} // namespace

bool read_lines(const std::string& path, std::vector<std::string>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return true;
}

// `-10737.40872.x` (a state printed with [?s]: raw id - 2^30) -> `952.x`
static std::string readable_scope_key(const std::string& key) {
    if (key.size() < 13 || key[0] != '-' || key[12] != '.') return key;
    const std::string num = key.substr(1, 5) + key.substr(7, 5);
    if (key[6] != '.' || num.find_first_not_of("0123456789") != std::string::npos) return key;
    const long long id = (1LL << 30) - std::stoll(num);
    if (id <= 0 || id >= (1 << 20)) return key;
    return std::to_string(id) + key.substr(12);
}

bool extract_block(const std::vector<std::string>& lines, const std::string& prefix, int block, KeyValues& out,
                   std::string& error) {
    const std::string marker = prefix + "|";
    int seen = 0;
    bool inside = false;
    for (const auto& raw : lines) {
        const size_t at = raw.find(marker);
        if (at == std::string::npos) continue;
        const std::string body = trim(raw.substr(at + marker.size()));
        const size_t bar = body.find('|');
        const std::string key = bar == std::string::npos ? body : body.substr(0, bar);
        const std::string value = bar == std::string::npos ? "" : body.substr(bar + 1);
        if (key == "BEGIN") {
            ++seen;
            inside = seen == block;
            if (inside) out.clear();
            continue;
        }
        if (key == "END") {
            if (inside) return true;
            continue;
        }
        if (inside) out.emplace_back(readable_scope_key(key), value);
    }
    if (inside) return true; // unterminated last block: use what we have
    error = "no " + prefix + " block #" + std::to_string(block) + " found (" + std::to_string(seen) + " blocks present)";
    return false;
}

std::string default_game_log(const std::string& mod_root) {
    // <...>/Hearts of Iron IV/mod/EoaNBR/toi -> <...>/Hearts of Iron IV/logs/game.log
    return (fs::path(mod_root).parent_path().parent_path().parent_path() / "logs" / "game.log").string();
}

std::string calibration_report(const KeyValues& game, const KeyValues& sim) {
    std::map<std::string, std::string> s(sim.begin(), sim.end());
    std::ostringstream md;
    md << "| probe | game | sim | | meaning |\n|---|---|---|---|---|\n";
    int mismatches = 0;
    for (const auto& [k, gv] : game) {
        auto it = s.find(k);
        const std::string sv = it == s.end() ? "(missing)" : it->second;
        double a = 0, b = 0;
        const bool same = gv == sv || (parse_number(gv, a) && parse_number(sv, b) && std::abs(a - b) < 0.000015);
        mismatches += !same;
        auto h = probe_hints().find(k);
        md << "| `" << k << "` | " << gv << " | " << sv << " | " << (same ? "ok" : "**DIFF**") << " | "
           << (h == probe_hints().end() ? "" : h->second) << " |\n";
    }
    md << "\n" << mismatches << " of " << game.size() << " probes differ. Adjust the matching knob in "
       << "src/pdx/semantics.h (or the implementation) and record the result in docs/SEMANTICS.md.\n";
    return md.str();
}

std::string parity_report(const KeyValues& game, const KeyValues& sim, size_t max_rows) {
    std::map<std::string, std::string> s(sim.begin(), sim.end());
    std::map<std::string, std::string> g(game.begin(), game.end());
    struct Row {
        std::string key, gv, sv;
        double rel = 0;
    };
    std::vector<Row> diffs;
    std::vector<std::string> only_game, only_sim;
    size_t matches = 0;
    for (const auto& [k, gv] : game) {
        auto it = s.find(k);
        if (it == s.end()) {
            only_game.push_back(k);
            continue;
        }
        double a = 0, b = 0;
        if (parse_number(gv, a) && parse_number(it->second, b)) {
            const double diff = std::abs(a - b);
            if (diff <= 0.00002) {
                ++matches;
                continue;
            }
            const double rel = diff / std::max({std::abs(a), std::abs(b), 1e-9});
            diffs.push_back({k, gv, it->second, rel});
        } else if (gv == it->second) {
            ++matches;
        } else {
            diffs.push_back({k, gv, it->second, 1e9});
        }
    }
    for (const auto& [k, sv] : sim) {
        if (!g.count(k)) only_sim.push_back(k);
    }
    std::stable_sort(diffs.begin(), diffs.end(), [](const Row& a, const Row& b) { return a.rel > b.rel; });
    std::ostringstream md;
    md << "# Parity: game vs simulator\n\n"
       << "- " << game.size() << " game values, " << sim.size() << " sim values\n"
       << "- " << matches << " match (|diff| <= 0.00002), " << diffs.size() << " differ, " << only_game.size()
       << " only in game, " << only_sim.size() << " only in sim\n\n";
    md << "## Largest differences (relative)\n\n| key | game | sim | rel. diff |\n|---|---:|---:|---:|\n";
    for (size_t i = 0; i < std::min(max_rows, diffs.size()); ++i) {
        const auto& r = diffs[i];
        md << "| `" << r.key << "` | " << r.gv << " | " << r.sv << " | ";
        if (r.rel >= 1e9) md << "text";
        else md << std::fixed << std::setprecision(1) << r.rel * 100.0 << "%";
        md << " |\n";
    }
    if (diffs.size() > max_rows) md << "\n… " << (diffs.size() - max_rows) << " more differences\n";
    auto list = [&](const char* title, const std::vector<std::string>& keys) {
        if (keys.empty()) return;
        md << "\n## " << title << " (" << keys.size() << ")\n\n";
        for (size_t i = 0; i < std::min<size_t>(40, keys.size()); ++i) md << "- `" << keys[i] << "`\n";
        if (keys.size() > 40) md << "- …\n";
    };
    list("Only in game", only_game);
    list("Only in sim", only_sim);
    return md.str();
}

} // namespace sim
