#include "sim/script_test.h"

#include "pdx/interp.h"
#include "pdx/parser.h"
#include "sim/diagnostics.h"

#include <fstream>
#include <sstream>

namespace sim {

namespace {

struct Expectations {
    std::vector<std::string> logs;
    std::vector<std::string> diags;
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

void build_test_world(world::World& w) {
    const int32_t tst = w.ensure_country(pdx::intern("TST"));
    const int32_t oth = w.ensure_country(pdx::intern("OTH"));
    for (int32_t id : {1, 2, 3}) {
        world::State& s = w.ensure_state(id);
        s.name = pdx::intern("STATE_" + std::to_string(id));
        s.owner = s.controller = id == 3 ? oth : tst;
        s.cores = {s.owner};
        s.manpower = pdx::Fixed::from_int(100000 * id);
    }
    w.country(tst)->capital = 1;
    w.country(oth)->capital = 3;
    w.rebuild_state_lists();
}

} // namespace

std::vector<ScriptTestResult> run_script_test_file(const std::string& path) {
    std::vector<ScriptTestResult> results;
    std::ifstream in(path, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    // Expectation lines by source line number.
    std::vector<std::pair<uint32_t, std::string>> expect_lines;
    {
        std::istringstream ls(text);
        std::string line;
        uint32_t n = 0;
        while (std::getline(ls, line)) {
            ++n;
            const size_t hash = line.find('#');
            if (hash == std::string::npos) continue;
            const std::string c = trim(line.substr(hash + 1));
            if (c.rfind("expect:", 0) == 0 || c.rfind("expect-diag:", 0) == 0) expect_lines.emplace_back(n, c);
        }
    }

    pdx::ScriptFile probe;
    pdx::parse_text(text, probe);
    if (!probe.errors.empty()) {
        ScriptTestResult r;
        r.file = path;
        r.name = "<parse>";
        r.pass = false;
        r.messages.push_back("parse error line " + std::to_string(probe.errors[0].line) + ": " + probe.errors[0].message);
        results.push_back(r);
        return results;
    }

    // Line ranges of top-level blocks.
    std::vector<std::pair<std::string, uint32_t>> tests;
    for (const pdx::Node* n = probe.root_begin(); n != probe.root_end(); ++n) {
        const std::string key = pdx::sym_str(n->key);
        if (key.rfind("test_", 0) == 0) tests.emplace_back(key, n->line);
    }

    for (size_t ti = 0; ti < tests.size(); ++ti) {
        ScriptTestResult r;
        r.file = path;
        r.name = tests[ti].first;
        const uint32_t start = tests[ti].second;
        uint32_t end = UINT32_MAX;
        for (const pdx::Node* n = probe.root_begin(); n != probe.root_end(); ++n) {
            if (n->line > start && n->line < end) end = n->line;
        }
        Expectations ex;
        for (const auto& [line, c] : expect_lines) {
            if (line <= start || line >= end) continue;
            if (c.rfind("expect-diag:", 0) == 0) ex.diags.push_back(trim(c.substr(12)));
            else ex.logs.push_back(trim(c.substr(7)));
        }

        // Fresh world + interpreter per test.
        world::World w;
        build_test_world(w);
        Diagnostics diags;
        pdx::Interp interp(w, diags);
        std::vector<std::string> logs;
        interp.log_sink = [&](const std::string& l) { logs.push_back(l); };

        pdx::ScriptFile f;
        f.path = path;
        pdx::parse_text(text, f);
        const pdx::ScriptFile* sf = interp.add_file(std::move(f));
        interp.index_scripted_effects(*sf);
        static const pdx::Sym s_triggers = pdx::intern("scripted_triggers");
        for (const pdx::Node* n = sf->root_begin(); n != sf->root_end(); ++n) {
            if (n->key != s_triggers || !n->is_block) continue;
            // Index the block's children as scripted triggers via a file alias whose root is that block.
            pdx::ScriptFile alias;
            alias.path = sf->path;
            alias.nodes = sf->nodes;
            alias.root_first = n->first_child;
            alias.root_count = n->child_count;
            interp.index_scripted_triggers(*interp.add_file(std::move(alias)));
        }

        pdx::ScriptedDef* def = interp.scripted_effect(pdx::intern(r.name));
        const pdx::EffectBlock block = interp.compile_effects(*def->file, *def->node);
        interp.run(block, pdx::ScopeRef::country(0));

        if (logs.size() != ex.logs.size()) {
            r.pass = false;
            r.messages.push_back("expected " + std::to_string(ex.logs.size()) + " log lines, got " + std::to_string(logs.size()));
        }
        for (size_t i = 0; i < std::max(logs.size(), ex.logs.size()); ++i) {
            const std::string got = i < logs.size() ? logs[i] : "<missing>";
            const std::string want = i < ex.logs.size() ? ex.logs[i] : "<none>";
            if (got != want) {
                r.pass = false;
                r.messages.push_back("log[" + std::to_string(i) + "]: expected \"" + want + "\", got \"" + got + "\"");
            }
        }
        for (const auto& want : ex.diags) {
            bool found = false;
            for (const auto& d : diags.all()) found |= want == diag_kind_name(d.kind);
            if (!found) {
                r.pass = false;
                r.messages.push_back("expected a '" + want + "' diagnostic");
            }
        }
        // Unexpected error-severity diagnostics fail the test unless declared.
        for (const auto& d : diags.all()) {
            if (d.severity < Severity::Warning) continue;
            bool declared = false;
            for (const auto& want : ex.diags) declared |= want == diag_kind_name(d.kind);
            if (!declared) {
                r.pass = false;
                r.messages.push_back(std::string("unexpected ") + diag_kind_name(d.kind) + " at line " +
                                     std::to_string(d.line) + ": " + d.message);
            }
        }
        results.push_back(std::move(r));
    }
    return results;
}

} // namespace sim
