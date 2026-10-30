#include "loader/mod_data.h"

#include "loader/mod_fs.h"
#include "pdx/interp.h"
#include "pdx/parser.h"

#include <fstream>

namespace loader {

using pdx::intern;
using pdx::Node;
using pdx::ScriptFile;
using pdx::sym_view;

namespace {

bool key_is(const Node& n, std::string_view k) { return sym_view(n.key) == k; }

void add_block_keys_as_tokens(const ScriptFile& f, const Node& block, std::unordered_set<Sym>& tokens) {
    for (const Node* c = f.begin(block); c != f.end(block); ++c) {
        if (c->key != pdx::kNoSym && c->is_block) tokens.insert(c->key);
    }
}

void load_script_enums(pdx::Interp& in, ModData& d) {
    const std::string path = join(d.root, "common/script_enums.txt");
    const ScriptFile* f = load_script(in, d.root, path);
    if (!f) return;
    for (const Node* e = f->root_begin(); e != f->root_end(); ++e) {
        if (!e->is_block) continue;
        for (const Node* v = f->begin(*e); v != f->end(*e); ++v) {
            if (v->key == pdx::kNoSym) d.tokens.insert(v->value);
        }
    }
}

void load_country_tags(pdx::Interp& in, ModData& d) {
    for (const auto& path : list_files(d.root, "common/country_tags")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* n = f->root_begin(); n != f->root_end(); ++n) {
            if (n->key == pdx::kNoSym || n->is_block || key_is(*n, "dynamic_tags")) continue;
            if (pdx::looks_like_tag(sym_view(n->key))) d.country_tags.push_back(n->key);
        }
    }
}

void load_modifier_definitions(pdx::Interp& in, ModData& d) {
    // A definition is any block containing category / value_type / color_type, at any depth.
    std::vector<std::pair<const ScriptFile*, const Node*>> stack;
    for (const auto& path : list_files(d.root, "common/modifier_definitions")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* n = f->root_begin(); n != f->root_end(); ++n) stack.emplace_back(f, n);
    }
    while (!stack.empty()) {
        auto [f, n] = stack.back();
        stack.pop_back();
        if (!n->is_block) continue;
        bool is_def = false;
        for (const Node* c = f->begin(*n); c != f->end(*n); ++c) {
            if (key_is(*c, "category") || key_is(*c, "value_type") || key_is(*c, "color_type")) is_def = true;
            if (c->is_block) stack.emplace_back(f, c);
        }
        if (is_def && n->key != pdx::kNoSym) {
            d.modifier_defined.insert(n->key);
            d.tokens.insert(n->key);
        }
    }
}

void load_ideas(pdx::Interp& in, ModData& d) {
    for (const auto& path : list_files(d.root, "common/ideas")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* root = f->root_begin(); root != f->root_end(); ++root) {
            if (!key_is(*root, "ideas") || !root->is_block) continue;
            for (const Node* cat = f->begin(*root); cat != f->end(*root); ++cat) {
                if (!cat->is_block) continue;
                for (const Node* idea = f->begin(*cat); idea != f->end(*cat); ++idea) {
                    if (idea->key == pdx::kNoSym || !idea->is_block) continue;
                    d.ideas.insert(idea->key);
                    d.tokens.insert(idea->key);
                    std::vector<ModEntry> mods;
                    for (const Node* c = f->begin(*idea); c != f->end(*idea); ++c) {
                        if (key_is(*c, "modifier") && c->is_block) collect_modifiers(*f, *c, mods);
                    }
                    if (!mods.empty()) d.idea_modifiers[idea->key] = std::move(mods);
                }
            }
        }
    }
}

void load_technologies(pdx::Interp& in, ModData& d) {
    for (const auto& path : list_files(d.root, "common/technologies")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* root = f->root_begin(); root != f->root_end(); ++root) {
            if (!key_is(*root, "technologies") || !root->is_block) continue;
            for (const Node* t = f->begin(*root); t != f->end(*root); ++t) {
                if (t->key == pdx::kNoSym || !t->is_block) continue;
                TechDef td;
                td.name = t->key;
                td.file = f;
                for (const Node* c = f->begin(*t); c != f->end(*t); ++c) {
                    if (key_is(*c, "modifier") && c->is_block) collect_modifiers(*f, *c, td.modifiers);
                    else if (key_is(*c, "on_research_complete") && c->is_block) td.on_research_complete = c;
                    else if (key_is(*c, "start_year")) {
                        Fixed y;
                        if (Fixed::parse(sym_view(c->value), y)) td.start_year = static_cast<int>(y.whole_trunc());
                    } else if (!c->is_block && c->key != pdx::kNoSym && !key_is(*c, "research_cost") &&
                               sym_view(c->key).rfind("xp_", 0) != 0) {
                        // country modifiers sit directly in the tech body (`tax_efficiency_factor = 0.02`);
                        // unit-stat blocks (`infantry = { ... }`) are not country modifiers
                        Fixed v;
                        if (Fixed::parse(sym_view(c->value), v)) td.modifiers.push_back({c->key, v});
                    }
                }
                d.tokens.insert(t->key);
                d.techs[t->key] = std::move(td);
            }
        }
    }
}

void load_dynamic_modifiers(pdx::Interp& in, ModData& d) {
    for (const auto& path : list_files(d.root, "common/dynamic_modifiers")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* n = f->root_begin(); n != f->root_end(); ++n) {
            if (n->key == pdx::kNoSym || !n->is_block) continue;
            DynamicModifierDef def;
            def.name = n->key;
            def.file = f;
            for (const Node* c = f->begin(*n); c != f->end(*n); ++c) {
                if (key_is(*c, "enable")) { def.enable = c; continue; }
                if (key_is(*c, "remove_trigger")) { def.remove_trigger = c; continue; }
                if (c->is_block || key_is(*c, "icon") || key_is(*c, "attacker_modifier") || c->key == pdx::kNoSym) continue;
                DynModEntry e;
                e.name = c->key;
                if (!Fixed::parse(sym_view(c->value), e.value)) {
                    e.is_var = true;
                    std::string err;
                    if (!pdx::parse_varref(sym_view(c->value), e.var, &err)) {
                        in.diag(sim::DiagKind::BadParameter, in.loc(*f, *c), "dynamic modifier value '" + pdx::sym_str(c->value) + "': " + err);
                        continue;
                    }
                }
                d.modifier_defined.insert(e.name);
                def.entries.push_back(std::move(e));
            }
            d.tokens.insert(def.name);
            d.dynamic_modifiers[def.name] = std::move(def);
        }
    }
}

void load_simple_modifier_blocks(pdx::Interp& in, ModData& d, const char* dir, const char* wrapper,
                                 std::unordered_map<Sym, std::vector<ModEntry>>& out) {
    for (const auto& path : list_files(d.root, dir)) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* root = f->root_begin(); root != f->root_end(); ++root) {
            if (!root->is_block) continue;
            const bool wrapped = wrapper && key_is(*root, wrapper);
            if (wrapper && !wrapped) continue;
            auto handle = [&](const Node* n) {
                if (n->key == pdx::kNoSym || !n->is_block) return;
                std::vector<ModEntry> mods;
                collect_modifiers(*f, *n, mods);
                for (const auto& m : mods) d.modifier_defined.insert(m.name);
                d.tokens.insert(n->key);
                out[n->key] = std::move(mods);
            };
            if (wrapped) {
                for (const Node* n = f->begin(*root); n != f->end(*root); ++n) handle(n);
            } else {
                handle(root);
            }
        }
    }
}

void load_misc_tokens(pdx::Interp& in, ModData& d) {
    for (const auto& path : list_files(d.root, "common/resources")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* r = f->root_begin(); r != f->root_end(); ++r) {
            if (!key_is(*r, "resources") || !r->is_block) continue;
            for (const Node* c = f->begin(*r); c != f->end(*r); ++c) {
                if (c->key != pdx::kNoSym && c->is_block) {
                    d.tokens.insert(c->key);
                    d.resources.push_back(c->key);
                }
            }
        }
    }
    for (const auto& path : list_files(d.root, "common/buildings")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* r = f->root_begin(); r != f->root_end(); ++r) {
            if (key_is(*r, "buildings") && r->is_block) add_block_keys_as_tokens(*f, *r, d.tokens);
        }
    }
    for (const auto& path : list_files(d.root, "common/units", ".txt", false)) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* r = f->root_begin(); r != f->root_end(); ++r) {
            if (key_is(*r, "sub_units") && r->is_block) add_block_keys_as_tokens(*f, *r, d.tokens);
        }
    }
    for (const auto& path : list_files(d.root, "common/units/equipment")) {
        const ScriptFile* f = load_script(in, d.root, path);
        if (!f) continue;
        for (const Node* r = f->root_begin(); r != f->root_end(); ++r) {
            if (key_is(*r, "equipments") && r->is_block) add_block_keys_as_tokens(*f, *r, d.tokens);
        }
    }
}

} // namespace

void collect_modifiers(const ScriptFile& f, const Node& block, std::vector<ModEntry>& out) {
    for (const Node* c = f.begin(block); c != f.end(block); ++c) {
        if (c->is_block) {
            if (key_is(*c, "hidden_modifier")) collect_modifiers(f, *c, out);
            continue;
        }
        if (c->key == pdx::kNoSym) continue;
        Fixed v;
        if (!Fixed::parse(sym_view(c->value), v)) continue; // custom_modifier_tooltip etc.
        out.push_back({c->key, v});
    }
}

const ScriptFile* load_script(pdx::Interp& interp, const std::string& root, const std::string& path) {
    ScriptFile f;
    pdx::parse_file(path, f);
    f.path = relative_to(path, root);
    const ScriptFile* stored = interp.add_file(std::move(f));
    for (const auto& e : stored->errors) {
        interp.diagnostics().report(sim::DiagKind::ParseError, stored->path, e.line, e.message);
    }
    return stored;
}

bool load_mod_data(const std::string& root, pdx::Interp& interp, ModData& d) {
    d.root = root;
    load_country_tags(interp, d);
    load_script_enums(interp, d);
    load_modifier_definitions(interp, d);
    load_ideas(interp, d);
    load_technologies(interp, d);
    load_dynamic_modifiers(interp, d);
    load_simple_modifier_blocks(interp, d, "common/state_category", "state_categories", d.state_categories);
    load_simple_modifier_blocks(interp, d, "common/modifiers", nullptr, d.static_modifiers);
    load_misc_tokens(interp, d);

    for (const auto& [name, mods] : d.idea_modifiers) {
        for (const auto& m : mods) d.modifier_defined.insert(m.name);
    }
    for (const auto& [name, t] : d.techs) {
        for (const auto& m : t.modifiers) d.modifier_defined.insert(m.name);
    }

    for (const auto& path : list_files(root, "common/scripted_effects")) {
        if (const ScriptFile* f = load_script(interp, root, path)) interp.index_scripted_effects(*f);
    }
    for (const auto& path : list_files(root, "common/scripted_triggers")) {
        if (const ScriptFile* f = load_script(interp, root, path)) interp.index_scripted_triggers(*f);
    }
    for (const auto& path : list_files(root, "common/on_actions")) {
        if (const ScriptFile* f = load_script(interp, root, path)) d.on_action_files.push_back(f);
    }
    return true;
}

void load_localisation(const std::string& root, std::unordered_map<std::string, std::string>& out) {
    for (const auto& path : list_files(root, "localisation/english", ".yml")) {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            size_t b = line.find_first_not_of(" \t\xEF\xBB\xBF"); // indentation + UTF-8 BOM
            if (b == std::string::npos || line[b] == '#') continue;
            const size_t colon = line.find(':', b);
            const size_t q1 = line.find('"', colon == std::string::npos ? b : colon);
            const size_t q2 = line.rfind('"');
            if (colon == std::string::npos || q1 == std::string::npos || q2 <= q1) continue;
            out[line.substr(b, colon - b)] = line.substr(q1 + 1, q2 - q1 - 1);
        }
    }
}

} // namespace loader
