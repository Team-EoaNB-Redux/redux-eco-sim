#pragma once

// Per-scope variable + array storage. Keys are (name symbol, @-target code) packed into 64 bits, so
// `pops@var:t` becomes an integer lookup with no string building at runtime.
//
// Target code (low 32 bits): 0 = no target, else kind in the top 2 bits + 30-bit payload:
//   1 = token (payload = token sym), 2 = country (payload = index), 3 = state/number (payload = integer).
// HOI4 `x@5` and `x@<state 5>` therefore name the same variable, which matches the engine treating the
// target as text appended to the name.

#include "pdx/value.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace world {

using pdx::Sym;
using pdx::Value;

struct VarKey {
    uint64_t v = 0;
    friend bool operator==(VarKey a, VarKey b) { return a.v == b.v; }
};

inline uint32_t target_code_of(const Value& t) {
    constexpr uint32_t payload_mask = (1u << 30) - 1u;
    switch (t.kind) {
    case pdx::Kind::Token: return (1u << 30) | (static_cast<uint32_t>(t.token_sym()) & payload_mask);
    case pdx::Kind::Country: return (2u << 30) | (static_cast<uint32_t>(t.id()) & payload_mask);
    case pdx::Kind::State:
    case pdx::Kind::Number:
    default: return (3u << 30) | (static_cast<uint32_t>(t.id()) & payload_mask);
    }
}

inline VarKey make_key(Sym name, uint32_t target_code = 0) {
    return VarKey{(static_cast<uint64_t>(name) << 32) | target_code};
}
inline Sym key_name(VarKey k) { return static_cast<Sym>(k.v >> 32); }
inline uint32_t key_target(VarKey k) { return static_cast<uint32_t>(k.v & 0xFFFFFFFFu); }

struct VarKeyHash {
    size_t operator()(VarKey k) const noexcept {
        uint64_t x = k.v;
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        return static_cast<size_t>(x);
    }
};

class VarStore {
public:
    // Variables: unset reads 0 (Value{}), `has` distinguishes set-to-0 from unset.
    const Value* find(VarKey k) const {
        auto it = vars_.find(k);
        return it == vars_.end() ? nullptr : &it->second;
    }
    Value get(VarKey k) const {
        const Value* v = find(k);
        return v ? *v : Value{};
    }
    bool has(VarKey k) const { return vars_.count(k) != 0; }
    void set(VarKey k, const Value& v) {
        if (!pins_.empty()) {
            auto p = pins_.find(k);
            if (p != pins_.end()) {
                vars_[k] = p->second;
                return;
            }
        }
        vars_[k] = v;
    }
    void erase(VarKey k) {
        if (!pins_.empty() && pins_.count(k)) return;
        vars_.erase(k);
    }
    // Scenario `pins`: the variable keeps this value whatever script writes (stand-in for unmodelled inputs,
    // e.g. the startup trade of a subset scenario).
    void pin(VarKey k, const Value& v) {
        pins_[k] = v;
        vars_[k] = v;
    }
    bool pinned(VarKey k) const { return pins_.count(k) != 0; }

    // Arrays. `array_or_null` returns nullptr for a never-created array (reads as empty, ^num = 0).
    std::vector<Value>* array_or_null(VarKey k) {
        auto it = arrays_.find(k);
        return it == arrays_.end() ? nullptr : &it->second;
    }
    const std::vector<Value>* array_or_null(VarKey k) const {
        auto it = arrays_.find(k);
        return it == arrays_.end() ? nullptr : &it->second;
    }
    std::vector<Value>& array(VarKey k) { return arrays_[k]; }
    void erase_array(VarKey k) { arrays_.erase(k); }

    // HOI4 arrays are really variables arr^0, arr^1, ... plus a separate arr^num. Writing arr^i with
    // i >= ^num stores the element without changing ^num, and reading it back works (Strat, in-game).
    // Those "ghost" elements live here; only indices >= the array's current size are consulted.
    const Value* ghost(VarKey k, int64_t i) const {
        auto it = ghosts_.find(k);
        if (it == ghosts_.end()) return nullptr;
        auto e = it->second.find(i);
        return e == it->second.end() ? nullptr : &e->second;
    }
    void set_ghost(VarKey k, int64_t i, const Value& v) { ghosts_[k][i] = v; }
    void clear_ghosts(VarKey k) { ghosts_.erase(k); }

    void clear() {
        vars_.clear();
        arrays_.clear();
        ghosts_.clear();
        pins_.clear();
    }
    const std::unordered_map<VarKey, Value, VarKeyHash>& vars() const { return vars_; }
    const std::unordered_map<VarKey, std::vector<Value>, VarKeyHash>& arrays() const { return arrays_; }

private:
    std::unordered_map<VarKey, Value, VarKeyHash> vars_;
    std::unordered_map<VarKey, std::vector<Value>, VarKeyHash> arrays_;
    std::unordered_map<VarKey, std::unordered_map<int64_t, Value>, VarKeyHash> ghosts_;
    std::unordered_map<VarKey, Value, VarKeyHash> pins_;
};

} // namespace world
