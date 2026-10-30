#pragma once

// Every engine behaviour the simulator cannot verify from documentation is a knob here, with the default
// we believe matches HOI4. docs/SEMANTICS.md lists each one as VERIFIED or KNOB; an in-game calibration
// probe (milestone 9) is how a KNOB gets promoted to VERIFIED. Scenarios may override any field.

#include <cstdint>

namespace pdx {

enum class OverflowMode : uint8_t {
    Wrap32,   // whole part wraps at signed 32-bit (wiki: "going past the maximum wraps to the minimum")
    Saturate, // clamp to the range
    None      // keep full int64 precision (diagnostic mode: reveals what the value "should" have been)
};

enum class Rounding : uint8_t {
    Truncate,      // toward zero
    HalfAwayFromZero
};

enum class DivByZero : uint8_t {
    KeepTarget, // the target variable is left unchanged
    Zero,       // the result is 0
    Saturate    // +/- max of the range, sign of the dividend
};

enum class TempPrecedence : uint8_t {
    TempFirst, // a bare name that exists as a temp reads the temp, else the scope variable
    ScopeFirst
};

enum class BareAtTarget : uint8_t {
    TokenIfKnown, // `x@foo`: if `foo` is a known token use it as the token, else treat foo as a variable
    Variable,     // always a variable lookup
    Literal       // always the literal name (`x@foo` is just a different variable name)
};

struct Semantics {
    OverflowMode overflow = OverflowMode::Wrap32;
    Rounding mul_rounding = Rounding::Truncate;
    Rounding div_rounding = Rounding::Truncate;
    DivByZero div_by_zero = DivByZero::Zero; // Strat: used to be -0.1, now believed to be 0
    TempPrecedence temp_precedence = TempPrecedence::TempFirst;
    BareAtTarget bare_at_target = BareAtTarget::Literal; // probe: a bare @name is never a variable lookup
    // check_variable = { x = 5 }: true -> equality, false -> greater_than_or_equals
    bool check_shorthand_eq_is_equals = true;
    // check_variable shorthand `<=` / `>=`: memory says unsupported in-game. false -> evaluate as false + lint
    bool check_shorthand_allows_le_ge = false;
    // Same-day order when a weekly and a monthly tick coincide
    bool monthly_before_weekly = true;
    int max_effect_iterations = 1000; // NDefines.NGame.MAX_EFFECT_ITERATION
    int max_call_depth = 200;
    // `[?x]` with no format: -1 = as many decimals as the value has (2.432 -> "2.432", 2.3 -> "2.3"; Strat).
    // A number >= 0 forces that many (truncated).
    int text_default_decimals = -1;
};

} // namespace pdx
