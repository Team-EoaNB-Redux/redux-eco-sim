#pragma once

// Structured diagnostics — the core value of the simulator for an AI: everything the game hides
// (silent 0 reads, failed math expressions, overflow wraps, unknown effects) is reported with file:line.
// Reports are deduplicated per (kind, file, line, detail) with a hit count and the first date seen.

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace sim {

enum class Severity : uint8_t { Info, Lint, Warning, Error };

enum class DiagKind : uint8_t {
    ParseError,
    UnknownEffect,          // key in effect position resolves to nothing
    UnknownTrigger,         // key in trigger position resolves to nothing (evaluates false)
    UnknownScriptedEffect,  // `foo = yes` where foo looks like a call but is undefined
    UnsupportedBuiltin,     // a real HOI4 builtin the simulator does not implement yet
    BadParameter,           // malformed builtin usage
    Overflow,               // value left the +/-2^31 range (wrapped per semantics)
    DivByZero,
    LoopCap,                // while/for loop hit MAX_EFFECT_ITERATION
    RecursionLimit,
    InvalidScope,           // scope change to a nonexistent / non-scope value
    ScopeUnset,             // lint: scope variable is unset/0 (in-game: reads 0 / block skipped)
    OutOfSubset,            // info: a state/country outside the scenario subset was referenced
    TempWithScope,          // lint: *_temp_* effect on a scoped/global name (the scope wins in-game)
    TempShadowsVar,         // lint: a bare name read while BOTH a temp and a scope variable of that name exist
    TokenExpected,          // @var: target holds a plain number where a token was expected
    UnsetTempRead,          // lint: temp read before being set in this execution
    MathExprError,          // in-game this silently evaluates to 0
    MetaEffectError,
    UndefinedModifier,      // modifier@x for an x no definition provides
    AmbiguousAtTarget,      // x@bare_identifier
    UnknownGameVar,
    EventNotLoaded,
    ArrayIndex,             // out-of-range array access
    ArrayGhostWrite,        // lint: arr^i written with i >= ^num (kept, like HOI4's arr^i variables)
    NonNumberArithmetic,    // arithmetic on a token/scope value
    ScriptLog,              // not a problem: `log` output is kept separately; reserved
};

const char* diag_kind_name(DiagKind k);
Severity diag_default_severity(DiagKind k);

struct Diagnostic {
    DiagKind kind = DiagKind::UnknownEffect;
    Severity severity = Severity::Warning;
    std::string file; // mod-relative path
    uint32_t line = 0;
    std::string message;
    uint64_t count = 0;
    std::string first_date;
};

class Diagnostics {
public:
    void report(DiagKind kind, const std::string& file, uint32_t line, const std::string& message,
                const std::string& date = {});
    const std::vector<Diagnostic>& all() const { return items_; }
    size_t count(DiagKind k) const;
    size_t count_at_least(Severity s) const;
    void clear() {
        items_.clear();
        index_.clear();
    }
    // Human-readable dump, grouped by kind, most severe first.
    std::string format(size_t max_per_kind = 50) const;

private:
    std::vector<Diagnostic> items_;
    std::map<std::tuple<uint8_t, std::string, uint32_t, std::string>, size_t> index_;
};

} // namespace sim
