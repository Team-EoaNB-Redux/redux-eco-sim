#include "sim/report.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace sim {

namespace fs = std::filesystem;

namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        o.push_back(c);
    }
    return o;
}

std::string fmt(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(std::abs(v) >= 1000 ? 1 : 3) << v;
    return os.str();
}

bool is_eco_file(const std::string& path) { return path.find("_economy_") != std::string::npos; }

const char* severity_name(Severity s) {
    switch (s) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Lint: return "lint";
    case Severity::Info:
    default: return "info";
    }
}

void write_diag_section(std::ostream& md, const std::vector<const Diagnostic*>& list, size_t per_kind) {
    if (list.empty()) {
        md << "_none_\n\n";
        return;
    }
    std::map<std::pair<int, int>, std::vector<const Diagnostic*>> by_kind; // (-severity, kind)
    for (const Diagnostic* d : list) by_kind[{-static_cast<int>(d->severity), static_cast<int>(d->kind)}].push_back(d);
    for (auto& [k, items] : by_kind) {
        uint64_t hits = 0;
        for (const Diagnostic* d : items) hits += d->count;
        md << "#### " << severity_name(items.front()->severity) << " · `" << diag_kind_name(items.front()->kind) << "` — "
           << items.size() << " sites, " << hits << " hits\n\n";
        std::stable_sort(items.begin(), items.end(), [](const Diagnostic* a, const Diagnostic* b) { return a->count > b->count; });
        for (size_t i = 0; i < std::min(per_kind, items.size()); ++i) {
            const Diagnostic* d = items[i];
            md << "- `" << d->file << ":" << d->line << "` " << d->message;
            if (d->count > 1) md << " (×" << d->count << ")";
            md << "\n";
        }
        if (items.size() > per_kind) md << "- … " << (items.size() - per_kind) << " more (see diagnostics.json)\n";
        md << "\n";
    }
}

bool read_csv(const std::string& path, std::vector<std::string>& header, std::vector<std::vector<std::string>>& rows) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) cols.push_back(c);
        if (first) {
            header = cols;
            first = false;
        } else {
            rows.push_back(cols);
        }
    }
    return true;
}

} // namespace

bool Recorder::add_watch_file(const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot read watch file " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line.erase(0, line.find_first_not_of(" \t\r"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if (!line.empty()) watches_.push_back(line);
    }
    return true;
}

void Recorder::sample() {
    dates_.push_back(sim_.world().date.to_string());
    std::vector<double> row;
    std::vector<std::string> exact;
    for (const auto& w : watches_) {
        pdx::Value v;
        std::string err;
        if (sim_.eval(w, v, err)) {
            row.push_back(v.num.to_double());
            exact.push_back(v.num.to_string_exact());
        } else {
            row.push_back(std::nan(""));
            exact.push_back("ERR");
        }
    }
    rows_.push_back(std::move(row));
    exact_.push_back(std::move(exact));
}

bool write_run_outputs(const std::string& dir, Simulation& s, const Recorder& r, int months, std::string& error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "cannot create " + dir + ": " + ec.message();
        return false;
    }

    // ---- series.csv ----
    {
        std::ofstream csv(fs::path(dir) / "series.csv");
        csv << "date";
        for (const auto& w : r.watches_) csv << "," << w;
        csv << "\n";
        for (size_t i = 0; i < r.dates_.size(); ++i) {
            csv << r.dates_[i];
            for (const auto& v : r.exact_[i]) csv << "," << v;
            csv << "\n";
        }
    }

    // ---- sim.log ----
    {
        std::ofstream log(fs::path(dir) / "sim.log");
        for (const auto& l : s.log()) log << l << "\n";
    }

    // ---- diagnostics.json ----
    const auto& diags = s.diagnostics().all();
    {
        std::ofstream js(fs::path(dir) / "diagnostics.json");
        js << "[\n";
        for (size_t i = 0; i < diags.size(); ++i) {
            const auto& d = diags[i];
            js << "  {\"kind\":\"" << diag_kind_name(d.kind) << "\",\"severity\":\"" << severity_name(d.severity)
               << "\",\"file\":\"" << json_escape(d.file) << "\",\"line\":" << d.line << ",\"count\":" << d.count
               << ",\"first_date\":\"" << d.first_date << "\",\"message\":\"" << json_escape(d.message) << "\"}"
               << (i + 1 < diags.size() ? "," : "") << "\n";
        }
        js << "]\n";
    }

    // ---- report.md ----
    std::ofstream md(fs::path(dir) / "report.md");
    const auto& sc = s.scenario();
    const auto& sem = s.interp().semantics();
    md << "# Eco sim run — " << sc.name << "\n\n"
       << "- scenario: `" << sc.path << "` · seed " << sc.seed << " · " << sc.start_date.to_string() << " → "
       << s.world().date.to_string() << " (" << months << " months)\n"
       << "- world: " << s.world().existing_countries().size() << " countries, " << s.world().existing_states().size()
       << " states\n"
       << "- time: load " << fmt(s.load_seconds) << " s · startup " << fmt(s.startup_seconds) << " s · ticks "
       << fmt(s.tick_seconds) << " s (" << fmt(months > 0 ? s.tick_seconds / months : 0) << " s/month)\n"
       << "- semantics: overflow=" << (sem.overflow == pdx::OverflowMode::Wrap32 ? "wrap32" : sem.overflow == pdx::OverflowMode::Saturate ? "saturate" : "none")
       << " · div_by_zero=" << (sem.div_by_zero == pdx::DivByZero::KeepTarget ? "keep_target" : sem.div_by_zero == pdx::DivByZero::Zero ? "zero" : "saturate")
       << " · mul/div rounding=" << (sem.mul_rounding == pdx::Rounding::Truncate ? "truncate" : "half_away")
       << " · check `x = n` means " << (sem.check_shorthand_eq_is_equals ? "equals" : ">=")
       << " (unverified knobs: see docs/SEMANTICS.md)\n\n";

    // Watches
    md << "## Watches\n\n";
    if (r.watches_.empty()) {
        md << "_no watches (use --watch <ref> or --watch-file watches/core.txt)_\n\n";
    } else {
        md << "| ref | start | end | min | max | change |\n|---|---:|---:|---:|---:|---:|\n";
        for (size_t c = 0; c < r.watches_.size(); ++c) {
            double lo = INFINITY, hi = -INFINITY;
            bool bad = false;
            for (const auto& row : r.rows_) {
                if (std::isnan(row[c])) {
                    bad = true;
                    continue;
                }
                lo = std::min(lo, row[c]);
                hi = std::max(hi, row[c]);
            }
            const double a = r.rows_.empty() ? 0 : r.rows_.front()[c];
            const double b = r.rows_.empty() ? 0 : r.rows_.back()[c];
            std::string change = "—";
            if (std::abs(a) > 1e-9) change = fmt((b - a) / std::abs(a) * 100.0) + "%";
            md << "| `" << r.watches_[c] << "` | " << fmt(a) << " | " << fmt(b) << " | " << fmt(lo) << " | " << fmt(hi)
               << " | " << change << (bad ? " ⚠ eval error" : "") << " |\n";
        }
        md << "\n";
    }

    // Diagnostics
    std::vector<const Diagnostic*> eco, other;
    for (const auto& d : diags) {
        if (d.severity == Severity::Info) continue;
        (is_eco_file(d.file) ? eco : other).push_back(&d);
    }
    md << "## Diagnostics in economy files (`_economy_*`)\n\n";
    write_diag_section(md, eco, 25);
    md << "## Diagnostics elsewhere (non-eco script reached by the startup/tick)\n\n";
    write_diag_section(md, other, 8);
    size_t info = 0;
    for (const auto& d : diags) info += d.severity == Severity::Info;
    if (info) md << "_" << info << " info-level sites omitted (e.g. out_of_subset references) — see diagnostics.json._\n\n";

    // Profile
    md << "## Profile (scripted effects, inclusive time)\n\n";
    std::vector<std::pair<pdx::Sym, pdx::Interp::CallStats>> prof(s.interp().profile.begin(), s.interp().profile.end());
    if (prof.empty()) {
        md << "_profiling disabled (run with --profile)_\n\n";
    } else {
        std::sort(prof.begin(), prof.end(), [](const auto& a, const auto& b) { return a.second.inclusive_seconds > b.second.inclusive_seconds; });
        md << "| scripted effect | calls | inclusive s | ms/call |\n|---|---:|---:|---:|\n";
        for (size_t i = 0; i < std::min<size_t>(25, prof.size()); ++i) {
            const auto& [name, st] = prof[i];
            md << "| `" << pdx::sym_str(name) << "` | " << st.calls << " | " << fmt(st.inclusive_seconds) << " | "
               << fmt(st.calls ? st.inclusive_seconds * 1000.0 / static_cast<double>(st.calls) : 0) << " |\n";
        }
        md << "\n";
    }

    // Builtin coverage on the executed path
    md << "## Builtins compiled with reduced fidelity\n\n";
    bool any = false;
    for (const auto& [name, count] : s.interp().builtin_uses) {
        const auto& reg = s.interp().registry();
        pdx::BuiltinStatus st = pdx::BuiltinStatus::Full;
        if (auto it = reg.effects.find(name); it != reg.effects.end()) st = it->second.status;
        else if (auto it2 = reg.triggers.find(name); it2 != reg.triggers.end()) st = it2->second.status;
        else if (auto it3 = reg.game_vars.find(name); it3 != reg.game_vars.end()) st = it3->second.status;
        if (st == pdx::BuiltinStatus::Full) continue;
        md << "- `" << pdx::sym_str(name) << "` — " << pdx::builtin_status_name(st) << " (" << count << " sites)\n";
        any = true;
    }
    if (!any) md << "_none_\n";
    md << "\n## Known approximations\n\n"
       << "- Native resource ledger: no equipment production lines (`resource_consumed@r` = scenario engine value "
          "`resource_consumed_<r>`, default 0).\n"
       << "- No research progress unless the scenario grants techs; no wars, diplomacy or AI focus trees.\n"
       << "- Unmodelled engine values (manpower pools, divisions, doctrine levels) read the scenario `engine = { }` block (default 0).\n"
       << "- Only whitelisted on_actions/events run (scenario `on_actions` / `events`).\n";
    return true;
}

bool compare_runs(const std::string& dir_a, const std::string& dir_b, std::string& out, std::string& error) {
    std::vector<std::string> ha, hb;
    std::vector<std::vector<std::string>> ra, rb;
    if (!read_csv((fs::path(dir_a) / "series.csv").string(), ha, ra)) {
        error = "cannot read " + dir_a + "/series.csv";
        return false;
    }
    if (!read_csv((fs::path(dir_b) / "series.csv").string(), hb, rb)) {
        error = "cannot read " + dir_b + "/series.csv";
        return false;
    }
    std::ostringstream md;
    md << "| ref | A end | B end | Δ | Δ% | first divergence |\n|---|---:|---:|---:|---:|---|\n";
    for (size_t c = 1; c < ha.size(); ++c) {
        auto it = std::find(hb.begin(), hb.end(), ha[c]);
        if (it == hb.end()) continue;
        const size_t cb = static_cast<size_t>(it - hb.begin());
        std::string diverge = "never";
        for (size_t i = 0; i < std::min(ra.size(), rb.size()); ++i) {
            if (c < ra[i].size() && cb < rb[i].size() && ra[i][c] != rb[i][cb]) {
                diverge = ra[i][0];
                break;
            }
        }
        const double a = ra.empty() || c >= ra.back().size() ? 0 : std::atof(ra.back()[c].c_str());
        const double b = rb.empty() || cb >= rb.back().size() ? 0 : std::atof(rb.back()[cb].c_str());
        const std::string pct = std::abs(a) > 1e-9 ? fmt((b - a) / std::abs(a) * 100.0) + "%" : "—";
        md << "| `" << ha[c] << "` | " << fmt(a) << " | " << fmt(b) << " | " << fmt(b - a) << " | " << pct << " | " << diverge << " |\n";
    }
    out = md.str();
    return true;
}

} // namespace sim
