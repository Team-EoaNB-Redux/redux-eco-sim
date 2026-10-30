#include "sim/diagnostics.h"

#include <algorithm>
#include <sstream>

namespace sim {

const char* diag_kind_name(DiagKind k) {
    switch (k) {
    case DiagKind::ParseError: return "parse_error";
    case DiagKind::UnknownEffect: return "unknown_effect";
    case DiagKind::UnknownTrigger: return "unknown_trigger";
    case DiagKind::UnknownScriptedEffect: return "unknown_scripted_effect";
    case DiagKind::UnsupportedBuiltin: return "unsupported_builtin";
    case DiagKind::BadParameter: return "bad_parameter";
    case DiagKind::Overflow: return "overflow";
    case DiagKind::DivByZero: return "div_by_zero";
    case DiagKind::LoopCap: return "loop_cap";
    case DiagKind::RecursionLimit: return "recursion_limit";
    case DiagKind::InvalidScope: return "invalid_scope";
    case DiagKind::ScopeUnset: return "scope_unset";
    case DiagKind::OutOfSubset: return "out_of_subset";
    case DiagKind::TempWithScope: return "temp_with_scope";
    case DiagKind::TempShadowsVar: return "temp_shadows_var";
    case DiagKind::TokenExpected: return "token_expected";
    case DiagKind::UnsetTempRead: return "unset_temp_read";
    case DiagKind::MathExprError: return "math_expr_error";
    case DiagKind::MetaEffectError: return "meta_effect_error";
    case DiagKind::UndefinedModifier: return "undefined_modifier";
    case DiagKind::AmbiguousAtTarget: return "ambiguous_at_target";
    case DiagKind::UnknownGameVar: return "unknown_game_var";
    case DiagKind::EventNotLoaded: return "event_not_loaded";
    case DiagKind::ArrayIndex: return "array_index";
    case DiagKind::ArrayGhostWrite: return "array_ghost_write";
    case DiagKind::NonNumberArithmetic: return "non_number_arithmetic";
    case DiagKind::ScriptLog: return "script_log";
    }
    return "unknown";
}

Severity diag_default_severity(DiagKind k) {
    switch (k) {
    case DiagKind::ParseError:
    case DiagKind::UnknownEffect:
    case DiagKind::UnknownTrigger:
    case DiagKind::UnsupportedBuiltin:
    case DiagKind::MathExprError:
    case DiagKind::MetaEffectError:
    case DiagKind::RecursionLimit:
        return Severity::Error;
    case DiagKind::UnknownScriptedEffect:
    case DiagKind::BadParameter:
    case DiagKind::Overflow:
    case DiagKind::DivByZero:
    case DiagKind::LoopCap:
    case DiagKind::InvalidScope:
    case DiagKind::TokenExpected:
    case DiagKind::UnknownGameVar:
    case DiagKind::EventNotLoaded:
    case DiagKind::ArrayIndex:
    case DiagKind::NonNumberArithmetic:
        return Severity::Warning;
    case DiagKind::UnsetTempRead:
    case DiagKind::ScopeUnset:
    case DiagKind::TempWithScope:
    case DiagKind::TempShadowsVar:
    case DiagKind::ArrayGhostWrite:
    case DiagKind::UndefinedModifier:
    case DiagKind::AmbiguousAtTarget:
        return Severity::Lint;
    case DiagKind::ScriptLog:
    default:
        return Severity::Info;
    }
}

void Diagnostics::report(DiagKind kind, const std::string& file, uint32_t line, const std::string& message,
                         const std::string& date) {
    auto key = std::make_tuple(static_cast<uint8_t>(kind), file, line, message);
    auto it = index_.find(key);
    if (it != index_.end()) {
        ++items_[it->second].count;
        return;
    }
    Diagnostic d;
    d.kind = kind;
    d.severity = diag_default_severity(kind);
    d.file = file;
    d.line = line;
    d.message = message;
    d.count = 1;
    d.first_date = date;
    index_.emplace(std::move(key), items_.size());
    items_.push_back(std::move(d));
}

size_t Diagnostics::count(DiagKind k) const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(), [k](const Diagnostic& d) { return d.kind == k; }));
}

size_t Diagnostics::count_at_least(Severity s) const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(), [s](const Diagnostic& d) { return d.severity >= s; }));
}

std::string Diagnostics::format(size_t max_per_kind) const {
    std::vector<const Diagnostic*> sorted;
    sorted.reserve(items_.size());
    for (const auto& d : items_) {
        sorted.push_back(&d);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const Diagnostic* a, const Diagnostic* b) {
        if (a->severity != b->severity) return a->severity > b->severity;
        if (a->kind != b->kind) return a->kind < b->kind;
        if (a->file != b->file) return a->file < b->file;
        return a->line < b->line;
    });
    static const char* sev_names[] = {"info", "lint", "warning", "error"};
    std::ostringstream os;
    DiagKind cur = DiagKind::ScriptLog;
    bool first = true;
    size_t shown = 0, hidden = 0;
    auto flush_hidden = [&]() {
        if (hidden > 0) {
            os << "    ... " << hidden << " more\n";
        }
    };
    for (const Diagnostic* d : sorted) {
        if (first || d->kind != cur) {
            flush_hidden();
            cur = d->kind;
            first = false;
            shown = hidden = 0;
            os << "[" << sev_names[static_cast<int>(d->severity)] << "] " << diag_kind_name(d->kind) << " ("
               << count(d->kind) << " sites)\n";
        }
        if (shown < max_per_kind) {
            os << "    " << d->file << ":" << d->line << ": " << d->message;
            if (d->count > 1) {
                os << "  (x" << d->count << ")";
            }
            os << "\n";
            ++shown;
        } else {
            ++hidden;
        }
    }
    flush_hidden();
    return os.str();
}

} // namespace sim
