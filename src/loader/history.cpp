#include "loader/history.h"

#include "loader/mod_fs.h"
#include "pdx/interp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace loader {

using pdx::intern;
using pdx::Node;
using pdx::ScriptFile;
using pdx::sym_view;

namespace {

bool key_is(const Node& n, std::string_view k) { return sym_view(n.key) == k; }

// Undated entries first (in order), then dated blocks <= start, chronologically (stable).
std::vector<const Node*> effective_entries(const ScriptFile& f, const Node* begin, const Node* end, const world::Date& start) {
    std::vector<const Node*> out;
    std::vector<std::pair<int64_t, const Node*>> dated;
    for (const Node* n = begin; n != end; ++n) {
        world::Date d;
        if (n->key != pdx::kNoSym && n->is_block && world::Date::parse(sym_view(n->key), d)) {
            if (d <= start) dated.emplace_back(d.ordinal(), n);
            continue;
        }
        out.push_back(n);
    }
    std::stable_sort(dated.begin(), dated.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [ord, blk] : dated) {
        for (const Node* c = f.begin(*blk); c != f.end(*blk); ++c) out.push_back(c);
    }
    return out;
}

void scan_country(const ScriptFile& f, const Node* begin, const Node* end, HistoryPrescan::CountryInfo& info) {
    for (const Node* n = begin; n != end; ++n) {
        const std::string_view k = sym_view(n->key);
        if (k == "set_autonomy" && n->is_block) {
            for (const Node* c = f.begin(*n); c != f.end(*n); ++c) {
                if (key_is(*c, "target")) info.subjects.push_back(c->value);
            }
        } else if (k == "puppet" && !n->is_block) {
            info.subjects.push_back(n->value);
        } else if (k == "set_variable" && n->is_block) {
            for (const Node* c = f.begin(*n); c != f.end(*n); ++c) {
                if (key_is(*c, "market_leader") && pdx::looks_like_tag(sym_view(c->value))) info.market_leader = c->value;
            }
        } else if (n->is_block) {
            scan_country(f, f.begin(*n), f.end(*n), info);
        }
    }
}

void load_definition_csv(const std::string& root, HistoryPrescan& out) {
    std::ifstream in(join(root, "map/definition.csv"));
    std::string line;
    while (std::getline(in, line)) {
        // id;r;g;b;type;coastal;terrain;continent
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string col;
        while (std::getline(ss, col, ';')) cols.push_back(col);
        if (cols.size() < 6 || cols[0].empty() || !std::all_of(cols[0].begin(), cols[0].end(), ::isdigit)) continue;
        out.coastal_provinces[std::atoi(cols[0].c_str())] = cols[5] == "true";
    }
}

const std::unordered_set<std::string_view>& state_native_keys() {
    static const std::unordered_set<std::string_view> keys = {
        "owner", "controller", "add_core_of", "remove_core_of", "buildings", "victory_points", "add_claim_by",
        "remove_claim_by", "set_province_controller", "buildings_max_level_factor",
        "add_extra_state_shared_building_slots", "set_demilitarized_zone", "set_state_category"};
    return keys;
}

// Runs a history block as effects: undated entries, then dated blocks (<= start) chronologically. Each
// range is compiled sibling-aware so if / else_if / else chains stay together.
void run_history_block(pdx::Interp& in, world::World& w, const ScriptFile& f, const Node* begin, const Node* end,
                       const world::Date& start, pdx::ScopeRef scope, const std::unordered_set<std::string_view>* skip) {
    world::VarStore temps;
    pdx::Ctx c(in, w);
    c.scopes.push_back(scope);
    c.root = scope;
    c.temps = &temps;
    std::vector<std::pair<int64_t, const Node*>> dated;
    auto run_range = [&](const Node* b, const Node* e, bool skip_dates) {
        for (const Node* n = b; n != e; ++n) {
            if (n->key == pdx::kNoSym) continue;
            world::Date d;
            if (n->is_block && world::Date::parse(sym_view(n->key), d)) {
                if (skip_dates && d <= start) dated.emplace_back(d.ordinal(), n);
                continue;
            }
            if (skip && skip->count(sym_view(n->key))) continue;
            const Node* consumed = n;
            if (auto op = in.compile_effect(f, *n, e, &consumed)) op->run(c);
            n = consumed;
        }
    };
    run_range(begin, end, true);
    std::stable_sort(dated.begin(), dated.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [ord, blk] : dated) run_range(f.begin(*blk), f.end(*blk), false);
}

} // namespace

namespace {
void prescan_states(pdx::Interp& in, const std::string& root, const world::Date& start, HistoryPrescan& out,
                    Sym only_owner);
}

void prescan_history(pdx::Interp& in, const std::string& root, const world::Date& start, HistoryPrescan& out,
                     const std::string& history_root) {
    const std::string& hroot = history_root.empty() ? root : history_root;
    for (const auto& path : list_files(hroot, "history/countries")) {
        const ScriptFile* f = load_script(in, hroot, path);
        if (!f) continue;
        std::string name = std::filesystem::path(path).filename().string();
        const Sym tag = intern(name.substr(0, std::min<size_t>(3, name.size())));
        HistoryPrescan::CountryInfo info;
        info.file = f;
        const auto entries = effective_entries(*f, f->root_begin(), f->root_end(), start);
        for (const Node* n : entries) scan_country(*f, n, n + 1, info);
        out.countries[tag] = std::move(info);
    }
    prescan_states(in, hroot, start, out, pdx::kNoSym);
    if (hroot != root) {
        const Sym uct = intern("UCT");
        bool has_uct = false;
        for (const auto& [id, st] : out.states) has_uct = has_uct || st.owner == uct;
        if (!has_uct) prescan_states(in, root, start, out, uct);
    }
    load_definition_csv(root, out);
}

namespace {
void prescan_states(pdx::Interp& in, const std::string& root, const world::Date& start, HistoryPrescan& out,
                    Sym only_owner) {
    for (const auto& path : list_files(root, "history/states")) {
        const ScriptFile* f = load_script(in, root, path);
        if (!f) continue;
        for (const Node* st = f->root_begin(); st != f->root_end(); ++st) {
            if (!key_is(*st, "state") || !st->is_block) continue;
            HistoryPrescan::StateInfo info;
            info.file = f;
            info.block = st;
            int32_t id = 0;
            for (const Node* c = f->begin(*st); c != f->end(*st); ++c) {
                if (key_is(*c, "id")) id = std::atoi(pdx::sym_str(c->value).c_str());
                if (key_is(*c, "history") && c->is_block) {
                    for (const Node* e : effective_entries(*f, f->begin(*c), f->end(*c), start)) {
                        if (key_is(*e, "owner")) info.owner = e->value;
                        else if (key_is(*e, "controller")) info.controller = e->value;
                    }
                }
            }
            // In-game, a state with a controller but no owner is owned by the controller (without a core).
            if (info.owner == pdx::kNoSym) info.owner = info.controller;
            if (id > 0 && (only_owner == pdx::kNoSym || info.owner == only_owner) && !out.states.count(id)) out.states[id] = info;
        }
    }
}
} // namespace

std::unordered_set<Sym> select_countries(const Scenario& sc, const HistoryPrescan& p) {
    std::unordered_set<Sym> sel(sc.countries.begin(), sc.countries.end());
    sel.insert(intern("UCT"));
    if (sc.all_countries) {
        for (const auto& [id, st] : p.states) {
            if (st.owner != pdx::kNoSym) sel.insert(st.owner);
        }
        return sel;
    }
    auto add_subjects = [&]() {
        for (int level = 0; level < 2; ++level) {
            std::vector<Sym> add;
            for (Sym t : sel) {
                auto it = p.countries.find(t);
                if (it == p.countries.end()) continue;
                for (Sym s : it->second.subjects) add.push_back(s);
            }
            sel.insert(add.begin(), add.end());
        }
    };
    if (sc.include_subjects) add_subjects();
    if (sc.include_market_members) {
        std::vector<Sym> add;
        for (const auto& [tag, info] : p.countries) {
            if (info.market_leader != pdx::kNoSym && sel.count(info.market_leader)) add.push_back(tag);
        }
        sel.insert(add.begin(), add.end());
        if (sc.include_subjects) add_subjects();
    }
    return sel;
}

void apply_history(pdx::Interp& in, world::World& w, const ModData& d, const HistoryPrescan& p,
                   const std::unordered_set<Sym>& selected, const world::Date& start, const StateSetup& setup) {
    for (Sym tag : d.country_tags) w.ensure_country(tag);
    for (const auto& [tag, info] : p.countries) w.ensure_country(tag);

    // ---- states, structural pass ----
    std::vector<int32_t> loaded;
    for (const auto& [id, info] : p.states) {
        // UCT's own states always load: UCT must exist to host on_monthly_UCT / on_weekly_UCT
        static const Sym uct = intern("UCT");
        const bool listed = setup.only_states.count(id) != 0;
        if (!setup.only_states.empty() ? !(listed || info.owner == uct)
                                       : (info.owner == pdx::kNoSym || !selected.count(info.owner))) continue;
        const ScriptFile& f = *info.file;
        world::State& st = w.ensure_state(id);
        for (const Node* c = f.begin(*info.block); c != f.end(*info.block); ++c) {
            const std::string_view k = sym_view(c->key);
            if (k == "name") st.name = c->value;
            else if (k == "manpower") pdx::Fixed::parse(sym_view(c->value), st.manpower);
            else if (k == "state_category") st.category = c->value;
            else if (k == "resources" && c->is_block) {
                for (const Node* r = f.begin(*c); r != f.end(*c); ++r) {
                    pdx::Fixed amt;
                    if (r->key != pdx::kNoSym && pdx::Fixed::parse(sym_view(r->value), amt)) st.resources[r->key] = amt;
                }
            } else if (k == "provinces" && c->is_block) {
                for (const Node* pr = f.begin(*c); pr != f.end(*c); ++pr) {
                    const int32_t pid = std::atoi(pdx::sym_str(pr->value).c_str());
                    st.provinces.push_back(pid);
                    auto it = p.coastal_provinces.find(pid);
                    if (it != p.coastal_provinces.end() && it->second) st.coastal = true;
                }
            } else if (k == "history" && c->is_block) {
                for (const Node* e : effective_entries(f, f.begin(*c), f.end(*c), start)) {
                    const std::string_view ek = sym_view(e->key);
                    if (ek == "owner" || ek == "controller") {
                        const int32_t ci = w.ensure_country(e->value);
                        if (ek == "owner") {
                            st.owner = ci;
                            if (st.controller < 0) st.controller = ci;
                        } else {
                            st.controller = ci;
                        }
                    } else if (ek == "add_core_of" || ek == "remove_core_of") {
                        const int32_t ci = w.ensure_country(e->value);
                        auto it = std::find(st.cores.begin(), st.cores.end(), ci);
                        if (ek == "add_core_of" && it == st.cores.end()) st.cores.push_back(ci);
                        if (ek == "remove_core_of" && it != st.cores.end()) st.cores.erase(it);
                    } else if (ek == "buildings" && e->is_block) {
                        for (const Node* b = f.begin(*e); b != f.end(*e); ++b) {
                            if (b->is_block) { // province-level buildings: count toward the state total
                                for (const Node* pb = f.begin(*b); pb != f.end(*b); ++pb) {
                                    pdx::Fixed lv;
                                    if (pdx::Fixed::parse(sym_view(pb->value), lv)) st.buildings[pb->key] += static_cast<int32_t>(lv.whole_trunc());
                                }
                                continue;
                            }
                            pdx::Fixed lv;
                            if (pdx::Fixed::parse(sym_view(b->value), lv)) st.buildings[b->key] = static_cast<int32_t>(lv.whole_trunc());
                        }
                    }
                }
            }
        }
        if (setup.force_owner != pdx::kNoSym && (listed || setup.only_states.empty())) {
            const int32_t ci = w.ensure_country(setup.force_owner);
            st.owner = st.controller = ci;
            st.cores.assign(1, ci);
        }
        if (st.owner < 0) st.owner = st.controller; // controller without owner: owned, but no core
        if (st.controller < 0) st.controller = st.owner;
        loaded.push_back(id);
    }
    w.rebuild_state_lists();

    // ---- countries ----
    for (Sym tag : d.country_tags) {
        if (!selected.count(tag)) continue;
        auto it = p.countries.find(tag);
        if (it == p.countries.end()) continue;
        const ScriptFile& f = *it->second.file;
        const int32_t ci = w.country_index(tag);
        world::Country& co = w.countries[static_cast<size_t>(ci)];
        static const std::unordered_set<std::string_view> country_native = {"capital", "oob"};
        for (const Node* e : effective_entries(f, f.root_begin(), f.root_end(), start)) {
            if (key_is(*e, "capital")) co.capital = std::atoi(pdx::sym_str(e->value).c_str());
        }
        run_history_block(in, w, f, f.root_begin(), f.root_end(), start, pdx::ScopeRef::country(ci), &country_native);
    }

    // ---- states, effect pass ----
    for (int32_t id : loaded) {
        const auto& info = p.states.at(id);
        const ScriptFile& f = *info.file;
        for (const Node* c = f.begin(*info.block); c != f.end(*info.block); ++c) {
            if (!key_is(*c, "history") || !c->is_block) continue;
            run_history_block(in, w, f, f.begin(*c), f.end(*c), start, pdx::ScopeRef::state(id), &state_native_keys());
        }
    }
    w.rebuild_state_lists();
}

} // namespace loader
