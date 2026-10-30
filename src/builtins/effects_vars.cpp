// Variable effects: set/add_to/subtract_from/multiply/divide/modulo/clamp/round/clear × variable/temp_variable.
// Forms: short `{ x = 5 }`, long `{ var = x value = 5 }`, flat `round_variable = x`, math RHS `{ x = { value = .. } }`.

#include "builtins/common.h"

namespace pdx {

namespace {

using sim::DiagKind;

enum class VarOp : uint8_t { Set, Add, Sub, Mul, Div, Mod, Clamp, Round, Clear, Random };

struct VarEffect : EffectOp {
    VarOp op = VarOp::Set;
    bool temp = false;
    VarRef target;
    Operand value;
    bool has_min = false, has_max = false, integer = false;
    Operand min, max;

    void run(Ctx& c) const override {
        Interp& in = c.in;
        Interp::Slot slot;
        if (!in.resolve_slot(c, target, temp, loc, slot)) {
            return;
        }
        switch (op) {
        case VarOp::Set:
            in.slot_set(c, slot, in.eval(c, value, loc), loc);
            return;
        case VarOp::Clear:
            if (slot.store) slot.store->erase(slot.key);
            return;
        case VarOp::Round:
            in.slot_set(c, slot, Value::number(Fixed::round_half_away(in.slot_get(slot).num)), loc);
            return;
        case VarOp::Clamp: {
            Fixed v = in.slot_get(slot).num;
            if (has_min) {
                const Fixed lo = in.eval(c, min, loc).num;
                if (v < lo) v = lo;
            }
            if (has_max) {
                const Fixed hi = in.eval(c, max, loc).num;
                if (v > hi) v = hi;
            }
            in.slot_set(c, slot, Value::number(v), loc);
            return;
        }
        case VarOp::Random: {
            const Fixed lo = has_min ? in.eval(c, min, loc).num : Fixed{};
            const Fixed hi = has_max ? in.eval(c, max, loc).num : Fixed::from_int(1);
            OpStatus st;
            Fixed r = Fixed::add(lo, Fixed::mul(Fixed::sub(hi, lo, in.semantics(), st), c.w.rng.unit(), in.semantics(), st),
                                 in.semantics(), st);
            if (integer) r = Fixed::floor(r);
            in.slot_set(c, slot, Value::number(r), loc);
            return;
        }
        default: {
            const Value cur = in.slot_get(slot);
            const Value rhs = in.eval(c, value, loc);
            if (rhs.kind == Kind::Token || rhs.kind == Kind::Country) {
                in.check_number(c, rhs, loc);
            }
            const char ch = op == VarOp::Add ? '+' : op == VarOp::Sub ? '-' : op == VarOp::Mul ? '*' : op == VarOp::Div ? '/' : '%';
            in.slot_set(c, slot, Value::number(in.arith(c, ch, cur.num, rhs.num, loc)), loc);
            return;
        }
        }
    }
};

bool is_param_key(std::string_view k) {
    return k == "var" || k == "value" || k == "min" || k == "max" || k == "tooltip" || k == "integer" ||
           k == "compare";
}

std::unique_ptr<EffectOp> make_var_effect(Interp& in, const ScriptFile& f, const Node& n, VarOp op, bool temp) {
    auto e = std::make_unique<VarEffect>();
    e->op = op;
    e->temp = temp;
    const SrcLoc l = in.loc(f, n);

    // Flat: round_variable = x / clear_variable = x
    if (!n.is_block) {
        if (op != VarOp::Round && op != VarOp::Clear && op != VarOp::Random) {
            in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " needs a { } block");
            return nullptr;
        }
        if (!in.compile_ref(sym_view(n.value), l, e->target)) return nullptr;
        return e;
    }

    const Node* var = bi::find_child(f, n, "var");
    if (var) {
        // Long form
        if (var->is_block || !in.compile_ref(sym_view(var->value), l, e->target)) return nullptr;
        if (const Node* v = bi::find_child(f, n, "value")) {
            if (!in.compile_operand(f, *v, e->value)) return nullptr;
        } else if (op != VarOp::Clamp && op != VarOp::Round && op != VarOp::Clear && op != VarOp::Random) {
            in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " long form without value");
            return nullptr;
        }
    } else {
        // Short form: the first non-parameter child is `target = value`
        const Node* pair = nullptr;
        int pairs = 0;
        for (const Node* c = f.begin(n); c != f.end(n); ++c) {
            if (c->key == kNoSym) {
                // `clear_variable = { x }`-style bare name
                if (!pair && (op == VarOp::Round || op == VarOp::Clear || op == VarOp::Random)) {
                    if (!in.compile_ref(sym_view(c->value), l, e->target)) return nullptr;
                    return e;
                }
                continue;
            }
            if (is_param_key(sym_view(c->key))) continue;
            if (!pair) pair = c;
            ++pairs;
        }
        if (!pair) {
            in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " has no variable");
            return nullptr;
        }
        if (pairs > 1) {
            in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " has more than one variable; only the first is used");
        }
        if (!in.compile_ref(sym_view(pair->key), l, e->target)) return nullptr;
        if (op != VarOp::Clamp && op != VarOp::Round && op != VarOp::Clear && op != VarOp::Random) {
            if (!in.compile_operand(f, *pair, e->value)) return nullptr;
        }
    }
    if (const Node* mn = bi::find_child(f, n, "min")) {
        e->has_min = in.compile_operand(f, *mn, e->min);
    }
    if (const Node* mx = bi::find_child(f, n, "max")) {
        e->has_max = in.compile_operand(f, *mx, e->max);
    }
    if (const Node* it = bi::find_child(f, n, "integer")) {
        e->integer = util::is_yes(it->value);
    }
    return e;
}

#define VAR_EFFECT(fn, op, temp) \
    std::unique_ptr<EffectOp> fn(Interp& in, const ScriptFile& f, const Node& n) { return make_var_effect(in, f, n, op, temp); }

VAR_EFFECT(set_variable, VarOp::Set, false)
VAR_EFFECT(set_temp_variable, VarOp::Set, true)
VAR_EFFECT(add_to_variable, VarOp::Add, false)
VAR_EFFECT(add_to_temp_variable, VarOp::Add, true)
VAR_EFFECT(subtract_from_variable, VarOp::Sub, false)
VAR_EFFECT(subtract_from_temp_variable, VarOp::Sub, true)
VAR_EFFECT(multiply_variable, VarOp::Mul, false)
VAR_EFFECT(multiply_temp_variable, VarOp::Mul, true)
VAR_EFFECT(divide_variable, VarOp::Div, false)
VAR_EFFECT(divide_temp_variable, VarOp::Div, true)
VAR_EFFECT(modulo_variable, VarOp::Mod, false)
VAR_EFFECT(modulo_temp_variable, VarOp::Mod, true)
VAR_EFFECT(clamp_variable, VarOp::Clamp, false)
VAR_EFFECT(clamp_temp_variable, VarOp::Clamp, true)
VAR_EFFECT(round_variable, VarOp::Round, false)
VAR_EFFECT(round_temp_variable, VarOp::Round, true)
VAR_EFFECT(clear_variable, VarOp::Clear, false)
VAR_EFFECT(clear_temp_variable, VarOp::Clear, true)
VAR_EFFECT(set_variable_to_random, VarOp::Random, false)
VAR_EFFECT(set_temp_variable_to_random, VarOp::Random, true)

#undef VAR_EFFECT

} // namespace

void register_effects_vars(Registry& r) {
    r.effect("set_variable", set_variable);
    r.effect("set_temp_variable", set_temp_variable);
    r.effect("add_to_variable", add_to_variable);
    r.effect("add_to_temp_variable", add_to_temp_variable);
    r.effect("subtract_from_variable", subtract_from_variable);
    r.effect("subtract_from_temp_variable", subtract_from_temp_variable);
    r.effect("multiply_variable", multiply_variable);
    r.effect("multiply_temp_variable", multiply_temp_variable);
    r.effect("divide_variable", divide_variable);
    r.effect("divide_temp_variable", divide_temp_variable);
    r.effect("modulo_variable", modulo_variable);
    r.effect("modulo_temp_variable", modulo_temp_variable);
    r.effect("clamp_variable", clamp_variable);
    r.effect("clamp_temp_variable", clamp_temp_variable);
    r.effect("round_variable", round_variable);
    r.effect("round_temp_variable", round_temp_variable);
    r.effect("clear_variable", clear_variable);
    r.effect("clear_temp_variable", clear_temp_variable);
    r.effect("set_variable_to_random", set_variable_to_random);
    r.effect("set_temp_variable_to_random", set_temp_variable_to_random);
    r.effect("randomize_variable", set_variable_to_random, BuiltinStatus::Approx);      // same var/min/max form
    r.effect("randomize_temp_variable", set_temp_variable_to_random, BuiltinStatus::Approx);
}

} // namespace pdx
