#include "test.h"

#include "pdx/fixed.h"

using pdx::Fixed;
using pdx::OpStatus;
using pdx::Semantics;

namespace {

Fixed F(const char* text) {
    Fixed f;
    if (!Fixed::parse(text, f)) {
        ::ut::report_failure(__FILE__, __LINE__, std::string("bad literal ") + text);
    }
    return f;
}

std::string S(Fixed f) { return f.to_string_exact(); }

} // namespace

TEST_CASE(fixed_parse_and_print) {
    CHECK_EQ(S(F("0")), std::string("0.00000"));
    CHECK_EQ(S(F("12")), std::string("12.00000"));
    CHECK_EQ(S(F("-0.25")), std::string("-0.25000"));
    CHECK_EQ(S(F("1.234567")), std::string("1.23456")); // finer precision truncated
    CHECK_EQ(S(F("+3.5")), std::string("3.50000"));
    CHECK_EQ(S(F(".5")), std::string("0.50000"));
    Fixed f;
    CHECK(!Fixed::parse("abc", f));
    CHECK(!Fixed::parse("1e5", f));
    CHECK(!Fixed::parse("-", f));
    CHECK_EQ(F("2147483647").to_string_fixed(0), std::string("2147483647"));
    CHECK_EQ(F("-1.99999").to_string_fixed(2), std::string("-1.99"));
}

TEST_CASE(fixed_mul_truncates_toward_zero) {
    Semantics s;
    OpStatus st;
    CHECK_EQ(S(Fixed::mul(F("0.00001"), F("0.5"), s, st)), std::string("0.00000"));
    CHECK_EQ(S(Fixed::mul(F("-0.00001"), F("0.5"), s, st)), std::string("0.00000"));
    CHECK_EQ(S(Fixed::mul(F("1.5"), F("-2.25"), s, st)), std::string("-3.37500"));
    CHECK_EQ(S(Fixed::mul(F("1000000"), F("1000"), s, st)), std::string("1000000000.00000"));
    CHECK(!st.overflow);
}

TEST_CASE(fixed_div_truncates_and_round_trip_loses) {
    Semantics s;
    OpStatus st;
    const Fixed third = Fixed::div(F("1"), F("3"), s, st);
    CHECK_EQ(S(third), std::string("0.33333"));
    CHECK_EQ(S(Fixed::mul(third, F("3"), s, st)), std::string("0.99999"));
    CHECK_EQ(S(Fixed::div(F("-1"), F("3"), s, st)), std::string("-0.33333"));
    CHECK_EQ(S(Fixed::div(F("1"), F("-3"), s, st)), std::string("-0.33333"));
    CHECK_EQ(S(Fixed::div(F("2000000000"), F("0.5"), s, st)), std::string("-294967296.00000")); // wraps
    CHECK(st.overflow);
}

TEST_CASE(fixed_rounding_knob) {
    Semantics s;
    s.mul_rounding = pdx::Rounding::HalfAwayFromZero;
    s.div_rounding = pdx::Rounding::HalfAwayFromZero;
    OpStatus st;
    CHECK_EQ(S(Fixed::div(F("2"), F("3"), s, st)), std::string("0.66667"));
    CHECK_EQ(S(Fixed::div(F("-2"), F("3"), s, st)), std::string("-0.66667"));
    CHECK_EQ(S(Fixed::mul(F("0.00001"), F("0.5"), s, st)), std::string("0.00001"));
}

TEST_CASE(fixed_div_by_zero_knob) {
    Semantics s;
    OpStatus st;
    CHECK_EQ(S(Fixed::div(F("5"), F("0"), s, st)), std::string("0.00000")); // default: Zero
    CHECK(st.div_by_zero);
    s.div_by_zero = pdx::DivByZero::KeepTarget;
    CHECK_EQ(S(Fixed::div(F("5"), F("0"), s, st)), std::string("5.00000"));
}

TEST_CASE(fixed_overflow_wraps_at_int32_whole) {
    Semantics s;
    OpStatus st;
    const Fixed r = Fixed::add(F("2147483647"), F("1"), s, st);
    CHECK(st.overflow);
    CHECK_EQ(S(r), std::string("-2147483648.00000"));
    OpStatus st2;
    const Fixed in_range = Fixed::add(F("2147483647"), F("0.99999"), s, st2);
    CHECK(!st2.overflow);
    CHECK_EQ(S(in_range), std::string("2147483647.99999"));
    s.overflow = pdx::OverflowMode::Saturate;
    OpStatus st3;
    CHECK_EQ(S(Fixed::add(F("2147483647"), F("5"), s, st3)), std::string("2147483647.99999"));
    s.overflow = pdx::OverflowMode::None;
    OpStatus st4;
    CHECK_EQ(S(Fixed::add(F("2147483647"), F("5"), s, st4)), std::string("2147483652.00000"));
    CHECK(st4.overflow);
}

TEST_CASE(fixed_large_mul_exact_via_128bit) {
    Semantics s;
    s.overflow = pdx::OverflowMode::None;
    OpStatus st;
    // 90,000,000 * 1,000,000 = 9e13 exactly (raw intermediate ~9e23 needs 128 bits)
    CHECK_EQ(S(Fixed::mul(F("90000000"), F("1000000"), s, st)), std::string("90000000000000.00000"));
}

TEST_CASE(fixed_round_variable_half_away) {
    CHECK_EQ(S(Fixed::round_half_away(F("1.5"))), std::string("2.00000"));
    CHECK_EQ(S(Fixed::round_half_away(F("-1.5"))), std::string("-2.00000"));
    CHECK_EQ(S(Fixed::round_half_away(F("1.49999"))), std::string("1.00000"));
    CHECK_EQ(S(Fixed::round_half_away(F("-0.4"))), std::string("0.00000"));
    CHECK_EQ(S(Fixed::floor(F("-0.4"))), std::string("-1.00000"));
    CHECK_EQ(S(Fixed::ceil(F("0.00001"))), std::string("1.00000"));
}

TEST_CASE(fixed_mod) {
    Semantics s;
    OpStatus st;
    CHECK_EQ(S(Fixed::mod(F("7"), F("3"), s, st)), std::string("1.00000"));
    CHECK_EQ(S(Fixed::mod(F("-7"), F("3"), s, st)), std::string("2.00000")); // Euclidean (wiki)
    CHECK_EQ(S(Fixed::mod(F("5.5"), F("2"), s, st)), std::string("1.50000"));
}
