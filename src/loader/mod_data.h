#pragma once

// Definition data loaded from the mod (everything except history): tokens, country tags, ideas, technologies,
// modifier definitions, dynamic/static modifiers, state categories. Script files are handed to the
// interpreter (which owns their storage), and scripted effects/triggers are indexed there.

#include "pdx/ast.h"
#include "pdx/fixed.h"
#include "pdx/varref.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pdx {
class Interp;
}
namespace sim {
class Diagnostics;
}

namespace loader {

using pdx::Fixed;
using pdx::Sym;

struct ModEntry {
    Sym name = pdx::kNoSym;
    Fixed value;
};

struct DynModEntry {
    Sym name = pdx::kNoSym;
    bool is_var = false;
    Fixed value;
    pdx::VarRef var;
};

struct DynamicModifierDef {
    Sym name = pdx::kNoSym;
    std::vector<DynModEntry> entries;
    const pdx::ScriptFile* file = nullptr;
    const pdx::Node* enable = nullptr;
    const pdx::Node* remove_trigger = nullptr;
};

struct TechDef {
    Sym name = pdx::kNoSym;
    std::vector<ModEntry> modifiers;
    const pdx::ScriptFile* file = nullptr;
    const pdx::Node* on_research_complete = nullptr;
    int start_year = 0;
};

struct ModData {
    std::string root;
    std::vector<Sym> country_tags;              // load order
    std::unordered_set<Sym> tokens;             // script_enums, ideas, techs, resources, units, buildings, ...
    std::unordered_set<Sym> ideas;
    std::unordered_map<Sym, std::vector<ModEntry>> idea_modifiers;
    std::unordered_map<Sym, TechDef> techs;
    std::unordered_set<Sym> modifier_defined;   // modifier_definitions + every name some modifier source provides
    std::unordered_map<Sym, DynamicModifierDef> dynamic_modifiers;
    std::unordered_map<Sym, std::vector<ModEntry>> state_categories;
    std::unordered_map<Sym, std::vector<ModEntry>> static_modifiers;
    std::vector<Sym> resources;
    // on_actions: file name -> parsed file (whitelisting happens in the simulation)
    std::vector<const pdx::ScriptFile*> on_action_files;
    std::vector<const pdx::ScriptFile*> event_files;
    size_t parse_errors = 0;
};

// Loads definitions and indexes every scripted effect/trigger into `interp`.
bool load_mod_data(const std::string& root, pdx::Interp& interp, ModData& out);

// localisation/english/*.yml -> key -> text (`KEY:0 "text"`; later files win).
void load_localisation(const std::string& root, std::unordered_map<std::string, std::string>& out);

// Parses a file into the interpreter's file store; parse errors become diagnostics.
const pdx::ScriptFile* load_script(pdx::Interp& interp, const std::string& root, const std::string& path);

// Flattens a `modifier = { ... hidden_modifier = { ... } }` block into numeric entries.
void collect_modifiers(const pdx::ScriptFile& f, const pdx::Node& block, std::vector<ModEntry>& out);

} // namespace loader
