#pragma once

// HOI4 script-variable number: decimal fixed point, 5 decimal places, integer-only math.
//
// Engine facts this models (wiki "Data structures" + Strat's spec):
//   * precision 0.00001; finer precision is truncated (also when parsing literals)
//   * whole part is a signed 32-bit integer; going past the max wraps to the min and vice versa
//   * unset variables read as 0 (a default-constructed Fixed is 0)
//
// Anything the wiki does not pin down (mul/div rounding, divide-by-zero result, overflow mode) is a knob in
// pdx/semantics.h. Ops report overflow / divide-by-zero through OpStatus so the interpreter can attach a
// file:line diagnostic; Fixed itself never logs.

#include "pdx/semantics.h"

#include <cstdint>
#include <string>
#include <string_view>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace pdx {

struct OpStatus {
    bool overflow = false;
    bool div_by_zero = false;
};

class Fixed {
public:
    static constexpr int64_t SCALE = 100000;
    static constexpr int DECIMALS = 5;
    static constexpr int64_t WHOLE_MIN = -2147483648LL;
    static constexpr int64_t WHOLE_MAX = 2147483647LL;
    // Inclusive raw range: -2147483648.99999 .. 2147483647.99999
    static constexpr int64_t RAW_MIN = WHOLE_MIN * SCALE - (SCALE - 1);
    static constexpr int64_t RAW_MAX = WHOLE_MAX * SCALE + (SCALE - 1);

    constexpr Fixed() = default;

    static constexpr Fixed from_raw(int64_t raw) {
        Fixed f;
        f.raw_ = raw;
        return f;
    }
    static constexpr Fixed from_int(int64_t whole) { return from_raw(whole * SCALE); }
    static Fixed from_double(double d); // UI / test convenience only; never used by the interpreter

    // Parses a script literal ("12", "-0.25", "1.234567" -> 1.23456). Returns false on malformed text.
    static bool parse(std::string_view text, Fixed& out);

    constexpr int64_t raw() const { return raw_; }
    double to_double() const { return static_cast<double>(raw_) / SCALE; } // display only
    constexpr int64_t whole_trunc() const { return raw_ / SCALE; }
    constexpr bool is_zero() const { return raw_ == 0; }
    constexpr bool truthy() const { return raw_ != 0; }

    // Exact decimal text with all 5 decimals, e.g. "-1.50000".
    std::string to_string_exact() const;
    // Text with `decimals` places (0..5), truncating like the formatter in-game (see textfmt).
    std::string to_string_fixed(int decimals) const;

    // ---- Arithmetic. Results are wrapped/saturated per Semantics::overflow; status flags record events. ----
    static Fixed add(Fixed a, Fixed b, const Semantics& s, OpStatus& st);
    static Fixed sub(Fixed a, Fixed b, const Semantics& s, OpStatus& st);
    static Fixed mul(Fixed a, Fixed b, const Semantics& s, OpStatus& st);
    // Divide-by-zero returns `a` when Semantics::div_by_zero == KeepTarget (the default).
    static Fixed div(Fixed a, Fixed b, const Semantics& s, OpStatus& st);
    static Fixed mod(Fixed a, Fixed b, const Semantics& s, OpStatus& st); // Euclidean remainder (>= 0)
    static Fixed neg(Fixed a, const Semantics& s, OpStatus& st);

    // round_variable: nearest integer, exact halves away from zero (wiki: -1.5 -> -2).
    static Fixed round_half_away(Fixed a);
    static Fixed floor(Fixed a);
    static Fixed ceil(Fixed a);
    static Fixed abs(Fixed a, const Semantics& s, OpStatus& st);

    // Brings an out-of-range raw value back into range according to the overflow mode.
    static int64_t normalize(int64_t raw, const Semantics& s, OpStatus& st);

    friend constexpr bool operator==(Fixed a, Fixed b) { return a.raw_ == b.raw_; }
    friend constexpr bool operator!=(Fixed a, Fixed b) { return a.raw_ != b.raw_; }
    friend constexpr bool operator<(Fixed a, Fixed b) { return a.raw_ < b.raw_; }
    friend constexpr bool operator>(Fixed a, Fixed b) { return a.raw_ > b.raw_; }
    friend constexpr bool operator<=(Fixed a, Fixed b) { return a.raw_ <= b.raw_; }
    friend constexpr bool operator>=(Fixed a, Fixed b) { return a.raw_ >= b.raw_; }

private:
    int64_t raw_ = 0;
};

namespace detail {

// Signed 64x64 -> 128 multiply, then divide by a positive 64-bit divisor, truncating toward zero.
// Returns false if the quotient does not fit in int64 (caller saturates; the value is far outside the
// variable range anyway, so the overflow flag is what matters).
bool muldiv_trunc(int64_t a, int64_t b, int64_t divisor, int64_t& quotient, int64_t& remainder);

} // namespace detail

} // namespace pdx
