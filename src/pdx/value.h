#pragma once

// Script values and scope handles.
//
// HOI4 stores tokens and scopes in variables as flagged raw integers in the fixed-point slot. We mirror that
// with a Kind tag (plus the scope index in `ref`) next to the number. The NUMERIC value of a non-number is
// what comparisons, arithmetic and [?x] see (VERIFIED in-game, calibration probes):
//   Country -> raw 2^30 + tag bytes little-endian ('E' | 'N'<<8 | 'G'<<16): ENG reads 10784.14917.
//              The eco relies on countries comparing > 10,500 (companies' financier encoding).
//   State   -> raw id - 2^30: state 1 reads -10737.41823, London (952) -10737.40872
//   Token   -> its symbol id as a whole number (in-game it is a different table index: not reproducible)
// Equality compares the numeric value only (like comparing two flagged ints).

#include "pdx/fixed.h"
#include "pdx/symbols.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace pdx {

enum class Kind : uint8_t { Number = 0, Token = 1, Country = 2, State = 3 };

inline constexpr int64_t kScopeRawBase = int64_t{1} << 30;

// Country index -> tag code (filled by World::ensure_country; the tag order comes from the mod files, so it
// is the same for every World in the process).
inline std::vector<uint32_t>& country_code_table() {
    static std::vector<uint32_t> t;
    return t;
}
inline uint32_t tag_code(std::string_view tag) {
    uint32_t code = 0;
    for (size_t i = 0; i < tag.size() && i < 3; ++i) code |= static_cast<uint32_t>(static_cast<unsigned char>(tag[i])) << (8 * i);
    return code;
}
inline void register_country_code(int32_t idx, uint32_t code) {
    auto& t = country_code_table();
    if (idx >= static_cast<int32_t>(t.size())) t.resize(static_cast<size_t>(idx) + 1, 0);
    t[static_cast<size_t>(idx)] = code;
}
inline int32_t country_index_of_code(uint32_t code) {
    const auto& t = country_code_table();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == code) return static_cast<int32_t>(i);
    return -1;
}

enum class ScopeType : uint8_t { None = 0, Global, Country, State };

struct ScopeRef {
    ScopeType type = ScopeType::None;
    int32_t id = -1; // country index or state id

    static constexpr ScopeRef none() { return {}; }
    static constexpr ScopeRef global() { return {ScopeType::Global, 0}; }
    static constexpr ScopeRef country(int32_t idx) { return {ScopeType::Country, idx}; }
    static constexpr ScopeRef state(int32_t id) { return {ScopeType::State, id}; }
    constexpr bool valid() const { return type != ScopeType::None; }
    constexpr bool is_country() const { return type == ScopeType::Country; }
    constexpr bool is_state() const { return type == ScopeType::State; }
    friend constexpr bool operator==(ScopeRef a, ScopeRef b) { return a.type == b.type && a.id == b.id; }
    friend constexpr bool operator!=(ScopeRef a, ScopeRef b) { return !(a == b); }
};

struct Value {
    Fixed num;
    Kind kind = Kind::Number;
    int32_t ref = -1; // country index / state id for scope kinds

    static Value number(Fixed f) { return {f, Kind::Number}; }
    static Value integer(int64_t v) { return {Fixed::from_int(v), Kind::Number}; }
    static Value token(Sym s) { return {Fixed::from_int(static_cast<int64_t>(s)), Kind::Token}; }
    static Value country(int32_t idx) {
        const auto& t = country_code_table();
        const uint32_t code = idx >= 0 && idx < static_cast<int32_t>(t.size()) ? t[static_cast<size_t>(idx)] : 0;
        return {Fixed::from_raw(kScopeRawBase + code), Kind::Country, idx};
    }
    static Value state(int32_t id) { return {Fixed::from_raw(id - kScopeRawBase), Kind::State, id}; }
    static Value scope(ScopeRef s) {
        if (s.is_country()) return country(s.id);
        if (s.is_state()) return state(s.id);
        return {};
    }

    bool is_number() const { return kind == Kind::Number; }
    Sym token_sym() const { return static_cast<Sym>(num.whole_trunc()); }
    int64_t id() const { return kind == Kind::Country || kind == Kind::State ? ref : num.whole_trunc(); }

    // Interprets the value as a scope. Plain integer numbers are accepted as state ids (`var:x` on a number).
    ScopeRef as_scope() const {
        switch (kind) {
        case Kind::Country: return ScopeRef::country(ref);
        case Kind::State: return ScopeRef::state(ref);
        case Kind::Number: {
            // a plain id (`var:x` holding 952), or a scope value that lost its kind (math expression copies)
            const int64_t r = num.raw();
            if (r % Fixed::SCALE == 0 && r > 0) return ScopeRef::state(static_cast<int32_t>(num.whole_trunc()));
            if (r > -kScopeRawBase && r < -kScopeRawBase + (1 << 20)) return ScopeRef::state(static_cast<int32_t>(r + kScopeRawBase));
            if (r >= kScopeRawBase && r < kScopeRawBase + (1 << 24)) {
                const int32_t ci = country_index_of_code(static_cast<uint32_t>(r - kScopeRawBase));
                if (ci >= 0) return ScopeRef::country(ci);
            }
            return ScopeRef::none();
        }
        case Kind::Token:
        default: return ScopeRef::none();
        }
    }

    friend bool operator==(const Value& a, const Value& b) { return a.num == b.num; }
    friend bool operator!=(const Value& a, const Value& b) { return a.num != b.num; }
};

} // namespace pdx
