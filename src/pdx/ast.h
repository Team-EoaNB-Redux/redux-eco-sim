#pragma once

// Clausewitz script AST. One ScriptFile owns a flat node arena; the children of any block are a contiguous
// index range [first_child, first_child + child_count) in that arena, so walking a block is a linear scan.
//
//   key = value          Node{key, Op::Eq, scalar value}
//   key > 5              Node{key, Op::Gt, scalar}
//   key = { ... }        Node{key, Op::Eq, is_block}
//   color = rgb { 1 2 3 } Node{key, Op::Eq, is_block, value = "rgb"}   (tagged block)
//   { a b c }            list items: Node{key = kNoSym, Op::None, scalar value}
//   { { ... } }          anonymous block item: Node{key = kNoSym, Op::None, is_block}

#include "pdx/symbols.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pdx {

enum class Op : uint8_t { None, Eq, Lt, Gt, Le, Ge, Ne };

const char* op_text(Op op);

struct Node {
    Sym key = kNoSym;
    Sym value = kNoSym; // scalar text, or the tag of a tagged block
    Op op = Op::None;
    bool is_block = false;
    bool quoted = false; // value was a quoted string
    uint32_t first_child = 0;
    uint32_t child_count = 0;
    uint32_t line = 0;
};

struct ParseError {
    uint32_t line = 0;
    std::string message;
};

struct ScriptFile {
    std::string path;     // absolute or mod-relative path, for diagnostics
    uint32_t file_id = 0; // index in the loader's file table
    std::vector<Node> nodes;
    uint32_t root_first = 0; // top-level entries
    uint32_t root_count = 0;
    std::vector<ParseError> errors;

    const Node* begin(const Node& block) const { return nodes.data() + block.first_child; }
    const Node* end(const Node& block) const { return nodes.data() + block.first_child + block.child_count; }
    const Node* root_begin() const { return nodes.data() + root_first; }
    const Node* root_end() const { return nodes.data() + root_first + root_count; }

    // First child of `block` with the given key, or nullptr.
    const Node* find(const Node& block, Sym key) const;
};

// Lightweight (file, node) handle so compiled code can point back at its source for diagnostics.
struct SrcLoc {
    uint32_t file_id = 0;
    uint32_t line = 0;
};

} // namespace pdx
