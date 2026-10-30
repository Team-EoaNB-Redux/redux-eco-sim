#include "pdx/fixed.h"

#include <cmath>
#include <limits>

namespace pdx {

namespace detail {

namespace {

constexpr uint64_t U64_SIGN_BIT = 1ULL << 63;

uint64_t magnitude(int64_t v) {
    return v < 0 ? (~static_cast<uint64_t>(v) + 1ULL) : static_cast<uint64_t>(v);
}

// 64x64 -> 128 unsigned multiply.
void umul128(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
#if defined(__SIZEOF_INT128__)
    unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
    hi = static_cast<uint64_t>(p >> 64);
    lo = static_cast<uint64_t>(p);
#elif defined(_MSC_VER) && defined(_M_X64)
    lo = _umul128(a, b, &hi);
#else
    const uint64_t a_lo = a & 0xFFFFFFFFULL, a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFFFFFFULL, b_hi = b >> 32;
    const uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    const uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);
    lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
    hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
#endif
}

// (hi:lo) / d where hi < d (so the quotient fits in 64 bits).
uint64_t udiv128(uint64_t hi, uint64_t lo, uint64_t d, uint64_t& rem) {
#if defined(__SIZEOF_INT128__)
    unsigned __int128 n = (static_cast<unsigned __int128>(hi) << 64) | lo;
    rem = static_cast<uint64_t>(n % d);
    return static_cast<uint64_t>(n / d);
#elif defined(_MSC_VER) && defined(_M_X64)
    return _udiv128(hi, lo, d, &rem);
#else
    uint64_t q = 0;
    uint64_t r = hi;
    for (int i = 63; i >= 0; --i) {
        const bool carry = (r & U64_SIGN_BIT) != 0;
        r = (r << 1) | ((lo >> i) & 1ULL);
        q <<= 1;
        if (carry || r >= d) {
            r -= d;
            q |= 1ULL;
        }
    }
    rem = r;
    return q;
#endif
}

} // namespace

bool muldiv_trunc(int64_t a, int64_t b, int64_t divisor, int64_t& quotient, int64_t& remainder) {
    const bool negative = (a < 0) != (b < 0);
    uint64_t hi = 0, lo = 0;
    umul128(magnitude(a), magnitude(b), hi, lo);
    const uint64_t d = static_cast<uint64_t>(divisor);
    if (hi >= d) {
        quotient = negative ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
        remainder = 0;
        return false;
    }
    uint64_t rem = 0;
    const uint64_t q = udiv128(hi, lo, d, rem);
    if (q > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        quotient = negative ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
        remainder = 0;
        return false;
    }
    quotient = negative ? -static_cast<int64_t>(q) : static_cast<int64_t>(q);
    remainder = negative ? -static_cast<int64_t>(rem) : static_cast<int64_t>(rem);
    return true;
}

} // namespace detail

namespace {

int64_t saturate_add(int64_t a, int64_t b, bool& overflowed) {
    if (b > 0 && a > std::numeric_limits<int64_t>::max() - b) {
        overflowed = true;
        return std::numeric_limits<int64_t>::max();
    }
    if (b < 0 && a < std::numeric_limits<int64_t>::min() - b) {
        overflowed = true;
        return std::numeric_limits<int64_t>::min();
    }
    return a + b;
}

// Applies rounding to a truncated quotient given its remainder and the (positive) divisor.
int64_t apply_rounding(int64_t q, int64_t rem, int64_t divisor, Rounding mode) {
    if (mode == Rounding::Truncate || rem == 0) {
        return q;
    }
    const uint64_t r2 = static_cast<uint64_t>(rem < 0 ? -rem : rem) * 2ULL;
    if (r2 >= static_cast<uint64_t>(divisor)) {
        return rem < 0 ? q - 1 : q + 1;
    }
    return q;
}

} // namespace

int64_t Fixed::normalize(int64_t raw, const Semantics& s, OpStatus& st) {
    if (raw >= RAW_MIN && raw <= RAW_MAX) {
        return raw;
    }
    st.overflow = true;
    switch (s.overflow) {
    case OverflowMode::None:
        return raw;
    case OverflowMode::Saturate:
        return raw < 0 ? RAW_MIN : RAW_MAX;
    case OverflowMode::Wrap32:
    default: {
        const int64_t whole = raw / SCALE;
        const int64_t frac = raw % SCALE;
        const int64_t whole32 = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(whole))));
        return whole32 * SCALE + frac;
    }
    }
}

Fixed Fixed::from_double(double d) {
    return from_raw(static_cast<int64_t>(std::llround(d * static_cast<double>(SCALE))));
}

bool Fixed::parse(std::string_view text, Fixed& out) {
    size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    uint64_t whole = 0;
    bool any_digit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        whole = whole * 10 + static_cast<uint64_t>(text[i] - '0');
        if (whole > 92233720368547ULL) { // keeps whole * SCALE inside int64
            return false;
        }
        any_digit = true;
        ++i;
    }
    uint64_t frac = 0;
    int frac_digits = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (frac_digits < DECIMALS) { // finer precision is truncated
                frac = frac * 10 + static_cast<uint64_t>(text[i] - '0');
                ++frac_digits;
            }
            any_digit = true;
            ++i;
        }
    }
    if (!any_digit || i != text.size()) {
        return false;
    }
    while (frac_digits < DECIMALS) {
        frac *= 10;
        ++frac_digits;
    }
    const int64_t raw = static_cast<int64_t>(whole) * SCALE + static_cast<int64_t>(frac);
    out = from_raw(negative ? -raw : raw);
    return true;
}

std::string Fixed::to_string_exact() const {
    return to_string_fixed(DECIMALS);
}

std::string Fixed::to_string_fixed(int decimals) const {
    if (decimals < 0) {
        decimals = 0;
    }
    if (decimals > DECIMALS) {
        decimals = DECIMALS;
    }
    const uint64_t mag = raw_ < 0 ? (~static_cast<uint64_t>(raw_) + 1ULL) : static_cast<uint64_t>(raw_);
    const uint64_t whole = mag / SCALE;
    uint64_t frac = mag % SCALE;
    for (int d = DECIMALS; d > decimals; --d) {
        frac /= 10;
    }
    std::string out;
    if (raw_ < 0 && (whole != 0 || frac != 0)) {
        out.push_back('-');
    }
    out += std::to_string(whole);
    if (decimals > 0) {
        std::string f = std::to_string(frac);
        out.push_back('.');
        out.append(static_cast<size_t>(decimals) - f.size(), '0');
        out += f;
    }
    return out;
}

Fixed Fixed::add(Fixed a, Fixed b, const Semantics& s, OpStatus& st) {
    bool ov = false;
    const int64_t r = saturate_add(a.raw_, b.raw_, ov);
    st.overflow |= ov;
    return from_raw(normalize(r, s, st));
}

Fixed Fixed::sub(Fixed a, Fixed b, const Semantics& s, OpStatus& st) {
    if (b.raw_ == std::numeric_limits<int64_t>::min()) {
        st.overflow = true;
        return from_raw(normalize(std::numeric_limits<int64_t>::max(), s, st));
    }
    return add(a, from_raw(-b.raw_), s, st);
}

Fixed Fixed::neg(Fixed a, const Semantics& s, OpStatus& st) {
    return sub(Fixed{}, a, s, st);
}

Fixed Fixed::mul(Fixed a, Fixed b, const Semantics& s, OpStatus& st) {
    int64_t q = 0, rem = 0;
    if (!detail::muldiv_trunc(a.raw_, b.raw_, SCALE, q, rem)) {
        st.overflow = true;
    }
    q = apply_rounding(q, rem, SCALE, s.mul_rounding);
    return from_raw(normalize(q, s, st));
}

Fixed Fixed::div(Fixed a, Fixed b, const Semantics& s, OpStatus& st) {
    if (b.raw_ == 0) {
        st.div_by_zero = true;
        switch (s.div_by_zero) {
        case DivByZero::Zero:
            return Fixed{};
        case DivByZero::Saturate:
            return from_raw(a.raw_ < 0 ? RAW_MIN : RAW_MAX);
        case DivByZero::KeepTarget:
        default:
            return a;
        }
    }
    const bool neg_divisor = b.raw_ < 0;
    const int64_t divisor = neg_divisor ? -b.raw_ : b.raw_;
    int64_t q = 0, rem = 0;
    if (!detail::muldiv_trunc(a.raw_, SCALE, divisor, q, rem)) {
        st.overflow = true;
    }
    q = apply_rounding(q, rem, divisor, s.div_rounding);
    if (neg_divisor) {
        q = -q;
    }
    return from_raw(normalize(q, s, st));
}

Fixed Fixed::mod(Fixed a, Fixed b, const Semantics& s, OpStatus& st) {
    if (b.raw_ == 0) {
        st.div_by_zero = true;
        return s.div_by_zero == DivByZero::Zero ? Fixed{} : a;
    }
    int64_t r = a.raw_ % b.raw_;
    if (r < 0) r += b.raw_ < 0 ? -b.raw_ : b.raw_; // Euclidean: result is always >= 0
    return from_raw(r);
}

Fixed Fixed::round_half_away(Fixed a) {
    const int64_t whole = a.raw_ / SCALE;
    const int64_t frac = a.raw_ % SCALE;
    int64_t r = whole;
    if (frac >= SCALE / 2) {
        r += 1;
    } else if (frac <= -SCALE / 2) {
        r -= 1;
    }
    return from_int(r);
}

Fixed Fixed::floor(Fixed a) {
    int64_t whole = a.raw_ / SCALE;
    if (a.raw_ % SCALE < 0) {
        whole -= 1;
    }
    return from_int(whole);
}

Fixed Fixed::ceil(Fixed a) {
    int64_t whole = a.raw_ / SCALE;
    if (a.raw_ % SCALE > 0) {
        whole += 1;
    }
    return from_int(whole);
}

Fixed Fixed::abs(Fixed a, const Semantics& s, OpStatus& st) {
    return a.raw_ < 0 ? neg(a, s, st) : a;
}

} // namespace pdx
