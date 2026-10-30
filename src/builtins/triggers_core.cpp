// Core triggers: AND / OR / NOT / always, check_variable, has_variable, flags, custom_trigger_tooltip, date.

#include "builtins/common.h"

namespace pdx {

namespace {

using sim::DiagKind;

enum class Cmp : uint8_t { Lt, Le, Gt, Ge, Eq, Ne };

bool cmp_from_text(std::string_view s, Cmp& out) {
    if (s == "less_than") out = Cmp::Lt;
    else if (s == "less_than_or_equals") out = Cmp::Le;
    else if (s == "greater_than") out = Cmp::Gt;
    else if (s == "greater_than_or_equals") out = Cmp::Ge;
    else if (s == "equals") out = Cmp::Eq;
    else if (s == "not_equals") out = Cmp::Ne;
    else return false;
    return true;
}

bool compare(Fixed a, Cmp c, Fixed b) {
    switch (c) {
    case Cmp::Lt: return a < b;
    case Cmp::Le: return a <= b;
    case Cmp::Gt: return a > b;
    case Cmp::Ge: return a >= b;
    case Cmp::Eq: return a == b;
    case Cmp::Ne: return a != b;
    }
    return false;
}

// ---- logic ----

struct And : TriggerOp {
    TriggerBlock body;
    bool eval(Ctx& c) const override { return c.in.eval_and(c, body); }
};
struct Or : TriggerOp {
    TriggerBlock body;
    bool eval(Ctx& c) const override {
        for (const auto& t : body) {
            if (t->eval(c)) return true;
        }
        return body.empty();
    }
};
struct Not : TriggerOp {
    TriggerBlock body; // NOT = { a b } is true when NONE is true (NOR), as in HOI4
    bool eval(Ctx& c) const override {
        for (const auto& t : body) {
            if (t->eval(c)) return false;
        }
        return true;
    }
};
struct Const : TriggerOp {
    bool v = true;
    bool eval(Ctx&) const override { return v; }
};

std::unique_ptr<TriggerOp> make_and(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<And>();
    t->body = in.compile_triggers(f, n);
    return t;
}
std::unique_ptr<TriggerOp> make_or(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<Or>();
    t->body = in.compile_triggers(f, n);
    return t;
}
std::unique_ptr<TriggerOp> make_not(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<Not>();
    t->body = in.compile_triggers(f, n);
    return t;
}
std::unique_ptr<TriggerOp> make_always(Interp&, const ScriptFile&, const Node& n) {
    auto t = std::make_unique<Const>();
    t->v = util::is_yes(n.value);
    return t;
}
std::unique_ptr<TriggerOp> make_custom_trigger_tooltip(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<And>();
    t->body = bi::compile_triggers_except(in, f, n, {"tooltip"});
    return t;
}

// ---- check_variable ----

struct CheckVariable : TriggerOp {
    VarRef lhs;
    Operand rhs;
    Cmp cmp = Cmp::Ge;
    bool unsupported = false;
    bool eval(Ctx& c) const override {
        if (unsupported) return false;
        const Value a = c.in.read(c, lhs, loc);
        const Value b = c.in.eval(c, rhs, loc);
        return compare(a.num, cmp, b.num);
    }
};

std::unique_ptr<TriggerOp> make_check_variable(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<CheckVariable>();
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) {
        in.diag(DiagKind::BadParameter, l, "check_variable needs a { } block");
        return nullptr;
    }
    if (const Node* var = bi::find_child(f, n, "var")) {
        if (!in.compile_ref(sym_view(var->value), l, t->lhs)) return nullptr;
        const Node* val = bi::find_child(f, n, "value");
        if (!val || !in.compile_operand(f, *val, t->rhs)) {
            in.diag(DiagKind::BadParameter, l, "check_variable long form without value");
            return nullptr;
        }
        t->cmp = Cmp::Ge; // documented default for the long form
        if (const Node* cmp = bi::find_child(f, n, "compare")) {
            if (!cmp_from_text(sym_view(cmp->value), t->cmp)) {
                in.diag(DiagKind::BadParameter, l, "unknown compare '" + sym_str(cmp->value) + "'");
            }
        }
        return t;
    }
    const Node* pair = nullptr;
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (c->key != kNoSym && !bi::key_is(*c, "tooltip")) {
            pair = c;
            break;
        }
    }
    if (!pair) {
        in.diag(DiagKind::BadParameter, l, "check_variable has no comparison");
        return nullptr;
    }
    if (!in.compile_ref(sym_view(pair->key), l, t->lhs)) return nullptr;
    if (!in.compile_operand(f, *pair, t->rhs)) return nullptr;
    const Semantics& s = in.semantics();
    switch (pair->op) {
    case Op::Gt: t->cmp = Cmp::Gt; break;
    case Op::Lt: t->cmp = Cmp::Lt; break;
    case Op::Eq: t->cmp = s.check_shorthand_eq_is_equals ? Cmp::Eq : Cmp::Ge; break;
    case Op::Ge:
    case Op::Le:
    case Op::Ne:
        if (s.check_shorthand_allows_le_ge) {
            t->cmp = pair->op == Op::Ge ? Cmp::Ge : pair->op == Op::Le ? Cmp::Le : Cmp::Ne;
        } else {
            t->unsupported = true;
            in.diag(DiagKind::BadParameter, l,
                    std::string("check_variable shorthand '") + op_text(pair->op) +
                        "' is not supported in-game (use NOT + < / >); evaluates false");
        }
        break;
    default: break;
    }
    return t;
}

// ---- has_variable ----

struct HasVariable : TriggerOp {
    VarRef ref;
    bool eval(Ctx& c) const override { return c.in.has(c, ref, loc); }
};

std::unique_ptr<TriggerOp> make_has_variable(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<HasVariable>();
    const SrcLoc l = in.loc(f, n);
    std::string_view name = sym_view(n.value);
    if (n.is_block) {
        const Node* v = bi::find_child(f, n, "var");
        if (!v) v = f.begin(n) != f.end(n) ? f.begin(n) : nullptr;
        if (!v) return nullptr;
        name = sym_view(v->key == kNoSym || bi::key_is(*v, "var") ? v->value : v->key);
    }
    if (!in.compile_ref(name, l, t->ref)) return nullptr;
    return t;
}

// ---- flags ----

enum class FlagScope : uint8_t { Country, State, Global };

struct HasFlag : TriggerOp {
    FlagScope fs = FlagScope::Country;
    Sym flag = kNoSym;
    bool has_value = false, has_days = false;
    Op value_op = Op::Gt, days_op = Op::Gt;
    int64_t value = 0, days = 0;
    bool eval(Ctx& c) const override {
        const ScopeRef s = fs == FlagScope::Global ? ScopeRef::global() : c.self();
        const auto* flags = c.w.flags(s);
        if (!flags) return false;
        auto it = flags->find(flag);
        if (it == flags->end()) return false;
        auto cmp = [](int64_t a, Op op, int64_t b) {
            switch (op) {
            case Op::Lt: return a < b;
            case Op::Gt: return a > b;
            case Op::Le: return a <= b;
            case Op::Ge: return a >= b;
            case Op::Ne: return a != b;
            case Op::Eq:
            default: return a == b;
            }
        };
        if (has_value && !cmp(it->second.value, value_op, value)) return false;
        if (!has_value && it->second.value <= 0) return false; // "if not set, the value comparison is > 0"
        if (has_days && !cmp(c.w.date.ordinal() - it->second.set_date.ordinal(), days_op, days)) return false;
        return true;
    }
};

template <FlagScope FS>
std::unique_ptr<TriggerOp> make_has_flag(Interp&, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<HasFlag>();
    t->fs = FS;
    if (!n.is_block) {
        t->flag = n.value;
        return t;
    }
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (bi::key_is(*c, "flag")) t->flag = c->value;
        else if (bi::key_is(*c, "value")) {
            t->has_value = true;
            t->value_op = c->op;
            Fixed v;
            Fixed::parse(sym_view(c->value), v);
            t->value = v.whole_trunc();
        } else if (bi::key_is(*c, "days")) {
            t->has_days = true;
            t->days_op = c->op;
            Fixed v;
            Fixed::parse(sym_view(c->value), v);
            t->days = v.whole_trunc();
        }
    }
    return t;
}

// ---- date ----

struct DateTrigger : TriggerOp {
    Op op = Op::Gt;
    world::Date date;
    bool eval(Ctx& c) const override {
        const int64_t a = c.w.date.ordinal(), b = date.ordinal();
        return op == Op::Lt ? a < b : op == Op::Gt ? a > b : op == Op::Le ? a <= b : op == Op::Ge ? a >= b : a == b;
    }
};

std::unique_ptr<TriggerOp> make_date(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<DateTrigger>();
    t->op = n.op;
    if (!world::Date::parse(sym_view(n.value), t->date)) {
        in.diag(DiagKind::BadParameter, in.loc(f, n), "bad date '" + sym_str(n.value) + "'");
        return nullptr;
    }
    return t;
}

} // namespace

void register_triggers_core(Registry& r) {
    r.trigger("AND", make_and);
    r.trigger("OR", make_or);
    r.trigger("NOT", make_not);
    r.trigger("NOR", make_not);
    r.trigger("always", make_always);
    r.trigger("custom_trigger_tooltip", make_custom_trigger_tooltip);
    r.trigger("hidden_trigger", make_and);
    r.trigger("check_variable", make_check_variable);
    r.trigger("has_variable", make_has_variable);
    r.trigger("has_country_flag", make_has_flag<FlagScope::Country>);
    r.trigger("has_state_flag", make_has_flag<FlagScope::State>);
    r.trigger("has_global_flag", make_has_flag<FlagScope::Global>);
    r.trigger("date", make_date);
}

} // namespace pdx
