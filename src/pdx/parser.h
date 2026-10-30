#pragma once

#include "pdx/ast.h"

#include <string>
#include <string_view>

namespace pdx {

// Parses Clausewitz text. Never throws; problems are recorded in ScriptFile::errors and parsing recovers
// (stray '}' skipped, unclosed blocks closed at EOF, missing values reported).
// Handles: UTF-8 BOM, '#' comments, quoted strings with \" escapes, operators = < > <= >= != ==,
// tagged blocks (`rgb { }`), bare-value lists, and file-local `@name = value` constants (substituted when
// `@name` later appears as a value).
void parse_text(std::string_view text, ScriptFile& out);

// Reads and parses a file. Returns false (with an error recorded) if the file cannot be read.
bool parse_file(const std::string& path, ScriptFile& out);

// Writes the children of `block` back out as Clausewitz text (used by meta_effect, whose `text = { }` is
// re-parsed after `[PARAM]` substitution). Quoted values are re-quoted.
std::string serialize_children(const ScriptFile& f, const Node& block);

} // namespace pdx
