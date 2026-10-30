#include "pdx/mathexpr.h"

#include "pdx/interp.h"

#include <cmath>

namespace pdx {

using sim::DiagKind;

enum class MK : uint8_t {
    Value, Add, Sub, Mul, Div, Min, Max, Mod, Pow, Root, Log, Sin, Cos, Tan, Atan, Round, Clamp,
    Gt, Lt, Ge, Le, Eq, Ne, And, Or, Xor, Not, If
};

struct MathOp {
    MK k = MK::Value;
    SrcLoc loc;
    Operand arg;
    // clamp: written-order list of (is_min, bound)
    std::vector<std::pair<bool, Operand>> bounds;
    // if / else_if / else chain
    struct Branch {
        bool has_limit = false;
        std::shared_ptr<MathExpr> limit;
        std::vector<MathOp> body;
    };
    std::vector<Branch> branches;
};

namespace {

bool lookup(std::string_view key, MK& out) {
    struct E {
        const char* n;
        MK k;
    };
    static const E table[] = {
        {"value", MK::Value}, {"add", MK::Add}, {"subtract", MK::Sub}, {"multiply", MK::Mul}, {"divide", MK::Div},
        {"min", MK::Min}, {"max", MK::Max}, {"mod", MK::Mod}, {"modulo", MK::Mod}, {"pow", MK::Pow},
        {"root", MK::Root}, {"log", MK::Log}, {"sin", MK::Sin}, {"cos", MK::Cos}, {"tan", MK::Tan},
        {"atan", MK::Atan}, {"round", MK::Round}, {"clamp", MK::Clamp}, {"greater_than", MK::Gt},
        {"less_than", MK::Lt}, {"greater_than_or_equals", MK::Ge}, {"less_than_or_equals", MK::Le},
        {"equals", MK::Eq}, {"not_equals", MK::Ne}, {"and", MK::And}, {"or", MK::Or}, {"xor", MK::Xor},
        {"not", MK::Not}, {"if", MK::If},
    };
    for (const auto& e : table) {
        if (key == e.n) {
            out = e.k;
            return true;
        }
    }
    return false;
}

bool compile_ops(Interp& in, const ScriptFile& f, const Node& block, std::vector<MathOp>& out, MathExpr& root);

bool compile_one(Interp& in, const ScriptFile& f, const Node* n, const Node* end, const Node*& consumed,
                 std::vector<MathOp>& out, MathExpr& root) {
    static const Sym s_else_if = intern("else_if"), s_else = intern("else"), s_limit = intern("limit"),
                     s_min = intern("min"), s_max = intern("max");
    const SrcLoc l = in.loc(f, *n);
    MK k;
    if (!lookup(sym_view(n->key), k)) {
        in.diag(DiagKind::MathExprError, l,
                "unknown math-expression function '" + sym_str(n->key) + "' (the whole expression evaluates to 0 in-game)");
        return false;
    }
    MathOp op;
    op.k = k;
    op.loc = l;
    switch (k) {
    case MK::Round:
    case MK::Sin:
    case MK::Cos:
    case MK::Tan:
    case MK::Atan:
    case MK::Not:
        break; // `= yes`
    case MK::Clamp:
        if (!n->is_block) {
            in.diag(DiagKind::MathExprError, l, "clamp needs { min = .. max = .. }");
            return false;
        }
        for (const Node* b = f.begin(*n); b != f.end(*n); ++b) {
            if (b->key != s_min && b->key != s_max) {
                in.diag(DiagKind::MathExprError, in.loc(f, *b), "clamp accepts only min/max");
                return false;
            }
            Operand bound;
            if (!in.compile_operand(f, *b, bound)) return false;
            op.bounds.emplace_back(b->key == s_min, std::move(bound));
        }
        break;
    case MK::If: {
        const Node* cur = n;
        for (;;) {
            MathOp::Branch br;
            br.has_limit = cur->key != s_else;
            if (cur->is_block) {
                for (const Node* c = f.begin(*cur); c != f.end(*cur); ++c) {
                    if (c->key == s_limit && br.has_limit) {
                        br.limit = compile_math(in, f, *c);
                        if (br.limit->failed) return false;
                        continue;
                    }
                    const Node* cons = c;
                    if (!compile_one(in, f, c, f.end(*cur), cons, br.body, root)) return false;
                    c = cons;
                }
            }
            op.branches.push_back(std::move(br));
            const Node* next = cur + 1;
            if (next < end && (next->key == s_else_if || next->key == s_else) && op.branches.back().has_limit) {
                cur = next;
                consumed = cur;
                continue;
            }
            break;
        }
        break;
    }
    default:
        if (!in.compile_operand(f, *n, op.arg)) {
            in.diag(DiagKind::MathExprError, l, "bad argument for '" + sym_str(n->key) + "'");
            return false;
        }
        if (op.arg.type == Operand::Type::Expr && op.arg.expr && op.arg.expr->failed) return false;
        break;
    }
    out.push_back(std::move(op));
    return true;
}

bool compile_ops(Interp& in, const ScriptFile& f, const Node& block, std::vector<MathOp>& out, MathExpr& root) {
    for (const Node* n = f.begin(block); n != f.end(block); ++n) {
        if (n->key == kNoSym) {
            in.diag(DiagKind::MathExprError, in.loc(f, *n), "bare value in a math expression");
            return false;
        }
        const Node* consumed = n;
        if (!compile_one(in, f, n, f.end(block), consumed, out, root)) return false;
        n = consumed;
    }
    return true;
}

Fixed apply_ops(Ctx& c, const std::vector<MathOp>& ops, Fixed acc);

Fixed b2f(bool b) { return Fixed::from_int(b ? 1 : 0); }

Fixed from_double_trunc(double d) {
    if (!std::isfinite(d)) return Fixed{};
    return Fixed::from_raw(static_cast<int64_t>(std::trunc(d * static_cast<double>(Fixed::SCALE))));
}

Fixed int_pow(Ctx& c, Fixed base, int64_t n, SrcLoc l) {
    Fixed r = Fixed::from_int(1);
    for (int64_t i = 0; i < n && i < 4096; ++i) r = c.in.arith(c, '*', r, base, l);
    return r;
}

Fixed apply_one(Ctx& c, const MathOp& op, Fixed acc) {
    Interp& in = c.in;
    auto arg = [&]() { return in.eval(c, op.arg, op.loc).num; };
    switch (op.k) {
    case MK::Value: return arg();
    case MK::Add: return in.arith(c, '+', acc, arg(), op.loc);
    case MK::Sub: return in.arith(c, '-', acc, arg(), op.loc);
    case MK::Mul: return in.arith(c, '*', acc, arg(), op.loc);
    case MK::Div: return in.arith(c, '/', acc, arg(), op.loc);
    case MK::Mod: return in.arith(c, '%', acc, arg(), op.loc);
    case MK::Min: { const Fixed a = arg(); return a < acc ? a : acc; }
    case MK::Max: { const Fixed a = arg(); return a > acc ? a : acc; }
    case MK::Pow: {
        const Fixed e = arg();
        if (e.raw() % Fixed::SCALE == 0) {
            const int64_t n = e.whole_trunc();
            if (n >= 0) return int_pow(c, acc, n, op.loc);
            return in.arith(c, '/', Fixed::from_int(1), int_pow(c, acc, -n, op.loc), op.loc);
        }
        return from_double_trunc(std::pow(acc.to_double(), e.to_double()));
    }
    case MK::Root: {
        const double n = arg().to_double();
        if (n == 0.0) return Fixed{};
        return from_double_trunc(std::pow(acc.to_double(), 1.0 / n));
    }
    case MK::Log: {
        const double base = arg().to_double();
        if (acc.raw() <= 0 || base <= 0.0 || base == 1.0) return Fixed{};
        return from_double_trunc(std::log(acc.to_double()) / std::log(base));
    }
    case MK::Sin: return from_double_trunc(std::sin(acc.to_double()));
    case MK::Cos: return from_double_trunc(std::cos(acc.to_double()));
    case MK::Tan: return from_double_trunc(std::tan(acc.to_double()));
    case MK::Atan: return from_double_trunc(std::atan(acc.to_double()));
    case MK::Round: return Fixed::round_half_away(acc);
    case MK::Clamp:
        for (const auto& [is_min, bound] : op.bounds) {
            const Fixed b = in.eval(c, bound, op.loc).num;
            if (is_min) {
                acc = acc < b ? b : acc;
            } else {
                acc = acc > b ? b : acc;
            }
        }
        return acc;
    case MK::Gt: return b2f(acc > arg());
    case MK::Lt: return b2f(acc < arg());
    case MK::Ge: return b2f(acc >= arg());
    case MK::Le: return b2f(acc <= arg());
    case MK::Eq: return b2f(acc == arg());
    case MK::Ne: return b2f(acc != arg());
    case MK::And: return b2f(acc.truthy() && arg().truthy());
    case MK::Or: return b2f(acc.truthy() || arg().truthy());
    case MK::Xor: return b2f(acc.truthy() != arg().truthy());
    case MK::Not: return b2f(!acc.truthy());
    case MK::If:
        for (const auto& br : op.branches) {
            if (!br.has_limit || eval_math(c, *br.limit).truthy()) {
                return apply_ops(c, br.body, acc);
            }
        }
        return acc;
    }
    return acc;
}

Fixed apply_ops(Ctx& c, const std::vector<MathOp>& ops, Fixed acc) {
    for (const auto& op : ops) {
        acc = apply_one(c, op, acc);
    }
    return acc;
}

} // namespace

std::shared_ptr<MathExpr> compile_math(Interp& in, const ScriptFile& f, const Node& block) {
    auto e = std::make_shared<MathExpr>();
    e->loc = in.loc(f, block);
    if (!compile_ops(in, f, block, e->ops, *e)) {
        e->failed = true;
        e->ops.clear();
    }
    return e;
}

Fixed eval_math(Ctx& c, const MathExpr& e) {
    if (e.failed) {
        return Fixed{};
    }
    return apply_ops(c, e.ops, Fixed{});
}

} // namespace pdx
