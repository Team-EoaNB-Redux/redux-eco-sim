// ecosim_cli — headless entry point (the AI's tool). See CLAUDE.md for the workflow.

#include "cli/args.h"
#include "loader/mod_fs.h"
#include "pdx/parser.h"
#include "sim/calibration.h"
#include "sim/report.h"
#include "sim/script_test.h"
#include "sim/simulation.h"
#include "sim/version.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

namespace fs = std::filesystem;

int print_help() {
    std::cout << "ecosim_cli " << ecosim::kVersion << "\n"
              << "usage: ecosim_cli <command> [options]\n\n"
              << "commands:\n"
              << "  run    [--scenario <file>] [--months N] [--print <ref>]... [--diag-limit N] [--log]\n"
              << "         load the scenario, run on_startup, simulate N months (default 12), print diagnostics\n"
              << "         and the values of --print refs (e.g. --print ENG.treasury --print 952.base_wage)\n"
              << "  run    ... --out runs/<name> [--watch-file watches/core.txt] [--set ref=value]...\n"
              << "         recorded run: writes report.md (read this first), series.csv, diagnostics.json, sim.log\n"
              << "  compare <run dir A> <run dir B>\n"
              << "         A/B diff of two recorded runs (final values, % change, first divergent month)\n"
              << "  calibrate [game.log]   compare in-game EOPROBE lines (effect eco_sim_probes = yes) with the sim\n"
              << "  parity [game.log] [--dump K] [--months N] [--out file.md]\n"
              << "         compare the K-th in-game EOSIM dump (effect eco_sim_parity_dump = yes) with the sim\n"
              << "         after startup + N months; game.log defaults to Documents/.../Hearts of Iron IV/logs/game.log\n"
              << "  effect [--scenario <file>] [--after-months N] [--scope ENG] \"<effect script>\"\n"
              << "         run effect text in a scope after startup (+N months), print its log output\n"
              << "  check --parse-only [--verbose]\n"
              << "         parse every script file under common/, history/, events/ and report errors\n"
              << "  test [--dir <dir>] [--verbose]\n"
              << "         run the HOI4-script golden tests (tests/script/*.txt)\n"
              << "  version | help\n\n"
              << "common options: --sem key=value (semantics knob, repeatable; see docs/SEMANTICS.md)\n"
              << "                --set ref=value (override a variable after startup, repeatable)\n"
              << "                --mod <toi dir> (default: nearest toi/ above the cwd or the executable)\n"
              << "                --scenario defaults to scenarios/eng_1857.txt next to bin/\n";
    return 0;
}

std::string resolve_mod_root(const cli::Args& args) {
    std::string root = loader::find_mod_root(args.get("mod"), "");
    if (root.empty() && !args.has("mod")) {
        root = loader::find_mod_root("", loader::executable_dir());
    }
    if (root.empty()) {
        std::cerr << "error: could not find the mod root (a dir containing common/scripted_effects). "
                     "Pass --mod <path-to-toi>.\n";
    }
    return root;
}

std::string resolve_scenario(const cli::Args& args) {
    const std::string s = args.get("scenario");
    if (!s.empty()) return s;
    return (fs::path(loader::executable_dir()).parent_path() / "scenarios" / "eng_1857.txt").string();
}

int cmd_check(const cli::Args& args) {
    const std::string root = resolve_mod_root(args);
    if (root.empty()) {
        return 2;
    }
    const bool verbose = args.has("verbose");
    const auto t0 = std::chrono::steady_clock::now();
    size_t files = 0, bytes_nodes = 0, error_files = 0, errors = 0;
    for (const char* dir : {"common", "history", "events"}) {
        for (const auto& path : loader::list_files(root, dir)) {
            pdx::ScriptFile f;
            pdx::parse_file(path, f);
            ++files;
            bytes_nodes += f.nodes.size();
            if (!f.errors.empty()) {
                ++error_files;
                errors += f.errors.size();
                const std::string rel = loader::relative_to(path, root);
                const size_t shown = verbose ? f.errors.size() : std::min<size_t>(f.errors.size(), 3);
                for (size_t i = 0; i < shown; ++i) {
                    std::cout << rel << ":" << f.errors[i].line << ": parse error: " << f.errors[i].message << "\n";
                }
                if (shown < f.errors.size()) {
                    std::cout << rel << ": ... " << (f.errors.size() - shown) << " more\n";
                }
            }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "mod root: " << root << "\n"
              << "parsed " << files << " files, " << bytes_nodes << " nodes, " << pdx::Symbols::instance().size()
              << " symbols in " << secs << " s\n"
              << errors << " parse errors in " << error_files << " files\n";
    return errors == 0 ? 0 : 1;
}

int cmd_test(const cli::Args& args) {
    std::string dir = args.get("dir");
    if (dir.empty()) {
        dir = (fs::path(loader::executable_dir()).parent_path() / "tests" / "script").string();
    }
    const bool verbose = args.has("verbose");
    size_t pass = 0, fail = 0;
    for (const auto& file : loader::list_files(dir, "", ".txt", false)) {
        for (const auto& r : sim::run_script_test_file(file)) {
            const std::string name = fs::path(r.file).filename().string() + " :: " + r.name;
            if (r.pass) {
                ++pass;
                if (verbose) std::cout << "  PASS " << name << "\n";
            } else {
                ++fail;
                std::cout << "  FAIL " << name << "\n";
                for (const auto& m : r.messages) std::cout << "        " << m << "\n";
            }
        }
    }
    std::cout << "script tests: " << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}

bool boot(const cli::Args& args, sim::Simulation& s) {
    const std::string root = resolve_mod_root(args);
    if (root.empty()) return false;
    std::string error;
    for (const auto& kv : args.all("sem")) {
        std::string t = kv;
        const size_t eq = t.find('=');
        if (eq != std::string::npos) t = t.substr(0, eq) + " = " + t.substr(eq + 1);
        s.semantics_overrides += t + " ";
    }
    if (!s.load(root, resolve_scenario(args), error)) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    s.startup();
    for (const auto& kv : args.all("set")) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) {
            std::cerr << "error: --set expects ref=value, got " << kv << "\n";
            return false;
        }
        std::vector<std::string> ignored;
        if (!s.run_effect(pdx::ScopeRef::global(), "set_variable = { " + kv.substr(0, eq) + " = " + kv.substr(eq + 1) + " }", ignored, error)) {
            std::cerr << "error: --set " << kv << ": " << error << "\n";
            return false;
        }
    }
    return true;
}

void print_refs(sim::Simulation& s, const cli::Args& args) {
    for (const auto& ref : args.all("print")) {
        pdx::Value v;
        std::string error;
        if (!s.eval(ref, v, error)) {
            std::cout << "  " << ref << " = <error: " << error << ">\n";
            continue;
        }
        std::cout << "  " << ref << " = " << v.num.to_string_exact() << "\n";
    }
}

int cmd_run(const cli::Args& args) {
    sim::Simulation s;
    if (!boot(args, s)) return 2;
    const int months = std::stoi(args.get("months", "12"));
    s.interp().profiling = args.has("profile") || args.has("out");
    const std::string out_dir = args.get("out");
    if (!out_dir.empty()) {
        // Recorded run: series.csv + report.md + diagnostics.json + sim.log
        sim::Recorder rec(s);
        std::string error;
        std::vector<std::string> files = args.all("watch-file");
        if (files.empty() && args.all("watch").empty()) {
            files.push_back((fs::path(loader::executable_dir()).parent_path() / "watches" / "core.txt").string());
        }
        for (const auto& f : files) {
            if (!rec.add_watch_file(f, error)) {
                std::cerr << "error: " << error << "\n";
                return 2;
            }
        }
        for (const auto& w : args.all("watch")) rec.add_watch(w);
        rec.sample();
        s.on_month = [&rec]() { rec.sample(); };
        s.run_months(months);
        if (!sim::write_run_outputs(out_dir, s, rec, months, error)) {
            std::cerr << "error: " << error << "\n";
            return 2;
        }
        std::cout << "wrote " << out_dir << "/report.md, series.csv, diagnostics.json, sim.log\n"
                  << "errors " << s.diagnostics().count_at_least(sim::Severity::Error) << ", warnings+ "
                  << s.diagnostics().count_at_least(sim::Severity::Warning) << " | " << s.tick_seconds / std::max(months, 1)
                  << " s/month\n";
        return 0;
    }
    const auto watches = args.all("watch");
    if (!watches.empty()) {
        std::cout << "date";
        for (const auto& w : watches) std::cout << "," << w;
        std::cout << "\n";
        auto row = [&s, &watches]() {
            std::cout << s.world().date.to_string();
            for (const auto& w : watches) {
                pdx::Value v;
                std::string error;
                std::cout << "," << (s.eval(w, v, error) ? v.num.to_string_fixed(3) : "ERR");
            }
            std::cout << "\n";
        };
        row();
        s.on_month = row;
    }
    s.run_months(months);
    const auto& w = s.world();
    std::cout << "scenario: " << s.scenario().name << " | date " << w.date.to_string() << " | " << s.months_elapsed()
              << " months\n"
              << "countries: " << w.existing_countries().size() << ", states: " << w.existing_states().size() << "\n"
              << "load " << s.load_seconds << " s, startup " << s.startup_seconds << " s, ticks " << s.tick_seconds << " s ("
              << (months > 0 ? s.tick_seconds / months : 0) << " s/month)\n";
    if (args.has("log")) {
        for (const auto& l : s.log()) std::cout << l << "\n";
    }
    print_refs(s, args);
    const size_t limit = static_cast<size_t>(std::stoi(args.get("diag-limit", "15")));
    std::cout << "\n== diagnostics ==\n" << s.diagnostics().format(limit);
    return s.diagnostics().count_at_least(sim::Severity::Error) == 0 ? 0 : 1;
}

int cmd_effect(const cli::Args& args) {
    if (args.positionals.empty()) {
        std::cerr << "error: effect needs script text\n";
        return 2;
    }
    sim::Simulation s;
    if (!boot(args, s)) return 2;
    s.run_months(std::stoi(args.get("after-months", "0")));
    const pdx::ScopeRef scope = s.parse_scope(args.get("scope", "global"));
    std::vector<std::string> lines;
    std::string error;
    if (!s.run_effect(scope, args.positionals[0], lines, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    for (const auto& l : lines) std::cout << l << "\n";
    print_refs(s, args);
    return 0;
}

// Runs a debug effect from _economy_sim_probes.txt in the simulator and returns its log lines (date prefix
// stripped by the extractor, which searches for the PREFIX| marker anywhere in the line).
bool run_probe_effect(sim::Simulation& s, const std::string& effect, std::vector<std::string>& lines) {
    std::string error;
    if (!s.interp().has_scripted_effect(pdx::intern(effect))) {
        std::cerr << "error: scripted effect '" << effect << "' not found (is toi/common/scripted_effects/"
                  << "_economy_sim_probes.txt present?)\n";
        return false;
    }
    const pdx::ScopeRef eng = s.parse_scope("ENG");
    if (!s.run_effect(eng.valid() ? eng : pdx::ScopeRef::global(), effect + " = yes", lines, error)) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    return true;
}

std::string game_log_path(const cli::Args& args, sim::Simulation& s) {
    if (!args.positionals.empty()) return args.positionals[0];
    return sim::default_game_log(s.mod_root());
}

int cmd_calibrate(const cli::Args& args) {
    sim::Simulation s;
    if (!boot(args, s)) return 2;
    std::string error;
    std::vector<std::string> game_lines, sim_lines;
    const std::string log_path = game_log_path(args, s);
    sim::KeyValues game, simv;
    if (!sim::read_lines(log_path, game_lines, error) ||
        !sim::extract_block(game_lines, "EOPROBE", std::stoi(args.get("dump", "1")), game, error)) {
        std::cerr << "error: " << error << "\n(run `effect eco_sim_probes = yes` in the in-game console first; log: "
                  << log_path << ")\n";
        return 2;
    }
    if (!run_probe_effect(s, "eco_sim_probes", sim_lines) || !sim::extract_block(sim_lines, "EOPROBE", 1, simv, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    std::cout << "game.log: " << log_path << "\n\n" << sim::calibration_report(game, simv);
    return 0;
}

int cmd_parity(const cli::Args& args) {
    sim::Simulation s;
    if (!boot(args, s)) return 2;
    s.run_months(std::stoi(args.get("months", "0")));
    std::string error;
    std::vector<std::string> game_lines, sim_lines;
    const std::string log_path = game_log_path(args, s);
    sim::KeyValues game, simv;
    if (!sim::read_lines(log_path, game_lines, error) ||
        !sim::extract_block(game_lines, "EOSIM", std::stoi(args.get("dump", "1")), game, error)) {
        std::cerr << "error: " << error << "\n(run `effect eco_sim_parity_dump = yes` in the in-game console first; log: "
                  << log_path << ")\n";
        return 2;
    }
    if (!run_probe_effect(s, "eco_sim_parity_dump", sim_lines) || !sim::extract_block(sim_lines, "EOSIM", 1, simv, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    const std::string report = sim::parity_report(game, simv, static_cast<size_t>(std::stoi(args.get("rows", "60"))));
    const std::string out = args.get("out");
    if (!out.empty()) {
        std::ofstream(out) << report;
        std::cout << "wrote " << out << "\n";
    } else {
        std::cout << "game.log: " << log_path << " | sim date " << s.world().date.to_string() << "\n\n" << report;
    }
    return 0;
}

int cmd_compare(const cli::Args& args) {
    if (args.positionals.size() != 2) {
        std::cerr << "usage: ecosim_cli compare <run dir A> <run dir B>\n";
        return 2;
    }
    std::string out, error;
    if (!sim::compare_runs(args.positionals[0], args.positionals[1], out, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }
    std::cout << out;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return print_help();
    }
    const std::string cmd = argv[1];
    const cli::Args args = cli::parse_args(argc, argv, 2, {"parse-only", "verbose", "log", "profile"});
    try {
        if (cmd == "run") return cmd_run(args);
        if (cmd == "effect") return cmd_effect(args);
        if (cmd == "check") return cmd_check(args);
        if (cmd == "test") return cmd_test(args);
        if (cmd == "compare") return cmd_compare(args);
        if (cmd == "calibrate") return cmd_calibrate(args);
        if (cmd == "parity") return cmd_parity(args);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 3;
    }
    if (cmd == "version") {
        std::cout << ecosim::kVersion << "\n";
        return 0;
    }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        return print_help();
    }
    std::cerr << "unknown command: " << cmd << "\n";
    print_help();
    return 2;
}
