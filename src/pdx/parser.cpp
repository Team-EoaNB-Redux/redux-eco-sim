#include "pdx/parser.h"

#include <cstdio>
#include <unordered_map>

namespace pdx {

const char* op_text(Op op) {
    switch (op) {
    case Op::Eq: return "=";
    case Op::Lt: return "<";
    case Op::Gt: return ">";
    case Op::Le: return "<=";
    case Op::Ge: return ">=";
    case Op::Ne: return "!=";
    case Op::None:
    default: return "";
    }
}

const Node* ScriptFile::find(const Node& block, Sym key) const {
    for (const Node* n = begin(block); n != end(block); ++n) {
        if (n->key == key) {
            return n;
        }
    }
    return nullptr;
}

namespace {

enum class Tok : uint8_t { End, Word, String, LBrace, RBrace, Oper };

struct Token {
    Tok kind = Tok::End;
    Op op = Op::None;
    std::string_view text;
    uint32_t line = 0;
};

bool is_space(unsigned char c) { return c <= ' '; }

bool is_word_break(const std::string_view& s, size_t i) {
    const char c = s[i];
    if (is_space(static_cast<unsigned char>(c))) {
        return true;
    }
    switch (c) {
    case '{': case '}': case '=': case '<': case '>': case '#': case '"':
        return true;
    case '!':
        return i + 1 < s.size() && s[i + 1] == '=';
    default:
        return false;
    }
}

class Lexer {
public:
    explicit Lexer(std::string_view text) : s_(text) {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF &&
            static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF) {
            pos_ = 3;
        }
    }

    Token peek() {
        if (!has_peek_) {
            peeked_ = lex();
            has_peek_ = true;
        }
        return peeked_;
    }

    Token next() {
        if (has_peek_) {
            has_peek_ = false;
            return peeked_;
        }
        return lex();
    }


private:
    Token lex() {
        for (;;) {
            while (pos_ < s_.size() && is_space(static_cast<unsigned char>(s_[pos_]))) {
                if (s_[pos_] == '\n') {
                    ++line_;
                }
                ++pos_;
            }
            if (pos_ < s_.size() && s_[pos_] == '#') {
                while (pos_ < s_.size() && s_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            break;
        }
        Token t;
        t.line = line_;
        if (pos_ >= s_.size()) {
            t.kind = Tok::End;
            return t;
        }
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            t.kind = Tok::LBrace;
            return t;
        }
        if (c == '}') {
            ++pos_;
            t.kind = Tok::RBrace;
            return t;
        }
        if (c == '=' || c == '<' || c == '>' || (c == '!' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '=')) {
            t.kind = Tok::Oper;
            const bool eq_next = pos_ + 1 < s_.size() && s_[pos_ + 1] == '=';
            switch (c) {
            case '=': t.op = Op::Eq; break;
            case '<': t.op = eq_next ? Op::Le : Op::Lt; break;
            case '>': t.op = eq_next ? Op::Ge : Op::Gt; break;
            case '!': t.op = Op::Ne; break;
            default: break;
            }
            pos_ += eq_next ? 2 : 1;
            return t;
        }
        if (c == '"') {
            ++pos_;
            const size_t start = pos_;
            while (pos_ < s_.size() && s_[pos_] != '"') {
                if (s_[pos_] == '\\' && pos_ + 1 < s_.size()) {
                    ++pos_;
                }
                if (s_[pos_] == '\n') {
                    ++line_;
                }
                ++pos_;
            }
            t.kind = Tok::String;
            t.text = s_.substr(start, pos_ - start);
            if (pos_ < s_.size()) {
                ++pos_; // closing quote
            }
            return t;
        }
        const size_t start = pos_;
        while (pos_ < s_.size() && !is_word_break(s_, pos_)) {
            ++pos_;
        }
        t.kind = Tok::Word;
        t.text = s_.substr(start, pos_ - start);
        return t;
    }

    std::string_view s_;
    size_t pos_ = 0;
    uint32_t line_ = 1;
    Token peeked_;
    bool has_peek_ = false;
};

bool is_color_tag(std::string_view w) {
    return w == "rgb" || w == "RGB" || w == "hsv" || w == "HSV" || w == "hsv360" || w == "HSV360";
}

Sym intern_string_token(const Token& t) {
    if (t.kind != Tok::String || t.text.find('\\') == std::string_view::npos) {
        return intern(t.text);
    }
    std::string out;
    out.reserve(t.text.size());
    for (size_t i = 0; i < t.text.size(); ++i) {
        if (t.text[i] == '\\' && i + 1 < t.text.size() && (t.text[i + 1] == '"' || t.text[i + 1] == '\\')) {
            ++i;
        }
        out.push_back(t.text[i]);
    }
    return intern(out);
}

class Parser {
public:
    Parser(std::string_view text, ScriptFile& out) : lex_(text), out_(out) {}

    void run() {
        frames_.emplace_back();
        parse_contents(/*depth=*/0);
        std::vector<Node>& top = frames_.back();
        out_.root_first = static_cast<uint32_t>(out_.nodes.size());
        out_.root_count = static_cast<uint32_t>(top.size());
        out_.nodes.insert(out_.nodes.end(), top.begin(), top.end());
        frames_.pop_back();
    }

private:
    void error(uint32_t line, std::string msg) { out_.errors.push_back({line, std::move(msg)}); }

    // Parses entries into frames_.back() until the matching '}' (depth > 0) or end of input.
    void parse_contents(int depth) {
        for (;;) {
            Token t = lex_.next();
            switch (t.kind) {
            case Tok::End:
                if (depth > 0) {
                    error(t.line, "unexpected end of file: missing '}'");
                }
                return;
            case Tok::RBrace:
                if (depth > 0) {
                    return;
                }
                error(t.line, "unmatched '}' at top level (ignored)");
                continue;
            case Tok::LBrace: {
                Node n;
                n.line = t.line;
                n.is_block = true;
                parse_block_into(n, depth);
                frames_.back().push_back(n);
                continue;
            }
            case Tok::Oper:
                error(t.line, std::string("operator '") + op_text(t.op) + "' without a key");
                continue;
            case Tok::Word:
            case Tok::String:
                parse_entry(t, depth);
                continue;
            }
        }
    }

    void parse_block_into(Node& n, int depth) {
        frames_.emplace_back();
        parse_contents(depth + 1);
        std::vector<Node> children = std::move(frames_.back());
        frames_.pop_back();
        n.first_child = static_cast<uint32_t>(out_.nodes.size());
        n.child_count = static_cast<uint32_t>(children.size());
        out_.nodes.insert(out_.nodes.end(), children.begin(), children.end());
    }

    void parse_entry(const Token& key_tok, int depth) {
        Token p = lex_.peek();
        if (p.kind != Tok::Oper) {
            // Bare list item.
            Node n;
            n.line = key_tok.line;
            n.value = resolve_value(key_tok);
            n.quoted = key_tok.kind == Tok::String;
            frames_.back().push_back(n);
            return;
        }
        lex_.next(); // consume operator
        Node n;
        n.line = key_tok.line;
        n.key = intern_string_token(key_tok);
        n.op = p.op;

        const Token v = lex_.peek();
        if (v.kind == Tok::RBrace || v.kind == Tok::End) {
            // Leave the '}' for the enclosing loop so the block still closes correctly.
            error(v.line, "missing value after '" + std::string(sym_view(n.key)) + " " + op_text(n.op) + "'");
            frames_.back().push_back(n);
            return;
        }
        lex_.next();
        switch (v.kind) {
        case Tok::LBrace:
            n.is_block = true;
            parse_block_into(n, depth);
            break;
        case Tok::Word:
        case Tok::String:
            n.quoted = v.kind == Tok::String;
            if (v.kind == Tok::Word && is_color_tag(v.text) && lex_.peek().kind == Tok::LBrace) {
                lex_.next();
                n.value = intern(v.text);
                n.is_block = true;
                parse_block_into(n, depth);
                break;
            }
            n.value = resolve_value(v);
            break;
        case Tok::Oper:
            error(v.line, "unexpected operator after '" + std::string(sym_view(n.key)) + "'");
            break;
        default:
            break;
        }

        // File-local constant definition: `@name = value` at top level.
        if (depth == 0 && !n.is_block && !sym_view(n.key).empty() && sym_view(n.key)[0] == '@') {
            constants_[std::string(sym_view(n.key))] = n.value;
            return;
        }
        frames_.back().push_back(n);
    }

    Sym resolve_value(const Token& t) {
        if (t.kind == Tok::Word && !t.text.empty() && t.text[0] == '@') {
            auto it = constants_.find(std::string(t.text));
            if (it != constants_.end()) {
                return it->second;
            }
        }
        return intern_string_token(t);
    }

    Lexer lex_;
    ScriptFile& out_;
    std::vector<std::vector<Node>> frames_;
    std::unordered_map<std::string, Sym> constants_;
};

} // namespace

void parse_text(std::string_view text, ScriptFile& out) {
    Parser p(text, out);
    p.run();
}

bool parse_file(const std::string& path, ScriptFile& out) {
    out.path = path;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        out.errors.push_back({0, "cannot open file"});
        return false;
    }
    std::string data;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0) {
        data.resize(static_cast<size_t>(size));
        const size_t got = std::fread(data.data(), 1, data.size(), f);
        data.resize(got);
    }
    std::fclose(f);
    parse_text(data, out);
    return true;
}

} // namespace pdx

namespace pdx {

namespace {

void append_value(std::string& out, const Node& n) {
    if (n.quoted) {
        out.push_back('"');
        for (char ch : sym_view(n.value)) {
            if (ch == '"' || ch == '\\') out.push_back('\\');
            out.push_back(ch);
        }
        out.push_back('"');
    } else {
        out.append(sym_view(n.value));
    }
}

void serialize_node(const ScriptFile& f, const Node& n, std::string& out) {
    if (n.key != kNoSym) {
        out.append(sym_view(n.key));
        out.push_back(' ');
        out.append(op_text(n.op == Op::None ? Op::Eq : n.op));
        out.push_back(' ');
    }
    if (n.is_block) {
        if (n.value != kNoSym) {
            out.append(sym_view(n.value));
            out.push_back(' ');
        }
        out.append("{ ");
        for (const Node* c = f.begin(n); c != f.end(n); ++c) {
            serialize_node(f, *c, out);
            out.push_back(' ');
        }
        out.push_back('}');
    } else {
        append_value(out, n);
    }
}

} // namespace

std::string serialize_children(const ScriptFile& f, const Node& block) {
    std::string out;
    if (!block.is_block) return out;
    for (const Node* c = f.begin(block); c != f.end(block); ++c) {
        serialize_node(f, *c, out);
        out.push_back('\n');
    }
    return out;
}

} // namespace pdx
