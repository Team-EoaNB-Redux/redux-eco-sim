#pragma once

// Paradox value-accumulator math expressions:
//   { value = x  add = 5  multiply = { value = y root = 2 }  clamp = { min = 0 max = 1 }  round = yes
//     if = { limit = { value = x greater_than = 10 } add = 100 } else = { subtract = 1 } }
// Functions apply in written order (no precedence). Booleans: 0 = false, anything else true; comparisons
// yield 1/0. In-game, an expression that fails to parse evaluates to 0 silently — the simulator does the
// same AND reports a math_expr_error diagnostic.
// pow/root/log/trig are numerical approximations in-game too; integer `pow` is exact repeated
// multiplication (with the usual fixed-point truncation at each step).

#include "pdx/ast.h"
#include "pdx/fixed.h"

#include <memory>
#include <vector>

namespace pdx {

class Interp;
struct Ctx;
struct Operand;

struct MathOp;

struct MathExpr {
    bool failed = false;
    SrcLoc loc;
    std::vector<MathOp> ops;
};

std::shared_ptr<MathExpr> compile_math(Interp& in, const ScriptFile& f, const Node& block);
Fixed eval_math(Ctx& c, const MathExpr& e);

} // namespace pdx
