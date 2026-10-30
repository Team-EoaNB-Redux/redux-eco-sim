#include "pdx/varref.h"

namespace pdx {

namespace {

bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

bool parse_int(std::string_view s, int64_t& out) {
    bool neg = false;
    size_t i = 0;
    if (!s.empty() && s[0] == '-') {
        neg = true;
        i = 1;
    }
    if (i >= s.size()) return false;
    int64_t v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

bool valid_name(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!is_ident_char(c)) return false;
    }
    return true;
}

// Splits `head` (the part before '@'/'^') into scope steps + final name.
bool parse_head(std::string_view head, VarRef& out, std::string* error) {
    // Tokenize on ':' and '.', keeping `var:` glued to the following segment.
    std::vector<std::string_view> segs;
    size_t start = 0;
    for (size_t i = 0; i <= head.size(); ++i) {
        if (i == head.size() || head[i] == ':' || head[i] == '.') {
            std::string_view seg = head.substr(start, i - start);
            if (seg == "var" && i < head.size() && head[i] == ':') {
                // `var:x` — merge with the next segment
                size_t j = i + 1;
                while (j < head.size() && head[j] != ':' && head[j] != '.') ++j;
                segs.push_back(head.substr(start, j - start));
                i = j;
                start = j + 1;
                continue;
            }
            segs.push_back(seg);
            start = i + 1;
        }
    }
    if (segs.empty() || segs.back().empty()) {
        if (error) *error = "empty variable name";
        return false;
    }
    size_t first = 0;
    if (segs.size() >= 2 && segs[0] == "global") {
        out.global = true;
        first = 1;
    }
    for (size_t i = first; i + 1 < segs.size(); ++i) {
        std::string_view s = segs[i];
        Step st;
        if (s == "THIS" || s == "this") {
            st.kind = StepKind::This;
        } else if (s == "ROOT" || s == "root") {
            st.kind = StepKind::Root;
        } else if (s == "FROM" || s == "from") {
            st.kind = StepKind::From;
        } else if (s == "PREV" || s == "prev") {
            if (!out.path.empty() && out.path.back().kind == StepKind::Prev) {
                ++out.path.back().prev_depth;
                continue;
            }
            st.kind = StepKind::Prev;
        } else if (s == "controller") {
            st.kind = StepKind::Controller;
        } else if (s == "owner") {
            st.kind = StepKind::Owner;
        } else if (s == "capital" || s == "capital_scope") {
            st.kind = StepKind::Capital;
        } else if (s == "overlord" || s == "OVERLORD") {
            st.kind = StepKind::Overlord;
        } else if (all_digits(s)) {
            st.kind = StepKind::StateId;
            int64_t v = 0;
            parse_int(s, v);
            st.number = static_cast<int32_t>(v);
        } else if (s.size() > 4 && s.substr(0, 4) == "var:") {
            st.kind = StepKind::Named;
            st.name = intern(s.substr(4));
        } else if (looks_like_tag(s)) {
            st.kind = StepKind::Tag;
            st.name = intern(s);
        } else if (valid_name(s)) {
            st.kind = StepKind::Named;
            st.name = intern(s);
        } else {
            if (error) *error = "bad scope segment '" + std::string(s) + "'";
            return false;
        }
        out.path.push_back(st);
    }
    std::string_view name = segs.back();
    if (name.size() > 4 && name.substr(0, 4) == "var:") {
        name = name.substr(4);
    }
    if (!valid_name(name)) {
        if (error) *error = "bad variable name '" + std::string(name) + "'";
        return false;
    }
    out.name = intern(name);
    return true;
}

bool parse_target(std::string_view t, Target& out, std::string* error) {
    if (t.empty()) {
        if (error) *error = "empty @ target";
        return false;
    }
    if (t.size() > 6 && t.substr(0, 6) == "token:") {
        out.kind = TargetKind::Token;
        out.sym = intern(t.substr(6));
        return true;
    }
    if (t.size() > 4 && t.substr(0, 4) == "var:") {
        auto ref = std::make_shared<VarRef>();
        if (!parse_varref(t.substr(4), *ref, error)) return false;
        out.kind = TargetKind::Var;
        out.var = std::move(ref);
        return true;
    }
    if (t == "THIS" || t == "ROOT" || t == "PREV" || t == "FROM") {
        out.kind = TargetKind::Scope;
        out.step.kind = t == "THIS" ? StepKind::This : t == "ROOT" ? StepKind::Root : t == "PREV" ? StepKind::Prev : StepKind::From;
        return true;
    }
    int64_t n = 0;
    if (parse_int(t, n)) {
        out.kind = TargetKind::Number;
        out.number = n;
        return true;
    }
    if (looks_like_tag(t)) {
        out.kind = TargetKind::Scope;
        out.step.kind = StepKind::Tag;
        out.step.name = intern(t);
        return true;
    }
    if (!valid_name(t)) {
        if (error) *error = "bad @ target '" + std::string(t) + "'";
        return false;
    }
    out.kind = TargetKind::Bare;
    out.sym = intern(t);
    return true;
}

bool parse_index(std::string_view s, Index& out, std::string* error) {
    if (s == "num") {
        out.kind = IndexKind::Num;
        return true;
    }
    int64_t n = 0;
    if (parse_int(s, n)) {
        out.kind = IndexKind::Int;
        out.i = n;
        return true;
    }
    auto ref = std::make_shared<VarRef>();
    if (!parse_varref(s, *ref, error)) return false;
    out.kind = IndexKind::Var;
    out.var = std::move(ref);
    return true;
}

} // namespace

bool looks_like_tag(std::string_view w) {
    if (w.size() != 3) return false;
    if (!(w[0] >= 'A' && w[0] <= 'Z')) return false;
    for (char c : w) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

bool parse_varref(std::string_view text, VarRef& out, std::string* error) {
    out = VarRef{};
    out.text = std::string(text);
    std::string_view s = text;

    // ?default (numeric)
    const size_t q = s.rfind('?');
    if (q != std::string_view::npos) {
        Fixed d;
        if (!Fixed::parse(s.substr(q + 1), d)) {
            if (error) *error = "bad ?default in '" + std::string(text) + "'";
            return false;
        }
        out.has_default = true;
        out.default_value = d;
        s = s.substr(0, q);
    }

    std::string_view head = s, tail;
    const size_t at = s.find('@');
    if (at != std::string_view::npos) {
        head = s.substr(0, at);
        tail = s.substr(at + 1);
    }

    std::string_view index_text;
    if (at == std::string_view::npos) {
        const size_t caret = head.find('^');
        if (caret != std::string_view::npos) {
            index_text = head.substr(caret + 1);
            head = head.substr(0, caret);
        }
    } else {
        const size_t caret = tail.find('^');
        if (caret != std::string_view::npos) {
            index_text = tail.substr(caret + 1);
            tail = tail.substr(0, caret);
        }
    }

    if (!parse_head(head, out, error)) return false;
    if (at != std::string_view::npos && !parse_target(tail, out.target, error)) return false;
    if (!index_text.empty() && !parse_index(index_text, out.index, error)) return false;
    return true;
}

} // namespace pdx
