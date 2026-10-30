#pragma once

// Helpers shared by builtin implementations.

#include "pdx/interp.h"

#include <initializer_list>
#include <string_view>

namespace pdx::bi {

inline SrcLoc loc_of(Interp& in, const ScriptFile& f, const Node& n) { return in.loc(f, n); }

inline bool key_is(const Node& n, std::string_view k) { return sym_view(n.key) == k; }

inline const Node* find_child(const ScriptFile& f, const Node& block, std::string_view key) {
    if (!block.is_block) return nullptr;
    for (const Node* c = f.begin(block); c != f.end(block); ++c) {
        if (sym_view(c->key) == key) return c;
    }
    return nullptr;
}

// Compiles the effects of a block, skipping parameter keys (limit, tooltip, ...).
inline EffectBlock compile_effects_except(Interp& in, const ScriptFile& f, const Node& block,
                                          std::initializer_list<std::string_view> skip) {
    EffectBlock out;
    if (!block.is_block) return out;
    const Node* end = f.end(block);
    for (const Node* n = f.begin(block); n != end; ++n) {
        bool skipped = false;
        for (auto s : skip) skipped |= sym_view(n->key) == s;
        if (skipped) continue;
        const Node* consumed = n;
        if (auto op = in.compile_effect(f, *n, end, &consumed)) out.push_back(std::move(op));
        n = consumed;
    }
    return out;
}

inline TriggerBlock compile_triggers_except(Interp& in, const ScriptFile& f, const Node& block,
                                            std::initializer_list<std::string_view> skip) {
    TriggerBlock out;
    if (!block.is_block) return out;
    const Node* end = f.end(block);
    for (const Node* n = f.begin(block); n != end; ++n) {
        bool skipped = false;
        for (auto s : skip) skipped |= sym_view(n->key) == s;
        if (skipped) continue;
        const Node* consumed = n;
        if (auto op = in.compile_trigger(f, *n, end, &consumed)) out.push_back(std::move(op));
        n = consumed;
    }
    return out;
}

// Reads a temp-variable NAME parameter (value = v, index = i, break = b). Returns `fallback` if absent.
inline Sym temp_name_param(const ScriptFile& f, const Node& block, std::string_view key, const char* fallback) {
    const Node* n = find_child(f, block, key);
    return n && !n->is_block ? n->value : (fallback ? intern(fallback) : kNoSym);
}

inline void set_temp(Ctx& c, Sym name, const Value& v) {
    if (name != kNoSym && c.temps) c.temps->set(world::make_key(name), v);
}

inline ScopeRef country_of(Ctx& c, ScopeRef s) {
    if (s.is_country()) return s;
    if (s.is_state()) {
        const world::State* st = c.w.state(s.id);
        return st && st->owner >= 0 ? ScopeRef::country(st->owner) : ScopeRef::none();
    }
    return ScopeRef::none();
}

// Runs `body` with a loop break temp active (if `brk` is set).
struct BreakGuard {
    Ctx& c;
    bool active;
    // The break temp is reset to 0 when the loop starts: the eco reuses `b` across loops
    // (calculate_standard_of_living's per-pop lookup only gives the in-game SoLs if a stale b = 1 is ignored).
    BreakGuard(Ctx& ctx, Sym brk) : c(ctx), active(brk != kNoSym) {
        if (active) {
            set_temp(c, brk, Value{});
            c.break_stack.push_back(brk);
        }
    }
    ~BreakGuard() {
        if (active) c.break_stack.pop_back();
    }
};

} // namespace pdx::bi
