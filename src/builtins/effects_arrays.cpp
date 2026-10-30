// Array effects + array triggers (is_in_array, any_of, all_of, any_of_scopes, all_of_scopes).
// Arrays are 0-indexed; arr^num is the size; elements must exist before being edited (wiki).

#include "builtins/common.h"

#include <algorithm>

namespace pdx {

namespace {

using sim::DiagKind;

// ---- add_to_array ----

struct AddToArray : EffectOp {
    bool temp = false;
    VarRef array;
    Operand value;
    bool has_index = false;
    Operand index;
    void run(Ctx& c) const override {
        std::vector<Value>* a = c.in.array(c, array, temp, true, loc);
        if (!a) return;
        const Value v = c.in.eval(c, value, loc);
        if (has_index) {
            const int64_t i = c.in.eval(c, index, loc).num.whole_trunc();
            if (i < 0 || i > static_cast<int64_t>(a->size())) {
                c.in.diag(DiagKind::ArrayIndex, loc, "add_to_array index " + std::to_string(i) + " out of range");
                a->push_back(v);
                return;
            }
            a->insert(a->begin() + i, v);
            return;
        }
        a->push_back(v);
    }
};

std::unique_ptr<EffectOp> make_add_to_array(Interp& in, const ScriptFile& f, const Node& n, bool temp) {
    auto e = std::make_unique<AddToArray>();
    e->temp = temp;
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) {
        in.diag(DiagKind::BadParameter, l, "add_to_array needs a { } block");
        return nullptr;
    }
    if (const Node* arr = bi::find_child(f, n, "array")) {
        if (!in.compile_ref(sym_view(arr->value), l, e->array)) return nullptr;
        const Node* v = bi::find_child(f, n, "value");
        if (!v || !in.compile_operand(f, *v, e->value)) {
            in.diag(DiagKind::BadParameter, l, "add_to_array without value");
            return nullptr;
        }
        if (const Node* idx = bi::find_child(f, n, "index")) {
            e->has_index = in.compile_operand(f, *idx, e->index);
        }
        return e;
    }
    const Node* pair = f.begin(n);
    if (pair == f.end(n) || pair->key == kNoSym) {
        in.diag(DiagKind::BadParameter, l, "add_to_array has no array");
        return nullptr;
    }
    if (!in.compile_ref(sym_view(pair->key), l, e->array)) return nullptr;
    if (!in.compile_operand(f, *pair, e->value)) return nullptr;
    return e;
}

// ---- remove_from_array ----

struct RemoveFromArray : EffectOp {
    bool temp = false;
    VarRef array;
    enum class Mode : uint8_t { Last, Value, Index } mode = Mode::Last;
    Operand arg;
    void run(Ctx& c) const override {
        std::vector<Value>* a = c.in.array(c, array, temp, false, loc);
        if (!a || a->empty()) return;
        switch (mode) {
        case Mode::Last: a->pop_back(); return;
        case Mode::Index: {
            const int64_t i = c.in.eval(c, arg, loc).num.whole_trunc();
            if (i < 0 || i >= static_cast<int64_t>(a->size())) {
                c.in.diag(DiagKind::ArrayIndex, loc, "remove_from_array index " + std::to_string(i) + " out of range");
                return;
            }
            a->erase(a->begin() + i);
            return;
        }
        case Mode::Value: {
            const Value v = c.in.eval(c, arg, loc);
            auto it = std::find(a->begin(), a->end(), v);
            if (it != a->end()) a->erase(it);
            return;
        }
        }
    }
};

std::unique_ptr<EffectOp> make_remove_from_array(Interp& in, const ScriptFile& f, const Node& n, bool temp) {
    auto e = std::make_unique<RemoveFromArray>();
    e->temp = temp;
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) {
        if (!in.compile_ref(sym_view(n.value), l, e->array)) return nullptr;
        return e;
    }
    if (const Node* arr = bi::find_child(f, n, "array")) {
        if (!in.compile_ref(sym_view(arr->value), l, e->array)) return nullptr;
        if (const Node* idx = bi::find_child(f, n, "index")) {
            e->mode = RemoveFromArray::Mode::Index;
            in.compile_operand(f, *idx, e->arg);
        } else if (const Node* v = bi::find_child(f, n, "value")) {
            e->mode = RemoveFromArray::Mode::Value;
            in.compile_operand(f, *v, e->arg);
        }
        return e;
    }
    const Node* pair = f.begin(n);
    if (pair == f.end(n) || pair->key == kNoSym) return nullptr;
    if (!in.compile_ref(sym_view(pair->key), l, e->array)) return nullptr;
    e->mode = RemoveFromArray::Mode::Value;
    in.compile_operand(f, *pair, e->arg);
    return e;
}

// ---- clear_array ----

struct ClearArray : EffectOp {
    bool temp = false;
    VarRef array;
    void run(Ctx& c) const override {
        if (std::vector<Value>* a = c.in.array(c, array, temp, false, loc)) a->clear();
        c.in.clear_ghosts(c, array, temp, loc);
    }
};

std::unique_ptr<EffectOp> make_clear_array(Interp& in, const ScriptFile& f, const Node& n, bool temp) {
    auto e = std::make_unique<ClearArray>();
    e->temp = temp;
    const SrcLoc l = in.loc(f, n);
    std::string_view name = sym_view(n.value);
    if (n.is_block) {
        const Node* arr = bi::find_child(f, n, "array");
        if (!arr) arr = f.begin(n) != f.end(n) ? f.begin(n) : nullptr;
        if (!arr) return nullptr;
        name = sym_view(arr->key == kNoSym || sym_view(arr->key) == "array" ? arr->value : arr->key);
    }
    if (!in.compile_ref(name, l, e->array)) return nullptr;
    return e;
}

// ---- resize_array ----

struct ResizeArray : EffectOp {
    bool temp = false;
    VarRef array;
    Operand size;
    bool has_value = false;
    Operand value;
    void run(Ctx& c) const override {
        std::vector<Value>* a = c.in.array(c, array, temp, true, loc);
        if (!a) return;
        int64_t sz = c.in.eval(c, size, loc).num.whole_trunc();
        if (sz < 0) sz = 0;
        const Value fill = has_value ? c.in.eval(c, value, loc) : Value{};
        a->resize(static_cast<size_t>(sz), fill);
    }
};

std::unique_ptr<EffectOp> make_resize_array(Interp& in, const ScriptFile& f, const Node& n, bool temp) {
    auto e = std::make_unique<ResizeArray>();
    e->temp = temp;
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) return nullptr;
    if (const Node* arr = bi::find_child(f, n, "array")) {
        if (!in.compile_ref(sym_view(arr->value), l, e->array)) return nullptr;
        const Node* sz = bi::find_child(f, n, "size");
        if (!sz || !in.compile_operand(f, *sz, e->size)) {
            in.diag(DiagKind::BadParameter, l, "resize_array without size");
            return nullptr;
        }
        if (const Node* v = bi::find_child(f, n, "value")) e->has_value = in.compile_operand(f, *v, e->value);
        return e;
    }
    const Node* pair = f.begin(n);
    if (pair == f.end(n) || pair->key == kNoSym) return nullptr;
    if (!in.compile_ref(sym_view(pair->key), l, e->array)) return nullptr;
    in.compile_operand(f, *pair, e->size);
    return e;
}

// ---- find_highest/lowest_in_array ----

struct FindExtreme : EffectOp {
    bool highest = true;
    VarRef array;
    Sym value_temp = kNoSym, index_temp = kNoSym;
    void run(Ctx& c) const override {
        const std::vector<Value>* a = c.in.array(c, array, false, false, loc);
        if (!a || a->empty()) return;
        size_t best = 0;
        for (size_t i = 1; i < a->size(); ++i) {
            const bool better = highest ? (*a)[i].num > (*a)[best].num : (*a)[i].num < (*a)[best].num;
            if (better) best = i;
        }
        bi::set_temp(c, value_temp, (*a)[best]);
        bi::set_temp(c, index_temp, Value::integer(static_cast<int64_t>(best)));
    }
};

std::unique_ptr<EffectOp> make_find_extreme(Interp& in, const ScriptFile& f, const Node& n, bool highest) {
    auto e = std::make_unique<FindExtreme>();
    e->highest = highest;
    const SrcLoc l = in.loc(f, n);
    const Node* arr = bi::find_child(f, n, "array");
    if (!arr || !in.compile_ref(sym_view(arr->value), l, e->array)) {
        in.diag(DiagKind::BadParameter, l, "find_highest/lowest_in_array needs array =");
        return nullptr;
    }
    e->value_temp = bi::temp_name_param(f, n, "value", nullptr);
    e->index_temp = bi::temp_name_param(f, n, "index", nullptr);
    return e;
}

// ---- triggers ----

struct IsInArray : TriggerOp {
    VarRef array;
    Operand value;
    bool eval(Ctx& c) const override {
        const std::vector<Value>* a = c.in.array(c, array, false, false, loc);
        if (!a) return false;
        const Value v = c.in.eval(c, value, loc);
        return std::find(a->begin(), a->end(), v) != a->end();
    }
};

std::unique_ptr<TriggerOp> make_is_in_array(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<IsInArray>();
    const SrcLoc l = in.loc(f, n);
    if (!n.is_block) return nullptr;
    if (const Node* arr = bi::find_child(f, n, "array")) {
        if (!in.compile_ref(sym_view(arr->value), l, t->array)) return nullptr;
        const Node* v = bi::find_child(f, n, "value");
        if (!v || !in.compile_operand(f, *v, t->value)) return nullptr;
        return t;
    }
    const Node* pair = f.begin(n);
    if (pair == f.end(n) || pair->key == kNoSym) return nullptr;
    if (!in.compile_ref(sym_view(pair->key), l, t->array)) return nullptr;
    if (!in.compile_operand(f, *pair, t->value)) return nullptr;
    return t;
}

struct AnyAllOf : TriggerOp {
    bool any = true;
    bool scopes = false;
    VarRef array;
    Sym value_temp = kNoSym, index_temp = kNoSym;
    TriggerBlock body;
    bool eval(Ctx& c) const override {
        const std::vector<Value>* a = c.in.array(c, array, false, false, loc);
        if (!a) return !any;
        const std::vector<Value> snapshot = *a;
        for (size_t i = 0; i < snapshot.size(); ++i) {
            bool r;
            if (scopes) {
                const ScopeRef s = snapshot[i].as_scope();
                if (!s.valid() || !c.w.scope_exists(s)) {
                    r = false;
                } else {
                    ScopeGuard g(c, s);
                    r = c.in.eval_and(c, body);
                }
            } else {
                bi::set_temp(c, value_temp, snapshot[i]);
                bi::set_temp(c, index_temp, Value::integer(static_cast<int64_t>(i)));
                r = c.in.eval_and(c, body);
            }
            if (any && r) return true;
            if (!any && !r) return false;
        }
        return !any;
    }
};

std::unique_ptr<TriggerOp> make_any_all_of(Interp& in, const ScriptFile& f, const Node& n, bool any, bool scopes) {
    auto t = std::make_unique<AnyAllOf>();
    t->any = any;
    t->scopes = scopes;
    const SrcLoc l = in.loc(f, n);
    const Node* arr = bi::find_child(f, n, "array");
    if (!arr || !in.compile_ref(sym_view(arr->value), l, t->array)) {
        in.diag(DiagKind::BadParameter, l, sym_str(n.key) + " needs array =");
        return nullptr;
    }
    if (!scopes) {
        t->value_temp = bi::temp_name_param(f, n, "value", "v");
        t->index_temp = bi::temp_name_param(f, n, "index", "i");
    }
    t->body = bi::compile_triggers_except(in, f, n, {"array", "value", "index", "tooltip"});
    return t;
}

#define WRAP_E(name, expr) \
    std::unique_ptr<EffectOp> name(Interp& in, const ScriptFile& f, const Node& n) { return expr; }
#define WRAP_T(name, expr) \
    std::unique_ptr<TriggerOp> name(Interp& in, const ScriptFile& f, const Node& n) { return expr; }

WRAP_E(add_to_array, make_add_to_array(in, f, n, false))
WRAP_E(add_to_temp_array, make_add_to_array(in, f, n, true))
WRAP_E(remove_from_array, make_remove_from_array(in, f, n, false))
WRAP_E(remove_from_temp_array, make_remove_from_array(in, f, n, true))
WRAP_E(clear_array, make_clear_array(in, f, n, false))
WRAP_E(clear_temp_array, make_clear_array(in, f, n, true))
WRAP_E(resize_array, make_resize_array(in, f, n, false))
WRAP_E(resize_temp_array, make_resize_array(in, f, n, true))
WRAP_E(find_highest_in_array, make_find_extreme(in, f, n, true))
WRAP_E(find_lowest_in_array, make_find_extreme(in, f, n, false))
WRAP_T(any_of, make_any_all_of(in, f, n, true, false))
WRAP_T(all_of, make_any_all_of(in, f, n, false, false))
WRAP_T(any_of_scopes, make_any_all_of(in, f, n, true, true))
WRAP_T(all_of_scopes, make_any_all_of(in, f, n, false, true))

#undef WRAP_E
#undef WRAP_T

} // namespace

void register_effects_arrays(Registry& r) {
    r.effect("add_to_array", add_to_array);
    r.effect("add_to_temp_array", add_to_temp_array);
    r.effect("remove_from_array", remove_from_array);
    r.effect("remove_from_temp_array", remove_from_temp_array);
    r.effect("clear_array", clear_array);
    r.effect("clear_temp_array", clear_temp_array);
    r.effect("resize_array", resize_array);
    r.effect("resize_temp_array", resize_temp_array);
    r.effect("find_highest_in_array", find_highest_in_array);
    r.effect("find_lowest_in_array", find_lowest_in_array);
    r.trigger("is_in_array", make_is_in_array);
    r.trigger("any_of", any_of);
    r.trigger("all_of", all_of);
    r.trigger("any_of_scopes", any_of_scopes);
    r.trigger("all_of_scopes", all_of_scopes);
}

} // namespace pdx
