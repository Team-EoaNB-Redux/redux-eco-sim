#pragma once

// Variable-reference grammar, parsed once per script site:
//
//   [step (':' | '.')]* ['global.'] name ['@' target] ['^' index] ['?' default]
//
//   step   : THIS | ROOT | FROM | PREV (repeatable: PREV.PREV) | controller | owner | capital | overlord
//            | TAG | <state id> | var:<name> | <name>            (a bare name = variable holding a scope)
//   target : token:<name> | var:<ref> | THIS | ROOT | PREV | FROM | TAG | <number> | <bare identifier>
//   index  : num | <integer> | <ref>
//
// Examples from the mod: `global.base_price@var:r_token`, `controller:pm_input_amount@var:pm_temp^num`,
// `market_leader_temp:supply@var:r_type`, `PREV.investment_weight@THIS`, `gdp_amount_array^num_temp`.

#include "pdx/fixed.h"
#include "pdx/symbols.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pdx {

struct VarRef;

enum class StepKind : uint8_t { This, Root, Prev, From, Controller, Owner, Capital, Overlord, Tag, StateId, Named };

struct Step {
    StepKind kind = StepKind::This;
    uint8_t prev_depth = 1; // PREV.PREV collapses into one step with depth 2
    Sym name = kNoSym;      // Tag / Named
    int32_t number = 0;     // StateId
};

enum class TargetKind : uint8_t { None, Token, Var, Scope, Bare, Number };

struct Target {
    TargetKind kind = TargetKind::None;
    Sym sym = kNoSym;            // Token / Bare
    Step step;                   // Scope
    std::shared_ptr<VarRef> var; // Var
    int64_t number = 0;          // Number
};

enum class IndexKind : uint8_t { None, Num, Int, Var };

struct Index {
    IndexKind kind = IndexKind::None;
    int64_t i = 0;
    std::shared_ptr<VarRef> var;
};

struct VarRef {
    std::vector<Step> path;
    bool global = false;
    Sym name = kNoSym;
    Target target;
    Index index;
    bool has_default = false;
    Fixed default_value;
    std::string text; // original text, for diagnostics

    bool is_plain_name() const { return path.empty() && !global; }
};

// Returns false (and an error message) if the text is not a syntactically valid reference.
bool parse_varref(std::string_view text, VarRef& out, std::string* error = nullptr);

// True for 3-character country-tag-shaped words (ENG, D01, UCT). The caller confirms against the tag table.
bool looks_like_tag(std::string_view w);

} // namespace pdx
