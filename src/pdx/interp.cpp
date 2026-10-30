#include "pdx/interp.h"

#include "pdx/mathexpr.h"

#include <algorithm>
#include <chrono>

namespace pdx {

using sim::DiagKind;

const char* builtin_status_name(BuiltinStatus s) {
    switch (s) {
    case BuiltinStatus::Full: return "full";
    case BuiltinStatus::Approx: return "approx";
    case BuiltinStatus::Stub: return "stub";
    }
    return "?";
}

bool Ctx::break_requested() const {
    if (break_stack.empty() || temps == nullptr) {
        return false;
    }
    const Value* v = temps->find(world::make_key(break_stack.back()));
    return v != nullptr && v->num.truthy();
}

namespace util {

const Node* child(const ScriptFile& f, const Node& block, const char* key) {
    if (!block.is_block) {
        return nullptr;
    }
    return f.find(block, intern(key));
}

bool is_yes(Sym v) {
    static const Sym yes = intern("yes");
    return v == yes;
}

Sym sym_of(const char* text) { return intern(text); }

} // namespace util

namespace {

// ---------------------------------------------------------------------------------------------
// Core ops
// ---------------------------------------------------------------------------------------------

struct NoOp : EffectOp {
    void run(Ctx&) const override {}
};

struct TrueTrigger : TriggerOp {
    bool eval(Ctx&) const override { return true; }
};
struct FalseTrigger : TriggerOp {
    bool eval(Ctx&) const override { return false; }
};

struct CallScriptedEffect : EffectOp {
    ScriptedDef* def = nullptr;
    Sym name = kNoSym;
    void run(Ctx& c) const override {
        if (c.depth >= c.in.semantics().max_call_depth) {
            c.in.diag(DiagKind::RecursionLimit, loc, "call depth limit reached calling " + sym_str(name));
            return;
        }
        if (!def->effects) {
            def->effects = std::make_unique<EffectBlock>(c.in.compile_effects(*def->file, *def->node));
        }
        ++def->calls;
        ++c.depth;
        if (c.in.profiling) {
            const auto t0 = std::chrono::steady_clock::now();
            c.in.run_block(c, *def->effects);
            auto& st = c.in.profile[name];
            ++st.calls;
            st.inclusive_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        } else {
            c.in.run_block(c, *def->effects);
        }
        --c.depth;
    }
};

struct CallScriptedTrigger : TriggerOp {
    ScriptedDef* def = nullptr;
    Sym name = kNoSym;
    bool negate = false;
    bool eval(Ctx& c) const override {
        if (c.depth >= c.in.semantics().max_call_depth) {
            c.in.diag(DiagKind::RecursionLimit, loc, "call depth limit reached calling trigger " + sym_str(name));
            return false;
        }
        if (!def->triggers) {
            def->triggers = std::make_unique<TriggerBlock>(c.in.compile_triggers(*def->file, *def->node));
        }
        ++def->calls;
        ++c.depth;
        const bool r = c.in.eval_and(c, *def->triggers);
        --c.depth;
        return negate ? !r : r;
    }
};

struct IfChain : EffectOp {
    struct Branch {
        bool has_limit = false;
        TriggerBlock limit;
        EffectBlock body;
    };
    std::vector<Branch> branches;
    void run(Ctx& c) const override {
        for (const auto& b : branches) {
            if (!b.has_limit || c.in.eval_and(c, b.limit)) {
                c.in.run_block(c, b.body);
                return;
            }
        }
    }
};

struct TriggerIfChain : TriggerOp {
    struct Branch {
        bool has_limit = false;
        TriggerBlock limit;
        TriggerBlock body;
    };
    std::vector<Branch> branches;
    bool eval(Ctx& c) const override {
        for (const auto& b : branches) {
            if (!b.has_limit || c.in.eval_and(c, b.limit)) {
                return c.in.eval_and(c, b.body);
            }
        }
        return true;
    }
};

struct ScopeEffect : EffectOp {
    Step step;
    EffectBlock body;
    void run(Ctx& c) const override {
        const ScopeRef s = c.in.resolve_step(c, step, c.self(), loc);
        if (!s.valid()) {
            return;
        }
        ScopeGuard g(c, s);
        c.in.run_block(c, body);
    }
};

struct ScopeTrigger : TriggerOp {
    Step step;
    TriggerBlock body;
    bool eval(Ctx& c) const override {
        const ScopeRef s = c.in.resolve_step(c, step, c.self(), loc);
        if (!s.valid()) {
            return false;
        }
        ScopeGuard g(c, s);
        return c.in.eval_and(c, body);
    }
};

struct EffectAsTrigger : TriggerOp {
    std::unique_ptr<EffectOp> effect;
    bool eval(Ctx& c) const override {
        effect->run(c);
        return true;
    }
};

// Effects that HOI4 allows inside trigger blocks (temp-variable manipulation).
bool allowed_in_trigger(std::string_view key) {
    static const char* names[] = {"set_temp_variable", "add_to_temp_variable", "subtract_from_temp_variable",
                                  "multiply_temp_variable", "divide_temp_variable", "clamp_temp_variable",
                                  "round_temp_variable", "modulo_temp_variable", "set_temp_variable_to_random",
                                  "add_to_temp_array", "remove_from_temp_array", "clear_temp_array",
                                  "resize_temp_array", "find_highest_in_array", "find_lowest_in_array"};
    for (const char* n : names) {
        if (key == n) return true;
    }
    return false;
}

bool parse_step_word(std::string_view key, const world::World& w, Step& out) {
    if (key == "THIS" || key == "this") { out.kind = StepKind::This; return true; }
    if (key == "ROOT" || key == "root") { out.kind = StepKind::Root; return true; }
    if (key == "FROM" || key == "from") { out.kind = StepKind::From; return true; }
    if (key == "PREV" || key == "prev") { out.kind = StepKind::Prev; return true; }
    if (key == "PREV.PREV") { out.kind = StepKind::Prev; out.prev_depth = 2; return true; }
    if (key == "PREV.PREV.PREV") { out.kind = StepKind::Prev; out.prev_depth = 3; return true; }
    if (key == "controller") { out.kind = StepKind::Controller; return true; }
    if (key == "owner") { out.kind = StepKind::Owner; return true; }
    if (key == "capital_scope") { out.kind = StepKind::Capital; return true; }
    if (key == "overlord" || key == "OVERLORD") { out.kind = StepKind::Overlord; return true; }
    if (key.size() > 4 && key.substr(0, 4) == "var:") {
        out.kind = StepKind::Named;
        out.name = intern(key.substr(4));
        return true;
    }
    if (!key.empty() && std::all_of(key.begin(), key.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
        out.kind = StepKind::StateId;
        out.number = std::stoi(std::string(key));
        return true;
    }
    if (looks_like_tag(key) && w.country_index(intern(key)) >= 0) {
        out.kind = StepKind::Tag;
        out.name = intern(key);
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Interp
// ---------------------------------------------------------------------------------------------

Interp::Interp(world::World& w, sim::Diagnostics& diags, Semantics sem)
    : w_(w), diags_(diags), sem_(sem), reg_(builtin_registry()) {}

const ScriptFile* Interp::add_file(ScriptFile&& f) {
    f.file_id = static_cast<uint32_t>(files_.size());
    files_.push_back(std::move(f));
    return &files_.back();
}

const ScriptFile* Interp::file(uint32_t file_id) const {
    return file_id < files_.size() ? &files_[file_id] : nullptr;
}

std::string Interp::file_path(uint32_t file_id) const {
    const ScriptFile* f = file(file_id);
    return f ? f->path : "<script>";
}

void Interp::index_scripted_effects(const ScriptFile& f) {
    for (const Node* n = f.root_begin(); n != f.root_end(); ++n) {
        if (n->key != kNoSym && n->is_block) {
            ScriptedDef& d = scripted_effects_[n->key];
            d.file = &f;
            d.node = n;
            d.effects.reset();
        }
    }
}

void Interp::index_scripted_triggers(const ScriptFile& f) {
    for (const Node* n = f.root_begin(); n != f.root_end(); ++n) {
        if (n->key != kNoSym && n->is_block) {
            ScriptedDef& d = scripted_triggers_[n->key];
            d.file = &f;
            d.node = n;
            d.triggers.reset();
        }
    }
}

ScriptedDef* Interp::scripted_effect(Sym name) {
    auto it = scripted_effects_.find(name);
    return it == scripted_effects_.end() ? nullptr : &it->second;
}

ScriptedDef* Interp::scripted_trigger(Sym name) {
    auto it = scripted_triggers_.find(name);
    return it == scripted_triggers_.end() ? nullptr : &it->second;
}

void Interp::set_skipped_effects(std::vector<Sym> names) { skipped_ = std::move(names); }

bool Interp::is_skipped(Sym name) const {
    return std::find(skipped_.begin(), skipped_.end(), name) != skipped_.end();
}

void Interp::diag(DiagKind k, SrcLoc l, const std::string& msg) {
    diags_.report(k, file_path(l.file_id), l.line, msg, w_.date.to_string());
}

// ---- compilation ----

EffectBlock Interp::compile_effects(const ScriptFile& f, const Node& block) {
    EffectBlock out;
    if (!block.is_block) {
        return out;
    }
    const Node* end = f.end(block);
    for (const Node* n = f.begin(block); n != end; ++n) {
        const Node* consumed = n;
        auto op = compile_effect(f, *n, end, &consumed);
        if (op) {
            out.push_back(std::move(op));
        }
        n = consumed;
    }
    return out;
}

TriggerBlock Interp::compile_triggers(const ScriptFile& f, const Node& block) {
    TriggerBlock out;
    if (!block.is_block) {
        return out;
    }
    const Node* end = f.end(block);
    for (const Node* n = f.begin(block); n != end; ++n) {
        const Node* consumed = n;
        auto op = compile_trigger(f, *n, end, &consumed);
        if (op) {
            out.push_back(std::move(op));
        }
        n = consumed;
    }
    return out;
}

std::unique_ptr<EffectOp> Interp::compile_effect(const ScriptFile& f, const Node& n, const Node* siblings_end,
                                                 const Node** consumed_until) {
    static const Sym s_if = intern("if"), s_else_if = intern("else_if"), s_else = intern("else"),
                     s_limit = intern("limit");
    const SrcLoc l = loc(f, n);
    if (n.key == kNoSym) {
        diag(DiagKind::BadParameter, l, "bare value '" + sym_str(n.value) + "' in an effect block");
        return nullptr;
    }
    const std::string_view key = sym_view(n.key);

    if (n.key == s_if) {
        auto chain = std::make_unique<IfChain>();
        chain->loc = l;
        const Node* cur = &n;
        for (;;) {
            IfChain::Branch b;
            b.has_limit = cur->key != s_else;
            if (cur->is_block) {
                for (const Node* c = f.begin(*cur); c != f.end(*cur); ++c) {
                    if (c->key == s_limit && b.has_limit) {
                        b.limit = compile_triggers(f, *c);
                        continue;
                    }
                    const Node* consumed = c;
                    if (auto op = compile_effect(f, *c, f.end(*cur), &consumed)) {
                        b.body.push_back(std::move(op));
                    }
                    c = consumed;
                }
            }
            chain->branches.push_back(std::move(b));
            const Node* next = cur + 1;
            if (siblings_end && next < siblings_end && (next->key == s_else_if || next->key == s_else) &&
                chain->branches.back().has_limit) {
                cur = next;
                if (consumed_until) *consumed_until = cur;
                continue;
            }
            break;
        }
        return chain;
    }
    if (n.key == s_else_if || n.key == s_else) {
        diag(DiagKind::BadParameter, l, std::string(key) + " without a preceding if");
        return nullptr;
    }

    // Scripted effect call
    if (ScriptedDef* def = scripted_effect(n.key)) {
        if (is_skipped(n.key)) {
            auto op = std::make_unique<NoOp>();
            op->loc = l;
            return op;
        }
        auto op = std::make_unique<CallScriptedEffect>();
        op->loc = l;
        op->def = def;
        op->name = n.key;
        return op;
    }

    // Builtin effect
    auto it = reg_.effects.find(n.key);
    if (it != reg_.effects.end()) {
        ++builtin_uses[n.key];
        auto op = it->second.make(*this, f, n);
        if (op) {
            op->loc = l;
        }
        return op;
    }

    // Scope change block: TAG / state id / ROOT / var:x / controller ...
    Step step;
    if (n.is_block && parse_step_word(key, w_, step)) {
        auto op = std::make_unique<ScopeEffect>();
        op->loc = l;
        op->step = step;
        op->body = compile_effects(f, n);
        return op;
    }

    if (reg_.triggers.count(n.key)) {
        diag(DiagKind::UnknownEffect, l, "'" + std::string(key) + "' is a trigger, used in an effect block");
    } else if (!n.is_block && util::is_yes(n.value)) {
        diag(DiagKind::UnknownScriptedEffect, l, "undefined scripted effect '" + std::string(key) + "'");
    } else {
        diag(DiagKind::UnknownEffect, l, "unknown effect '" + std::string(key) + "'");
    }
    return nullptr;
}

std::unique_ptr<TriggerOp> Interp::compile_trigger(const ScriptFile& f, const Node& n, const Node* siblings_end,
                                                   const Node** consumed_until) {
    static const Sym s_if = intern("if"), s_else_if = intern("else_if"), s_else = intern("else"),
                     s_limit = intern("limit");
    const SrcLoc l = loc(f, n);
    if (n.key == kNoSym) {
        diag(DiagKind::BadParameter, l, "bare value '" + sym_str(n.value) + "' in a trigger block");
        return nullptr;
    }
    const std::string_view key = sym_view(n.key);

    if (n.key == s_if) {
        auto chain = std::make_unique<TriggerIfChain>();
        chain->loc = l;
        const Node* cur = &n;
        for (;;) {
            TriggerIfChain::Branch b;
            b.has_limit = cur->key != s_else;
            if (cur->is_block) {
                for (const Node* c = f.begin(*cur); c != f.end(*cur); ++c) {
                    if (c->key == s_limit && b.has_limit) {
                        b.limit = compile_triggers(f, *c);
                        continue;
                    }
                    const Node* consumed = c;
                    if (auto op = compile_trigger(f, *c, f.end(*cur), &consumed)) {
                        b.body.push_back(std::move(op));
                    }
                    c = consumed;
                }
            }
            chain->branches.push_back(std::move(b));
            const Node* next = cur + 1;
            if (siblings_end && next < siblings_end && (next->key == s_else_if || next->key == s_else) &&
                chain->branches.back().has_limit) {
                cur = next;
                if (consumed_until) *consumed_until = cur;
                continue;
            }
            break;
        }
        return chain;
    }
    if (n.key == s_else_if || n.key == s_else) {
        diag(DiagKind::BadParameter, l, std::string(key) + " without a preceding if (trigger)");
        return nullptr;
    }

    if (ScriptedDef* def = scripted_trigger(n.key)) {
        auto op = std::make_unique<CallScriptedTrigger>();
        op->loc = l;
        op->def = def;
        op->name = n.key;
        static const Sym s_no = intern("no");
        op->negate = !n.is_block && n.value == s_no;
        return op;
    }

    auto it = reg_.triggers.find(n.key);
    if (it != reg_.triggers.end()) {
        ++builtin_uses[n.key];
        auto op = it->second.make(*this, f, n);
        if (op) {
            op->loc = l;
        }
        return op;
    }

    if (allowed_in_trigger(key)) {
        auto eit = reg_.effects.find(n.key);
        if (eit != reg_.effects.end()) {
            auto inner = eit->second.make(*this, f, n);
            if (!inner) return nullptr;
            inner->loc = l;
            auto op = std::make_unique<EffectAsTrigger>();
            op->loc = l;
            op->effect = std::move(inner);
            return op;
        }
    }

    Step step;
    if (n.is_block && parse_step_word(key, w_, step)) {
        auto op = std::make_unique<ScopeTrigger>();
        op->loc = l;
        op->step = step;
        op->body = compile_triggers(f, n);
        return op;
    }

    if (reg_.effects.count(n.key) || scripted_effect(n.key)) {
        diag(DiagKind::UnknownTrigger, l, "'" + std::string(key) + "' is an effect, used in a trigger block (evaluates false)");
    } else {
        diag(DiagKind::UnknownTrigger, l, "unknown trigger '" + std::string(key) + "' (evaluates false)");
    }
    auto op = std::make_unique<FalseTrigger>();
    op->loc = l;
    return op;
}

bool Interp::compile_ref(std::string_view text, SrcLoc l, VarRef& out) {
    std::string err;
    if (!parse_varref(text, out, &err)) {
        diag(DiagKind::BadParameter, l, "bad variable reference '" + std::string(text) + "': " + err);
        return false;
    }
    if (reg_.game_vars.count(out.name)) ++builtin_uses[out.name];
    return true;
}

bool Interp::compile_operand_text(std::string_view text, SrcLoc l, Operand& out) {
    Fixed num;
    if (Fixed::parse(text, num)) {
        out.type = Operand::Type::Literal;
        out.literal = Value::number(num);
        return true;
    }
    if (text == "yes" || text == "no") {
        out.type = Operand::Type::Literal;
        out.literal = Value::integer(text == "yes" ? 1 : 0);
        return true;
    }
    if (text.size() > 6 && text.substr(0, 6) == "token:") {
        out.type = Operand::Type::Literal;
        const Sym t = intern(text.substr(6));
        out.literal = Value::token(t);
        if (hooks.is_token && !hooks.is_token(t)) {
            diag(DiagKind::TokenExpected, l, "unknown token '" + std::string(text.substr(6)) + "'");
        }
        return true;
    }
    if (text == "random") {
        out.type = Operand::Type::Random;
        return true;
    }
    Step st;
    if (parse_step_word(text, w_, st) && st.kind != StepKind::Named && st.kind != StepKind::StateId &&
        st.kind != StepKind::Controller && st.kind != StepKind::Owner && st.kind != StepKind::Capital &&
        st.kind != StepKind::Overlord) {
        out.type = Operand::Type::Scope;
        out.scope = st;
        return true;
    }
    if (text.size() > 4 && text.substr(0, 4) == "var:") {
        text = text.substr(4);
    }
    out.type = Operand::Type::Ref;
    return compile_ref(text, l, out.ref);
}

bool Interp::compile_operand(const ScriptFile& f, const Node& n, Operand& out) {
    const SrcLoc l = loc(f, n);
    if (n.is_block) {
        out.type = Operand::Type::Expr;
        out.expr = compile_math(*this, f, n);
        return true;
    }
    return compile_operand_text(sym_view(n.value), l, out);
}

// ---- execution ----

void Interp::run(const EffectBlock& block, ScopeRef self, ScopeRef root, ScopeRef from, world::VarStore* temps) {
    world::VarStore local;
    Ctx c(*this, w_);
    c.scopes.push_back(self);
    c.root = root.valid() ? root : self;
    c.from = from;
    c.temps = temps ? temps : &local;
    run_block(c, block);
}

bool Interp::test(const TriggerBlock& block, ScopeRef self, ScopeRef root, ScopeRef from, world::VarStore* temps) {
    world::VarStore local;
    Ctx c(*this, w_);
    c.scopes.push_back(self);
    c.root = root.valid() ? root : self;
    c.from = from;
    c.temps = temps ? temps : &local;
    return eval_and(c, block);
}

void Interp::run_block(Ctx& c, const EffectBlock& block) const {
    for (const auto& op : block) {
        op->run(c);
        if (!c.break_stack.empty() && c.break_requested()) {
            return;
        }
    }
}

bool Interp::eval_and(Ctx& c, const TriggerBlock& block) const {
    for (const auto& op : block) {
        if (!op->eval(c)) {
            return false;
        }
    }
    return true;
}

// ---- values ----

Fixed Interp::arith(Ctx& c, char op, Fixed a, Fixed b, SrcLoc l) {
    (void)c;
    OpStatus st;
    Fixed r;
    switch (op) {
    case '+': r = Fixed::add(a, b, sem_, st); break;
    case '-': r = Fixed::sub(a, b, sem_, st); break;
    case '*': r = Fixed::mul(a, b, sem_, st); break;
    case '/': r = Fixed::div(a, b, sem_, st); break;
    case '%': r = Fixed::mod(a, b, sem_, st); break;
    default: r = a; break;
    }
    if (st.overflow) {
        diag(DiagKind::Overflow, l, "value out of range (" + a.to_string_exact() + " " + op + " " + b.to_string_exact() + ")");
    }
    if (st.div_by_zero) {
        diag(DiagKind::DivByZero, l, std::string(op == '%' ? "modulo" : "division") + " by zero");
    }
    return r;
}

Value Interp::check_number(Ctx& c, const Value& v, SrcLoc l) {
    (void)c;
    if (v.kind != Kind::Number && v.kind != Kind::State) {
        diag(DiagKind::NonNumberArithmetic, l, "arithmetic on a token/scope value");
    }
    return v;
}

Value Interp::eval(Ctx& c, const Operand& op, SrcLoc l) {
    switch (op.type) {
    case Operand::Type::Literal: return op.literal;
    case Operand::Type::Ref: return read(c, op.ref, l);
    case Operand::Type::Scope: {
        const ScopeRef s = resolve_step(c, op.scope, c.self(), l);
        return s.valid() ? Value::scope(s) : Value{};
    }
    case Operand::Type::Expr: return Value::number(eval_math(c, *op.expr));
    case Operand::Type::Random: return Value::number(w_.rng.unit());
    }
    return {};
}

ScopeRef Interp::resolve_step(Ctx& c, const Step& s, ScopeRef acc, SrcLoc l) {
    auto invalid = [&](const std::string& what) {
        diag(DiagKind::InvalidScope, l, what);
        return ScopeRef::none();
    };
    switch (s.kind) {
    case StepKind::This: return c.self();
    case StepKind::Root: return c.root;
    case StepKind::From: return c.from.valid() ? c.from : invalid("FROM is not set here");
    case StepKind::Prev: {
        const ScopeRef p = c.prev(s.prev_depth);
        return p.valid() ? p : invalid("PREV is not set here");
    }
    case StepKind::Tag: {
        const int32_t idx = w_.country_index(s.name);
        return idx >= 0 ? ScopeRef::country(idx) : invalid("unknown tag " + sym_str(s.name));
    }
    case StepKind::StateId:
        if (w_.state(s.number)) return ScopeRef::state(s.number);
        diag(DiagKind::OutOfSubset, l, "state " + std::to_string(s.number) + " is not loaded in this scenario");
        return ScopeRef::none();
    case StepKind::Controller:
    case StepKind::Owner: {
        if (acc.is_state()) {
            const world::State* st = w_.state(acc.id);
            const int32_t ci = st ? (s.kind == StepKind::Controller ? st->controller : st->owner) : -1;
            return ci >= 0 ? ScopeRef::country(ci) : invalid("state has no owner/controller");
        }
        if (acc.is_country()) {
            // Countries have no built-in controller/owner link; the eco stores `controller = THIS` as a variable.
            const Sym name = intern(s.kind == StepKind::Controller ? "controller" : "owner");
            world::VarStore* vs = w_.vars(acc);
            const Value* v = vs ? vs->find(world::make_key(name)) : nullptr;
            if (v) {
                const ScopeRef r = v->as_scope();
                if (r.valid()) return r;
            }
            return acc;
        }
        return invalid("controller/owner of a non-scope");
    }
    case StepKind::Capital: {
        ScopeRef country = acc;
        if (acc.is_state()) {
            const world::State* st = w_.state(acc.id);
            country = st ? ScopeRef::country(st->owner) : ScopeRef::none();
        }
        const world::Country* co = country.is_country() ? w_.country(country.id) : nullptr;
        return co && w_.state(co->capital) ? ScopeRef::state(co->capital) : invalid("no capital");
    }
    case StepKind::Overlord: {
        const world::Country* co = acc.is_country() ? w_.country(acc.id) : nullptr;
        return co && co->overlord >= 0 ? ScopeRef::country(co->overlord) : invalid("no overlord");
    }
    case StepKind::Named: {
        // A variable holding a scope: temp first (unscoped), then the accumulated scope's variables.
        const world::VarKey k = world::make_key(s.name);
        const Value* v = nullptr;
        if (c.temps && acc == c.self()) {
            v = c.temps->find(k);
        }
        if (!v) {
            if (world::VarStore* vs = w_.vars(acc)) {
                v = vs->find(k);
            }
        }
        if (!v) {
            // HOI4 game variables that hold scopes: state controller/owner, capital, overlord.
            const std::string_view nm = sym_view(s.name);
            Step builtin;
            builtin.kind = nm == "controller" ? StepKind::Controller : nm == "owner" ? StepKind::Owner
                         : (nm == "capital" || nm == "capital_scope") ? StepKind::Capital
                         : nm == "overlord" ? StepKind::Overlord : StepKind::Named;
            if (builtin.kind != StepKind::Named) return resolve_step(c, builtin, acc, l);
            diag(DiagKind::ScopeUnset, l, "variable '" + sym_str(s.name) + "' holding a scope is not set");
            return ScopeRef::none();
        }
        const ScopeRef r = v->as_scope();
        if (!r.valid() && v->num.is_zero()) {
            diag(DiagKind::ScopeUnset, l, "'" + sym_str(s.name) + "' holds 0 (no scope)");
            return ScopeRef::none();
        }
        return r.valid() && w_.scope_exists(r) ? r : invalid("'" + sym_str(s.name) + "' does not hold a valid scope");
    }
    }
    return ScopeRef::none();
}

bool Interp::resolve_path(Ctx& c, const VarRef& ref, ScopeRef& out, SrcLoc l) {
    if (ref.global) {
        out = ScopeRef::global();
        return true;
    }
    ScopeRef acc = c.self();
    for (const Step& s : ref.path) {
        acc = resolve_step(c, s, acc, l);
        if (!acc.valid()) {
            return false;
        }
    }
    out = acc;
    return true;
}

uint32_t Interp::target_code(Ctx& c, const VarRef& ref, SrcLoc l) {
    const Target& t = ref.target;
    switch (t.kind) {
    case TargetKind::None: return 0;
    case TargetKind::Token: return world::target_code_of(Value::token(t.sym));
    case TargetKind::Var: return world::target_code_of(read(c, *t.var, l));
    case TargetKind::Number: return world::target_code_of(Value::integer(t.number));
    case TargetKind::Scope: {
        const ScopeRef s = resolve_step(c, t.step, c.self(), l);
        return world::target_code_of(Value::scope(s));
    }
    case TargetKind::Bare: {
        const bool known_token = hooks.is_token && hooks.is_token(t.sym);
        if (!known_token) {
            diag(DiagKind::AmbiguousAtTarget, l,
                 "'" + ref.text + "': bare @" + sym_str(t.sym) + " is not a known token (read as a variable)");
        }
        if (sem_.bare_at_target == BareAtTarget::Literal ||
            (sem_.bare_at_target == BareAtTarget::TokenIfKnown && known_token)) {
            return world::target_code_of(Value::token(t.sym));
        }
        VarRef inner;
        inner.name = t.sym;
        inner.text = sym_str(t.sym);
        return world::target_code_of(read(c, inner, l));
    }
    }
    return 0;
}

namespace {

int64_t index_value(Interp& in, Ctx& c, const Index& idx, SrcLoc l) {
    if (idx.kind == IndexKind::Int) return idx.i;
    if (idx.kind == IndexKind::Var) return in.read(c, *idx.var, l).num.whole_trunc();
    return 0;
}

// Built-in (engine) arrays readable as `owned_states`, `global.countries`, `researched_techs`, ...
bool builtin_array(Ctx& c, ScopeRef scope, Sym name, std::vector<Value>& out) {
    static const Sym s_owned = intern("owned_states"), s_controlled = intern("controlled_states"),
                     s_core = intern("core_states"), s_countries = intern("countries"),
                     s_states = intern("states"), s_techs = intern("researched_techs"),
                     s_subjects = intern("subjects"), s_owned_controlled = intern("owned_controlled_states");
    world::World& w = c.w;
    if (scope.type == ScopeType::Global) {
        if (name == s_countries) {
            for (int32_t i : w.existing_countries()) out.push_back(Value::country(i));
            return true;
        }
        if (name == s_states) {
            for (int32_t i : w.existing_states()) out.push_back(Value::state(i));
            return true;
        }
        return false;
    }
    const world::Country* co = scope.is_country() ? w.country(scope.id) : nullptr;
    if (!co) return false;
    if (name == s_owned || name == s_controlled || name == s_core || name == s_owned_controlled) {
        const std::vector<int32_t>& src = name == s_owned ? co->owned_states
                                          : name == s_core ? co->core_states
                                                           : co->controlled_states;
        for (int32_t id : src) {
            if (name == s_owned_controlled && (!w.state(id) || w.state(id)->owner != co->index)) continue;
            out.push_back(Value::state(id));
        }
        return true;
    }
    if (name == s_techs) {
        for (Sym t : co->tech_order) out.push_back(Value::token(t));
        return true;
    }
    if (name == s_subjects) {
        for (int32_t s : co->subjects) out.push_back(Value::country(s));
        return true;
    }
    return false;
}

thread_local std::deque<std::vector<Value>> g_builtin_array_scratch;

} // namespace

std::vector<Value>* Interp::array(Ctx& c, const VarRef& ref, bool temp, bool create, SrcLoc l) {
    const uint32_t code = target_code(c, ref, l);
    const world::VarKey key = world::make_key(ref.name, code);
    if (temp && ref.is_plain_name()) {
        return create ? &c.temps->array(key) : c.temps->array_or_null(key);
    }
    if (temp) {
        diag(DiagKind::TempWithScope, l, "temp array effect on scoped name '" + ref.text + "' (uses the scoped array)");
    }
    ScopeRef scope;
    if (!resolve_path(c, ref, scope, l)) {
        return nullptr;
    }
    world::VarStore* vs = w_.vars(scope);
    if (ref.is_plain_name()) {
        if (auto* ta = c.temps->array_or_null(key)) {
            auto* sa = vs ? vs->array_or_null(key) : nullptr;
            if (sa) {
                diag(DiagKind::TempShadowsVar, l, "array '" + ref.text + "' exists as a temp array AND an array of " +
                                                      w_.scope_name(scope) + " (temp size " + std::to_string(ta->size()) +
                                                      ", scope size " + std::to_string(sa->size()) + ")");
                return sem_.temp_precedence == TempPrecedence::TempFirst ? ta : sa;
            }
            return ta;
        }
    }
    if (!vs) {
        return nullptr;
    }
    if (auto* a = vs->array_or_null(key)) {
        return a;
    }
    if (!create && code == 0) {
        std::vector<Value> tmp;
        if (builtin_array(c, scope, ref.name, tmp)) {
            if (g_builtin_array_scratch.size() > 64) g_builtin_array_scratch.pop_front();
            g_builtin_array_scratch.push_back(std::move(tmp));
            return &g_builtin_array_scratch.back();
        }
    }
    return create ? &vs->array(key) : nullptr;
}

const Value* Interp::find_ghost(Ctx& c, const VarRef& ref, int64_t index, SrcLoc l) {
    const world::VarKey key = world::make_key(ref.name, target_code(c, ref, l));
    if (ref.is_plain_name() && c.temps) {
        if (const Value* g = c.temps->ghost(key, index)) return g;
    }
    ScopeRef scope;
    if (!resolve_path(c, ref, scope, l)) return nullptr;
    world::VarStore* vs = w_.vars(scope);
    return vs ? vs->ghost(key, index) : nullptr;
}

void Interp::clear_ghosts(Ctx& c, const VarRef& ref, bool temp, SrcLoc l) {
    const world::VarKey key = world::make_key(ref.name, target_code(c, ref, l));
    if (ref.is_plain_name() && c.temps) c.temps->clear_ghosts(key);
    if (temp && ref.is_plain_name()) return;
    ScopeRef scope;
    if (!resolve_path(c, ref, scope, l)) return;
    if (world::VarStore* vs = w_.vars(scope)) vs->clear_ghosts(key);
}

Value Interp::read(Ctx& c, const VarRef& ref, SrcLoc l) {
    // Arrays: arr^num / arr^i
    if (ref.index.kind != IndexKind::None) {
        const std::vector<Value>* arr = array(c, ref, false, false, l);
        if (ref.index.kind == IndexKind::Num) {
            return Value::integer(arr ? static_cast<int64_t>(arr->size()) : 0);
        }
        const int64_t i = index_value(*this, c, ref.index, l);
        if (arr && i >= 0 && i < static_cast<int64_t>(arr->size())) {
            return (*arr)[static_cast<size_t>(i)];
        }
        if (i >= 0) {
            if (const Value* g = find_ghost(c, ref, i, l)) return *g;
        }
        diag(DiagKind::ArrayIndex, l, "'" + ref.text + "' index " + std::to_string(i) + " out of range (size " +
                                          std::to_string(arr ? arr->size() : 0) + ")");
        return ref.has_default ? Value::number(ref.default_value) : Value{};
    }

    const uint32_t code = (reg_.game_vars.count(ref.name) && reg_.game_vars.at(ref.name).target_is_name) ? 0 : target_code(c, ref, l);
    const world::VarKey key = world::make_key(ref.name, code);

    ScopeRef scope;
    const Value* temp_hit = (ref.is_plain_name() && c.temps) ? c.temps->find(key) : nullptr;
    if (temp_hit) {
        // Precedence between a temp and a same-named scope variable is unverified: report every collision.
        world::VarStore* vs = w_.vars(c.self());
        const Value* scope_hit = vs ? vs->find(key) : nullptr;
        if (scope_hit) {
            diag(DiagKind::TempShadowsVar, l, "'" + ref.text + "' exists as a temp AND a variable of " +
                                                  w_.scope_name(c.self()) + " (temp " + temp_hit->num.to_string_fixed(3) +
                                                  ", var " + scope_hit->num.to_string_fixed(3) + ")");
            return sem_.temp_precedence == TempPrecedence::TempFirst ? *temp_hit : *scope_hit;
        }
        if (sem_.temp_precedence == TempPrecedence::TempFirst) return *temp_hit;
    }

    if (!resolve_path(c, ref, scope, l)) {
        return ref.has_default ? Value::number(ref.default_value) : Value{};
    }

    auto gv = reg_.game_vars.find(ref.name);
    if (gv != reg_.game_vars.end()) {
        return gv->second.fn(c, scope, ref, l);
    }

    if (world::VarStore* vs = w_.vars(scope)) {
        if (const Value* v = vs->find(key)) {
            return *v;
        }
    }
    if (ref.is_plain_name() && c.temps && sem_.temp_precedence == TempPrecedence::ScopeFirst) {
        if (const Value* v = c.temps->find(key)) {
            return *v;
        }
    }
    return ref.has_default ? Value::number(ref.default_value) : Value{};
}

bool Interp::has(Ctx& c, const VarRef& ref, SrcLoc l) {
    const world::VarKey key = world::make_key(ref.name, target_code(c, ref, l));
    if (ref.is_plain_name() && c.temps && c.temps->has(key)) {
        return true;
    }
    ScopeRef scope;
    if (!resolve_path(c, ref, scope, l)) {
        return false;
    }
    world::VarStore* vs = w_.vars(scope);
    return vs && (vs->has(key) || vs->array_or_null(key) != nullptr);
}

bool Interp::resolve_slot(Ctx& c, const VarRef& ref, bool temp, SrcLoc l, Slot& out) {
    out.key = world::make_key(ref.name, target_code(c, ref, l));
    if (temp && ref.is_plain_name()) {
        out.store = c.temps;
        out.is_temp = true;
    } else {
        if (temp) {
            diag(DiagKind::TempWithScope, l, "temp variable effect on scoped name '" + ref.text + "' (uses the scoped variable)");
        }
        ScopeRef scope;
        if (!resolve_path(c, ref, scope, l)) {
            return false;
        }
        out.store = w_.vars(scope);
        if (!out.store) {
            diag(DiagKind::InvalidScope, l, "cannot write '" + ref.text + "' in scope " + w_.scope_name(scope));
            return false;
        }
    }
    if (ref.index.kind != IndexKind::None) {
        if (ref.index.kind == IndexKind::Num) {
            diag(DiagKind::BadParameter, l, "cannot assign to '" + ref.text + "' (^num)");
            return false;
        }
        out.is_array_element = true;
        out.element = index_value(*this, c, ref.index, l);
    }
    return true;
}

Value Interp::slot_get(const Slot& s) const {
    if (!s.store) return {};
    if (s.is_array_element) {
        const std::vector<Value>* a = s.store->array_or_null(s.key);
        if (a && s.element >= 0 && s.element < static_cast<int64_t>(a->size())) return (*a)[static_cast<size_t>(s.element)];
        const Value* g = s.element >= 0 ? s.store->ghost(s.key, s.element) : nullptr;
        return g ? *g : Value{};
    }
    return s.store->get(s.key);
}

void Interp::slot_set(Ctx& c, const Slot& s, const Value& v, SrcLoc l) {
    (void)c;
    if (!s.store) return;
    if (s.is_array_element) {
        std::vector<Value>* a = s.store->array_or_null(s.key);
        if (s.element < 0) {
            diag(DiagKind::ArrayIndex, l, "array element write at negative index " + std::to_string(s.element));
            return;
        }
        if (a && s.element < static_cast<int64_t>(a->size())) {
            (*a)[static_cast<size_t>(s.element)] = v;
            return;
        }
        diag(DiagKind::ArrayGhostWrite, l, "array element " + std::to_string(s.element) + " written past ^num (" +
                                               std::to_string(a ? a->size() : 0) + "); kept as a standalone element");
        s.store->set_ghost(s.key, s.element, v);
        return;
    }
    s.store->set(s.key, v);
}

} // namespace pdx
