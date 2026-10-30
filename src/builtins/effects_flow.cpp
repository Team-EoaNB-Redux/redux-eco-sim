// Flow-control effects: hidden_effect, loops (for_each_loop, for_each_scope_loop, for_loop_effect,
// while_loop_effect), random_list, random, log, meta_effect, tooltip no-ops.

#include "builtins/common.h"

#include "pdx/parser.h"

#include <unordered_map>

namespace pdx {

namespace {

using sim::DiagKind;

struct Block : EffectOp {
    EffectBlock body;
    void run(Ctx& c) const override { c.in.run_block(c, body); }
};

struct Nothing : EffectOp {
    void run(Ctx&) const override {}
};

std::unique_ptr<EffectOp> hidden_effect(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<Block>();
    e->body = in.compile_effects(f, n);
    return e;
}

std::unique_ptr<EffectOp> tooltip_noop(Interp&, const ScriptFile&, const Node&) {
    return std::make_unique<Nothing>();
}

// ---- for_each_loop ----

struct ForEachLoop : EffectOp {
    VarRef array;
    Sym value_temp = kNoSym, index_temp = kNoSym, brk = kNoSym;
    EffectBlock body;
    void run(Ctx& c) const override {
        bi::BreakGuard g(c, brk);
        if (c.break_requested()) return;
        // Re-read the array each iteration: elements may be appended during the loop (removal is undefined
        // in-game; we stop at the current size).
        for (size_t i = 0;; ++i) {
            const std::vector<Value>* a = c.in.array(c, array, false, false, loc);
            if (!a || i >= a->size()) break;
            bi::set_temp(c, value_temp, (*a)[i]);
            bi::set_temp(c, index_temp, Value::integer(static_cast<int64_t>(i)));
            c.in.run_block(c, body);
            if (c.break_requested()) break;
        }
    }
};

std::unique_ptr<EffectOp> for_each_loop(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<ForEachLoop>();
    const SrcLoc l = in.loc(f, n);
    const Node* arr = bi::find_child(f, n, "array");
    if (!arr || !in.compile_ref(sym_view(arr->value), l, e->array)) {
        in.diag(DiagKind::BadParameter, l, "for_each_loop needs array =");
        return nullptr;
    }
    e->value_temp = bi::temp_name_param(f, n, "value", "v");
    e->index_temp = bi::temp_name_param(f, n, "index", "i");
    e->brk = bi::temp_name_param(f, n, "break", nullptr);
    e->body = bi::compile_effects_except(in, f, n, {"array", "value", "index", "break"});
    return e;
}

// ---- for_each_scope_loop ----

struct ForEachScopeLoop : EffectOp {
    VarRef array;
    Sym brk = kNoSym;
    EffectBlock body;
    void run(Ctx& c) const override {
        const std::vector<Value>* a = c.in.array(c, array, false, false, loc);
        if (!a) return;
        const std::vector<Value> snapshot = *a;
        bi::BreakGuard g(c, brk);
        if (c.break_requested()) return;
        for (const Value& v : snapshot) {
            const ScopeRef s = v.as_scope();
            if (!s.valid() || !c.w.scope_exists(s)) {
                c.in.diag(DiagKind::InvalidScope, loc, "for_each_scope_loop element is not a valid scope");
                continue;
            }
            ScopeGuard sg(c, s);
            c.in.run_block(c, body);
            if (c.break_requested()) break;
        }
    }
};

std::unique_ptr<EffectOp> for_each_scope_loop(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<ForEachScopeLoop>();
    const SrcLoc l = in.loc(f, n);
    const Node* arr = bi::find_child(f, n, "array");
    if (!arr || !in.compile_ref(sym_view(arr->value), l, e->array)) {
        in.diag(DiagKind::BadParameter, l, "for_each_scope_loop needs array =");
        return nullptr;
    }
    e->brk = bi::temp_name_param(f, n, "break", nullptr);
    e->body = bi::compile_effects_except(in, f, n, {"array", "break", "tooltip"});
    return e;
}

// ---- for_loop_effect ----

enum class Cmp : uint8_t { Lt, Le, Gt, Ge, Eq, Ne };

bool parse_cmp(std::string_view s, Cmp& out) {
    if (s == "less_than" || s == "<") out = Cmp::Lt;
    else if (s == "less_than_or_equals" || s == "<=") out = Cmp::Le;
    else if (s == "greater_than" || s == ">") out = Cmp::Gt;
    else if (s == "greater_than_or_equals" || s == ">=") out = Cmp::Ge;
    else if (s == "equals" || s == "=") out = Cmp::Eq;
    else if (s == "not_equals" || s == "!=") out = Cmp::Ne;
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

struct ForLoopEffect : EffectOp {
    Operand start, end, add;
    bool has_start = false, has_end = false, has_add = false;
    Cmp cmp = Cmp::Lt;
    Sym value_temp = kNoSym, brk = kNoSym;
    EffectBlock body;
    void run(Ctx& c) const override {
        Interp& in = c.in;
        Fixed v = has_start ? in.eval(c, start, loc).num : Fixed{};
        const Fixed e = has_end ? in.eval(c, end, loc).num : Fixed{};
        const Fixed step = has_add ? in.eval(c, add, loc).num : Fixed::from_int(1);
        bi::BreakGuard g(c, brk);
        if (c.break_requested()) return;
        int iterations = 0;
        while (compare(v, cmp, e)) {
            if (iterations++ >= in.semantics().max_effect_iterations) {
                in.diag(DiagKind::LoopCap, loc, "for_loop_effect stopped at " + std::to_string(in.semantics().max_effect_iterations) + " iterations");
                break;
            }
            bi::set_temp(c, value_temp, Value::number(v));
            in.run_block(c, body);
            if (c.break_requested()) break;
            v = in.arith(c, '+', v, step, loc);
        }
    }
};

std::unique_ptr<EffectOp> for_loop_effect(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<ForLoopEffect>();
    const SrcLoc l = in.loc(f, n);
    if (const Node* s = bi::find_child(f, n, "start")) e->has_start = in.compile_operand(f, *s, e->start);
    if (const Node* s = bi::find_child(f, n, "end")) e->has_end = in.compile_operand(f, *s, e->end);
    if (const Node* s = bi::find_child(f, n, "add")) e->has_add = in.compile_operand(f, *s, e->add);
    if (const Node* s = bi::find_child(f, n, "compare")) {
        if (!parse_cmp(sym_view(s->value), e->cmp)) in.diag(DiagKind::BadParameter, l, "bad compare in for_loop_effect");
    }
    e->value_temp = bi::temp_name_param(f, n, "value", "v");
    e->brk = bi::temp_name_param(f, n, "break", nullptr);
    e->body = bi::compile_effects_except(in, f, n, {"start", "end", "add", "compare", "value", "break"});
    return e;
}

// ---- while_loop_effect ----

struct WhileLoopEffect : EffectOp {
    TriggerBlock limit;
    Sym brk = kNoSym;
    EffectBlock body;
    void run(Ctx& c) const override {
        Interp& in = c.in;
        bi::BreakGuard g(c, brk);
        if (c.break_requested()) return;
        int iterations = 0;
        while (in.eval_and(c, limit)) {
            if (iterations++ >= in.semantics().max_effect_iterations) {
                in.diag(DiagKind::LoopCap, loc, "while_loop_effect stopped at " + std::to_string(in.semantics().max_effect_iterations) + " iterations");
                break;
            }
            in.run_block(c, body);
            if (c.break_requested()) break;
        }
    }
};

std::unique_ptr<EffectOp> while_loop_effect(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<WhileLoopEffect>();
    if (const Node* l = bi::find_child(f, n, "limit")) e->limit = in.compile_triggers(f, *l);
    e->brk = bi::temp_name_param(f, n, "break", nullptr);
    e->body = bi::compile_effects_except(in, f, n, {"limit", "break"});
    return e;
}

// ---- random_list / random ----

struct RandomList : EffectOp {
    struct Entry {
        Fixed weight;
        std::vector<std::pair<Fixed, TriggerBlock>> factors; // modifier = { factor = x <triggers> }
        EffectBlock body;
    };
    std::vector<Entry> entries;
    void run(Ctx& c) const override {
        std::vector<int64_t> weights;
        int64_t total = 0;
        for (const auto& e : entries) {
            Fixed w = e.weight;
            for (const auto& [factor, cond] : e.factors) {
                if (c.in.eval_and(c, cond)) w = c.in.arith(c, '*', w, factor, loc);
            }
            const int64_t wi = w.raw() > 0 ? w.raw() : 0;
            weights.push_back(wi);
            total += wi;
        }
        if (total <= 0) return;
        int64_t pick = static_cast<int64_t>(c.w.rng.below(static_cast<uint64_t>(total)));
        for (size_t i = 0; i < entries.size(); ++i) {
            if (pick < weights[i]) {
                c.in.run_block(c, entries[i].body);
                return;
            }
            pick -= weights[i];
        }
    }
};

std::unique_ptr<EffectOp> random_list(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<RandomList>();
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        Fixed w;
        if (c->key == kNoSym || !Fixed::parse(sym_view(c->key), w)) continue; // seed = / log = etc.
        RandomList::Entry entry;
        entry.weight = w;
        for (const Node* x = f.begin(*c); x != f.end(*c); ++x) {
            if (bi::key_is(*x, "modifier")) {
                Fixed factor = Fixed::from_int(1);
                if (const Node* fa = bi::find_child(f, *x, "factor")) Fixed::parse(sym_view(fa->value), factor);
                entry.factors.emplace_back(factor, bi::compile_triggers_except(in, f, *x, {"factor", "add"}));
            }
        }
        entry.body = bi::compile_effects_except(in, f, *c, {"modifier"});
        e->entries.push_back(std::move(entry));
    }
    return e;
}

struct RandomEffect : EffectOp {
    Fixed chance = Fixed::from_int(100);
    EffectBlock body;
    void run(Ctx& c) const override {
        const int64_t roll = static_cast<int64_t>(c.w.rng.below(100 * Fixed::SCALE));
        if (roll < chance.raw()) c.in.run_block(c, body);
    }
};

std::unique_ptr<EffectOp> random_effect(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<RandomEffect>();
    if (const Node* ch = bi::find_child(f, n, "chance")) Fixed::parse(sym_view(ch->value), e->chance);
    e->body = bi::compile_effects_except(in, f, n, {"chance"});
    return e;
}

// ---- log ----

struct Log : EffectOp {
    std::string text;
    void run(Ctx& c) const override {
        const std::string line = c.in.format_text(c, text, loc);
        if (c.in.log_sink) c.in.log_sink(line);
    }
};

std::unique_ptr<EffectOp> log_effect(Interp&, const ScriptFile&, const Node& n) {
    auto e = std::make_unique<Log>();
    e->text = sym_str(n.value);
    return e;
}

// ---- meta_effect ----

struct MetaEffect : EffectOp {
    std::string text;
    std::vector<std::pair<std::string, std::string>> params; // [KEY] -> format string
    std::string origin;
    mutable std::unordered_map<std::string, std::shared_ptr<EffectBlock>> cache;

    void run(Ctx& c) const override {
        Interp& in = c.in;
        std::string code = text;
        for (const auto& [key, fmt] : params) {
            const std::string val = in.format_text(c, fmt, loc);
            const std::string needle = "[" + key + "]";
            for (size_t pos = code.find(needle); pos != std::string::npos; pos = code.find(needle, pos + val.size())) {
                code.replace(pos, needle.size(), val);
            }
        }
        auto it = cache.find(code);
        if (it == cache.end()) {
            ScriptFile sf;
            sf.path = origin + " (meta_effect)";
            parse_text(code, sf);
            if (!sf.errors.empty()) {
                in.diag(DiagKind::MetaEffectError, loc, "generated text does not parse: " + sf.errors.front().message + " :: " + code);
            }
            const ScriptFile* stored = in.add_file(std::move(sf));
            Node root;
            root.is_block = true;
            root.first_child = stored->root_first;
            root.child_count = stored->root_count;
            auto block = std::make_shared<EffectBlock>(in.compile_effects(*stored, root));
            it = cache.emplace(code, std::move(block)).first;
        }
        in.run_block(c, *it->second);
    }
};

std::unique_ptr<EffectOp> meta_effect(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<MetaEffect>();
    const SrcLoc l = in.loc(f, n);
    const Node* text = bi::find_child(f, n, "text");
    if (!text || !text->is_block) {
        in.diag(DiagKind::MetaEffectError, l, "meta_effect needs text = { }");
        return nullptr;
    }
    e->text = serialize_children(f, *text);
    e->origin = in.format_loc(l);
    for (const Node* c = f.begin(n); c != f.end(n); ++c) {
        if (c->key == kNoSym || bi::key_is(*c, "text") || c->is_block) continue;
        e->params.emplace_back(sym_str(c->key), sym_str(c->value));
    }
    return e;
}

} // namespace

void register_effects_flow(Registry& r) {
    r.effect("hidden_effect", hidden_effect);
    r.effect("for_each_loop", for_each_loop);
    r.effect("for_each_scope_loop", for_each_scope_loop);
    r.effect("for_loop_effect", for_loop_effect);
    r.effect("while_loop_effect", while_loop_effect);
    r.effect("random_list", random_list);
    r.effect("random", random_effect);
    r.effect("log", log_effect);
    r.effect("meta_effect", meta_effect);
    r.effect("tooltip", tooltip_noop);
    r.effect("custom_effect_tooltip", tooltip_noop);
    r.effect("effect_tooltip", tooltip_noop);
    r.effect("custom_override_tooltip", tooltip_noop);
}

} // namespace pdx
