#include "pdx/symbols.h"

namespace pdx {

Symbols& Symbols::instance() {
    static Symbols s;
    return s;
}

Symbols::Symbols() {
    storage_.emplace_back();
    views_.emplace_back(storage_.back());
    index_.emplace(views_.back(), kNoSym);
}

Sym Symbols::intern(std::string_view text) {
    auto it = index_.find(text);
    if (it != index_.end()) {
        return it->second;
    }
    storage_.emplace_back(text);
    const Sym id = static_cast<Sym>(views_.size());
    views_.emplace_back(storage_.back());
    index_.emplace(views_.back(), id);
    return id;
}

Sym Symbols::find(std::string_view text) const {
    auto it = index_.find(text);
    return it == index_.end() ? kNoSym : it->second;
}

} // namespace pdx
