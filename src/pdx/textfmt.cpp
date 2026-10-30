// Localisation-style text formatting used by `log` and `meta_effect` parameters:
//   [?var]            value with Semantics::text_default_decimals decimals
//   [?var|N]          N decimals (0..5), truncated
//   [?var|%]          x100 + "%" (2 decimals unless N given); |%% appends % without scaling
//   [?var|*] / |^     SI suffix (K, M, B) with 2 decimals
//   [?var|=]          explicit +/- sign; colour codes (+ - Y G R H ...) are ignored
//   [?var.GetTokenKey]          token name
//   [THIS.GetName] / [ROOT.GetTag] / [?scope_var.GetName]   country tag / state name

#include "pdx/interp.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pdx {

namespace {

std::string strip_suffix(std::string_view& s, std::string_view suffix) {
    if (s.size() > suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
        s = s.substr(0, s.size() - suffix.size());
        return std::string(suffix);
    }
    return {};
}

std::string scope_display_name(const world::World& w, ScopeRef s) {
    if (s.is_country()) {
        const world::Country* c = w.country(s.id);
        return c ? sym_str(c->tag) : "";
    }
    if (s.is_state()) {
        const world::State* st = w.state(s.id);
        return st && st->name != kNoSym ? sym_str(st->name) : "STATE_" + std::to_string(s.id);
    }
    return "";
}

} // namespace

std::string Interp::value_text(Ctx& c, const Value& v, std::string_view fmt) {
    (void)c;
    int decimals = -1;
    bool percent = false, percent_raw = false, si = false, sign = false;
    for (size_t i = 0; i < fmt.size(); ++i) {
        const char ch = fmt[i];
        if (ch >= '0' && ch <= '9') {
            decimals = std::min(ch - '0', Fixed::DECIMALS);
        } else if (ch == '%') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
                percent_raw = true;
                ++i;
            } else {
                percent = true;
            }
        } else if (ch == '*' || ch == '^') {
            si = true;
        } else if (ch == '=') {
            sign = true;
        }
    }
    Fixed x = v.num;
    std::string suffix;
    if (percent_raw) {
        suffix = "%";
        if (decimals < 0) decimals = 2;
    } else if (percent) {
        OpStatus st;
        x = Fixed::mul(x, Fixed::from_int(100), sem_, st);
        suffix = "%";
        if (decimals < 0) decimals = 2;
    }
    if (si) {
        const double mag = std::abs(x.to_double());
        OpStatus st;
        if (mag >= 1e9) {
            x = Fixed::div(x, Fixed::from_int(1000000000), sem_, st);
            suffix = "B" + suffix;
        } else if (mag >= 1e6) {
            x = Fixed::div(x, Fixed::from_int(1000000), sem_, st);
            suffix = "M" + suffix;
        } else if (mag >= 1e3) {
            x = Fixed::div(x, Fixed::from_int(1000), sem_, st);
            suffix = "K" + suffix;
        }
        if (decimals < 0) decimals = 2;
    }
    bool trim = false;
    if (decimals < 0) {
        decimals = sem_.text_default_decimals;
        if (decimals < 0) {
            decimals = Fixed::DECIMALS;
            trim = true;
        }
    }
    std::string out = x.to_string_fixed(decimals);
    if (trim && out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (sign && x.raw() > 0) out = "+" + out;
    return out + suffix;
}

std::string Interp::format_text(Ctx& c, std::string_view text, SrcLoc l) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (ch != '[') {
            out.push_back(ch);
            ++i;
            continue;
        }
        const size_t close = text.find(']', i + 1);
        if (close == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        std::string_view inner = text.substr(i + 1, close - i - 1);
        i = close + 1;

        if (!inner.empty() && inner[0] == '?') {
            inner = inner.substr(1);
            std::string_view fmt;
            const size_t bar = inner.find('|');
            if (bar != std::string_view::npos) {
                fmt = inner.substr(bar + 1);
                inner = inner.substr(0, bar);
            }
            std::string fn = strip_suffix(inner, ".GetTokenKey");
            if (fn.empty()) fn = strip_suffix(inner, ".GetTokenLocalizedKey");
            if (fn.empty()) fn = strip_suffix(inner, ".GetName");
            if (fn.empty()) fn = strip_suffix(inner, ".GetTag");
            Operand op;
            if (!compile_operand_text(inner, l, op)) {
                out.append("?");
                continue;
            }
            const Value v = eval(c, op, l);
            if (fn == ".GetTokenKey" || fn == ".GetTokenLocalizedKey") {
                out.append(v.kind == Kind::Token ? sym_str(v.token_sym()) : (v.num.is_zero() ? "0" : value_text(c, v, fmt)));
            } else if (fn == ".GetTag") {
                out.append(scope_display_name(w_, v.as_scope()));
            } else if (fn == ".GetName") {
                out.append(w_.display_name(v.as_scope()));
            } else {
                out.append(value_text(c, v, fmt));
            }
            continue;
        }

        // [SCOPE.Function]
        const size_t dot = inner.rfind('.');
        if (dot != std::string_view::npos) {
            const std::string_view scope_text = inner.substr(0, dot);
            const std::string_view func = inner.substr(dot + 1);
            Operand op;
            if (compile_operand_text(scope_text, l, op)) {
                const Value v = eval(c, op, l);
                if (func == "GetTag") {
                    out.append(scope_display_name(w_, v.as_scope()));
                    continue;
                }
                if (func == "GetName" || func == "GetNameDef" || func == "GetAdjective") {
                    out.append(w_.display_name(v.as_scope()));
                    continue;
                }
            }
        }
        out.push_back('[');
        out.append(inner);
        out.push_back(']');
    }
    return out;
}

} // namespace pdx
