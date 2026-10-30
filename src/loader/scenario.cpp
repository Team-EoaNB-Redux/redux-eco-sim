#include "loader/scenario.h"

#include "pdx/fixed.h"
#include "pdx/parser.h"

#include <cstdlib>
#include <filesystem>

namespace loader {

using pdx::Node;
using pdx::ScriptFile;
using pdx::sym_str;
using pdx::sym_view;

namespace {

bool yes(const Node& n) { return sym_view(n.value) == "yes"; }

std::vector<std::string> strings(const ScriptFile& f, const Node& n) {
    std::vector<std::string> out;
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (c->key == pdx::kNoSym) out.push_back(sym_str(c->value));
    }
    return out;
}

bool apply_semantics(const ScriptFile& f, const Node& block, pdx::Semantics& s, std::string& error) {
    for (const Node* c = f.begin(block); c != f.end(block); ++c) {
        const std::string_view k = sym_view(c->key), v = sym_view(c->value);
        if (k == "overflow") {
            s.overflow = v == "saturate" ? pdx::OverflowMode::Saturate : v == "none" ? pdx::OverflowMode::None : pdx::OverflowMode::Wrap32;
        } else if (k == "mul_rounding" || k == "div_rounding") {
            const auto r = v == "half_away" ? pdx::Rounding::HalfAwayFromZero : pdx::Rounding::Truncate;
            (k == "mul_rounding" ? s.mul_rounding : s.div_rounding) = r;
        } else if (k == "div_by_zero") {
            s.div_by_zero = v == "zero" ? pdx::DivByZero::Zero : v == "saturate" ? pdx::DivByZero::Saturate : pdx::DivByZero::KeepTarget;
        } else if (k == "temp_precedence") {
            s.temp_precedence = v == "scope_first" ? pdx::TempPrecedence::ScopeFirst : pdx::TempPrecedence::TempFirst;
        } else if (k == "bare_at_target") {
            s.bare_at_target = v == "variable" ? pdx::BareAtTarget::Variable : v == "literal" ? pdx::BareAtTarget::Literal : pdx::BareAtTarget::TokenIfKnown;
        } else if (k == "check_shorthand_eq_is_equals") {
            s.check_shorthand_eq_is_equals = yes(*c);
        } else if (k == "check_shorthand_allows_le_ge") {
            s.check_shorthand_allows_le_ge = yes(*c);
        } else if (k == "monthly_before_weekly") {
            s.monthly_before_weekly = yes(*c);
        } else if (k == "max_effect_iterations" || k == "text_default_decimals" || k == "max_call_depth") {
            pdx::Fixed n;
            pdx::Fixed::parse(v, n);
            const int iv = static_cast<int>(n.whole_trunc());
            if (k == "max_effect_iterations") s.max_effect_iterations = iv;
            else if (k == "text_default_decimals") s.text_default_decimals = iv;
            else s.max_call_depth = iv;
        } else {
            error = "unknown semantics key '" + std::string(k) + "'";
            return false;
        }
    }
    return true;
}

void scoped_values(const ScriptFile& f, const Node& block, std::vector<Scenario::ScopedValues>& out) {
    for (const Node* t = f.begin(block); t != f.end(block); ++t) {
        if (!t->is_block) continue;
        Scenario::ScopedValues sv;
        sv.tag = t->key;
        for (const Node* c = f.begin(*t); c != f.end(*t); ++c) {
            if (c->key != pdx::kNoSym && !c->is_block) sv.values.emplace_back(c->key, sym_str(c->value));
        }
        out.push_back(std::move(sv));
    }
}

} // namespace

bool apply_semantics_text(const std::string& text, pdx::Semantics& s, std::string& error) {
    ScriptFile f;
    pdx::parse_text("semantics = { " + text + " }", f);
    if (!f.errors.empty() || f.root_count != 1) {
        error = "bad semantics text: " + text;
        return false;
    }
    return apply_semantics(f, *f.root_begin(), s, error);
}

bool load_scenario(const std::string& path, Scenario& out, std::string& error) {
    ScriptFile f;
    if (!pdx::parse_file(path, f)) {
        error = "cannot read scenario " + path;
        return false;
    }
    if (!f.errors.empty()) {
        error = path + ":" + std::to_string(f.errors[0].line) + ": " + f.errors[0].message;
        return false;
    }
    const Node* root = nullptr;
    for (const Node* n = f.root_begin(); n != f.root_end(); ++n) {
        if (sym_view(n->key) == "scenario" && n->is_block) root = n;
    }
    if (!root) {
        error = path + ": no `scenario = { }` block";
        return false;
    }
    out.path = path;
    for (const Node* c = f.begin(*root); c != f.end(*root); ++c) {
        const std::string_view k = sym_view(c->key);
        if (k == "name") out.name = sym_str(c->value);
        else if (k == "start_date") {
            if (!world::Date::parse(sym_view(c->value), out.start_date)) {
                error = "bad start_date";
                return false;
            }
        } else if (k == "seed") {
            pdx::Fixed s;
            pdx::Fixed::parse(sym_view(c->value), s);
            out.seed = static_cast<uint64_t>(s.whole_trunc());
        } else if (k == "countries") {
            for (const auto& t : strings(f, *c)) {
                if (t == "ALL") out.all_countries = true;
                else out.countries.push_back(pdx::intern(t));
            }
        } else if (k == "include_subjects") out.include_subjects = yes(*c);
        else if (k == "include_market_members") out.include_market_members = yes(*c);
        else if (k == "players") {
            for (const auto& t : strings(f, *c)) out.players.push_back(pdx::intern(t));
        } else if (k == "player_event_choice") out.player_first_option = sym_view(c->value) == "first_option";
        else if (k == "on_actions") out.on_action_files = strings(f, *c);
        else if (k == "events") out.event_files = strings(f, *c);
        else if (k == "skip_effects") {
            for (const auto& t : strings(f, *c)) out.skip_effects.push_back(pdx::intern(t));
        } else if (k == "overrides" || k == "pins") {
            auto& dst = k == "pins" ? out.pins : out.overrides;
            for (const Node* o = f.begin(*c); o != f.end(*c); ++o) {
                if (o->key != pdx::kNoSym && !o->is_block) dst.emplace_back(sym_str(o->key), sym_str(o->value));
            }
        } else if (k == "history") {
            std::filesystem::path h(sym_str(c->value));
            if (h.is_relative()) h = std::filesystem::path(path).parent_path() / h;
            out.history_dir = h.lexically_normal().string();
            if (!std::filesystem::is_directory(out.history_dir)) {
                error = path + ": history folder not found: " + out.history_dir;
                return false;
            }
        } else if (k == "states") {
            for (const auto& t : strings(f, *c)) out.states.push_back(std::atoi(t.c_str()));
        } else if (k == "state_owner") out.state_owner = c->value;
        else if (k == "engine") scoped_values(f, *c, out.engine);
        else if (k == "units") scoped_values(f, *c, out.units);
        else if (k == "semantics") {
            if (!apply_semantics(f, *c, out.semantics, error)) return false;
        } else {
            error = path + ":" + std::to_string(c->line) + ": unknown scenario key '" + std::string(k) + "'";
            return false;
        }
    }
    if (out.on_action_files.empty()) out.on_action_files = {"_eoanbr_constants_on_startup.txt", "_eoanbr_eco.txt"};
    return true;
}

} // namespace loader
