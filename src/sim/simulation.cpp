#include "sim/simulation.h"

#include "loader/mod_fs.h"
#include "pdx/parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace sim {

using pdx::ScopeRef;
using pdx::Sym;

namespace {

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

Simulation::Simulation() = default;
Simulation::~Simulation() = default;

bool Simulation::load(const std::string& mod_root, const std::string& scenario_path, std::string& error) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!loader::load_scenario(scenario_path, scenario_, error)) return false;
    if (!semantics_overrides.empty() && !loader::apply_semantics_text(semantics_overrides, scenario_.semantics, error)) return false;

    interp_ = std::make_unique<pdx::Interp>(world_, diags_, scenario_.semantics);
    interp_->log_sink = [this](const std::string& line) { log_.push_back("[" + world_.date.to_string() + "] " + line); };
    interp_->set_skipped_effects(scenario_.skip_effects);

    loader::load_mod_data(mod_root, *interp_, data_);
    loader::load_localisation(mod_root, world_.localisation);
    if (!scenario_.history_dir.empty()) loader::load_localisation(scenario_.history_dir, world_.localisation);
    interp_->hooks.is_token = [this](Sym s) { return data_.tokens.count(s) != 0; };

    mods_ = std::make_unique<ModifierEngine>(data_, world_, *interp_);
    mods_->install_hooks();
    events_ = std::make_unique<Events>(*interp_);
    events_->install_hooks();
    events_->set_player_first_option(scenario_.player_first_option);
    for (const auto& name : scenario_.event_files) {
        const std::string path = loader::join(loader::join(mod_root, "events"), name);
        if (!std::filesystem::exists(path)) {
            error = "event file not found: " + path;
            return false;
        }
        if (const pdx::ScriptFile* f = loader::load_script(*interp_, mod_root, path)) events_->load(*f);
    }

    world_.date = scenario_.start_date;
    world_.rng.reseed(scenario_.seed);
    start_ordinal_ = world_.date.ordinal();

    loader::prescan_history(*interp_, mod_root, scenario_.start_date, prescan_, scenario_.history_dir);
    const auto selected = loader::select_countries(scenario_, prescan_);
    loader::StateSetup setup;
    setup.only_states.insert(scenario_.states.begin(), scenario_.states.end());
    setup.force_owner = scenario_.state_owner;
    loader::apply_history(*interp_, world_, data_, prescan_, selected, scenario_.start_date, setup);

    for (Sym tag : scenario_.players) {
        if (world::Country* c = world_.country(world_.country_index(tag))) c->is_ai = false;
    }
    for (const auto& sv : scenario_.engine) {
        world::Country* c = world_.country(world_.country_index(sv.tag));
        if (!c) continue;
        for (const auto& [k, v] : sv.values) {
            if (pdx::sym_view(k) == "stability") pdx::Fixed::parse(v, c->stability); // a modelled stat, not a lookup
            else pdx::Fixed::parse(v, c->engine[k]);
        }
    }
    for (const auto& sv : scenario_.units) {
        world::Country* c = world_.country(world_.country_index(sv.tag));
        if (!c) continue;
        for (const auto& [k, v] : sv.values) {
            pdx::Fixed n;
            pdx::Fixed::parse(v, n);
            c->battalions[k] = static_cast<int32_t>(n.whole_trunc());
        }
    }
    mods_->mark_all_dirty();
    collect_on_actions();
    load_seconds = seconds_since(t0);
    return true;
}

void Simulation::collect_on_actions() {
    for (const auto& wanted : scenario_.on_action_files) {
        const pdx::ScriptFile* file = nullptr;
        for (const pdx::ScriptFile* f : data_.on_action_files) {
            if (std::filesystem::path(f->path).filename().string() == wanted) file = f;
        }
        if (!file) {
            diags_.report(DiagKind::BadParameter, scenario_.path, 0, "on_action file not found: " + wanted);
            continue;
        }
        for (const pdx::Node* root = file->root_begin(); root != file->root_end(); ++root) {
            if (pdx::sym_view(root->key) != "on_actions" || !root->is_block) continue;
            for (const pdx::Node* oa = file->begin(*root); oa != file->end(*root); ++oa) {
                if (!oa->is_block) continue;
                for (const pdx::Node* e = file->begin(*oa); e != file->end(*oa); ++e) {
                    if (pdx::sym_view(e->key) == "effect" && e->is_block) {
                        on_actions_[pdx::sym_str(oa->key)].push_back({file, e, nullptr});
                    }
                }
            }
        }
    }
}

void Simulation::fire(const std::string& name, ScopeRef scope) {
    auto it = on_actions_.find(name);
    if (it == on_actions_.end()) return;
    for (auto& block : it->second) {
        if (!block.compiled) block.compiled = std::make_shared<pdx::EffectBlock>(interp_->compile_effects(*block.file, *block.effect));
        interp_->run(*block.compiled, scope, scope);
    }
}

void Simulation::startup() {
    const auto t0 = std::chrono::steady_clock::now();
    const int32_t uct = world_.country_index(pdx::intern("UCT"));
    ScopeRef scope = uct >= 0 ? ScopeRef::country(uct) : ScopeRef::none();
    if (!scope.valid()) {
        const auto existing = world_.existing_countries();
        if (!existing.empty()) scope = ScopeRef::country(existing.front());
    }
    apply_pins();
    mods_->daily_refresh();
    fire("on_startup", scope);
    apply_overrides();
    mods_->daily_refresh();
    startup_seconds = seconds_since(t0);
}

void Simulation::apply_overrides() {
    for (const auto& [ref, value] : scenario_.overrides) {
        std::vector<std::string> ignored;
        std::string error;
        if (!run_effect(ScopeRef::global(), "set_variable = { " + ref + " = " + value + " }", ignored, error)) {
            diags_.report(DiagKind::BadParameter, scenario_.path, 0, "override " + ref + ": " + error);
        }
    }
}

void Simulation::apply_pins() {
    for (const auto& [ref, value] : scenario_.pins) {
        pdx::VarRef vr;
        std::string error;
        pdx::Fixed v;
        pdx::Interp::Slot slot;
        world::VarStore temps;
        pdx::Ctx c(*interp_, world_);
        c.scopes.push_back(ScopeRef::global());
        c.root = ScopeRef::global();
        c.temps = &temps;
        if (!pdx::parse_varref(ref, vr, &error) || !pdx::Fixed::parse(value, v) ||
            !interp_->resolve_slot(c, vr, false, pdx::SrcLoc{}, slot) || !slot.store || slot.is_array_element) {
            diags_.report(DiagKind::BadParameter, scenario_.path, 0, "pin " + ref + " = " + value + ": cannot resolve");
            continue;
        }
        slot.store->pin(slot.key, pdx::Value::number(v));
    }
}

void Simulation::step_day() {
    const auto t0 = std::chrono::steady_clock::now();
    world_.date = world_.date.plus_days(1);
    mods_->daily_refresh();
    events_->tick(world_.date);

    const bool monthly = world_.date.day == 1;
    const bool weekly = (world_.date.ordinal() - start_ordinal_) % 7 == 0;
    auto fire_tagged = [&](const char* prefix) {
        for (const auto& [name, blocks] : on_actions_) {
            if (name.rfind(prefix, 0) != 0) continue;
            const std::string tag = name.substr(std::string(prefix).size());
            const int32_t ci = world_.country_index(pdx::intern(tag));
            if (ci >= 0 && world_.countries[static_cast<size_t>(ci)].exists) fire(name, ScopeRef::country(ci));
        }
    };
    auto do_monthly = [&]() {
        if (!monthly) return;
        fire_tagged("on_monthly_");
        ++months_;
        if (on_month) on_month();
    };
    auto do_weekly = [&]() {
        if (weekly) fire_tagged("on_weekly_");
    };
    if (interp_->semantics().monthly_before_weekly) {
        do_monthly();
        do_weekly();
    } else {
        do_weekly();
        do_monthly();
    }
    tick_seconds += seconds_since(t0);
}

void Simulation::run_days(int n) {
    for (int i = 0; i < n; ++i) step_day();
}

void Simulation::run_months(int n) {
    const int target = months_ + n;
    while (months_ < target) step_day();
}

ScopeRef Simulation::parse_scope(const std::string& text) const {
    if (text.empty() || text == "global") return ScopeRef::global();
    if (std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int32_t id = std::stoi(text);
        return world_.state(id) ? ScopeRef::state(id) : ScopeRef::none();
    }
    const int32_t ci = world_.country_index(pdx::intern(text));
    return ci >= 0 ? ScopeRef::country(ci) : ScopeRef::none();
}

bool Simulation::eval(const std::string& ref, pdx::Value& out, std::string& error) {
    pdx::VarRef vr;
    if (!pdx::parse_varref(ref, vr, &error)) return false;
    world::VarStore temps;
    pdx::Ctx c(*interp_, world_);
    c.scopes.push_back(ScopeRef::global());
    c.root = ScopeRef::global();
    c.temps = &temps;
    out = interp_->read(c, vr, pdx::SrcLoc{});
    return true;
}

bool Simulation::run_effect(ScopeRef scope, const std::string& text, std::vector<std::string>& log_out, std::string& error) {
    if (!scope.valid()) {
        error = "invalid scope";
        return false;
    }
    pdx::ScriptFile f;
    f.path = "<console>";
    pdx::parse_text(text, f);
    if (!f.errors.empty()) {
        error = "parse error: " + f.errors.front().message;
        return false;
    }
    const pdx::ScriptFile* stored = interp_->add_file(std::move(f));
    pdx::Node root;
    root.is_block = true;
    root.first_child = stored->root_first;
    root.child_count = stored->root_count;
    const pdx::EffectBlock block = interp_->compile_effects(*stored, root);
    const size_t before = log_.size();
    interp_->run(block, scope, scope);
    log_out.assign(log_.begin() + static_cast<std::ptrdiff_t>(before), log_.end());
    return true;
}

} // namespace sim
