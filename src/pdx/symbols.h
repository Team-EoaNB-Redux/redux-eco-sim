#pragma once

// Global string interning. Every key, value word and variable name in the loaded scripts maps to a dense
// uint32 id, so the interpreter compares and hashes integers instead of strings. Id 0 is the empty string.
// Not thread-safe for concurrent interning; loading is single-threaded.

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pdx {

using Sym = uint32_t;
inline constexpr Sym kNoSym = 0;

class Symbols {
public:
    static Symbols& instance();

    Sym intern(std::string_view text);
    // Returns kNoSym if the text was never interned (cheap existence test, does not insert).
    Sym find(std::string_view text) const;
    std::string_view view(Sym id) const { return views_[id]; }
    size_t size() const { return views_.size(); }

private:
    Symbols();

    std::deque<std::string> storage_; // stable addresses for the views
    std::vector<std::string_view> views_;
    std::unordered_map<std::string_view, Sym> index_;
};

inline Sym intern(std::string_view text) { return Symbols::instance().intern(text); }
inline std::string_view sym_view(Sym id) { return Symbols::instance().view(id); }
inline std::string sym_str(Sym id) { return std::string(Symbols::instance().view(id)); }

} // namespace pdx
