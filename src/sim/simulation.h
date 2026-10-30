#pragma once

// Simulation facade — the single entry point the CLI and the GDExtension use:
//   load(mod, scenario) -> startup() -> run_months(n) / step_day() -> queries
// Owns the world, interpreter, definitions, modifier engine, events and the on_action schedule.

#include "loader/history.h"
#include "loader/mod_data.h"
#include "loader/scenario.h"
#include "pdx/interp.h"
#include "sim/diagnostics.h"
#include "sim/events.h"
#include "sim/modifiers.h"
#include "world/world.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim {

class Simulation {
public:
    Simulation();
    ~Simulation();

    bool load(const std::string& mod_root, const std::string& scenario_path, std::string& error);
    // `key = value` semantics overrides applied on top of the scenario's (call before load).
    std::string semantics_overrides;
    // Runs the whitelisted on_startup blocks, then applies scenario overrides.
    void startup();
    void step_day();
    void run_days(int n);
    void run_months(int n);

    // Called after every monthly tick (recorder hook).
    std::function<void()> on_month;

    // ---- queries ----
    // `ref` = HOI4 variable reference with an optional leading scope: "ENG.treasury", "952.base_wage",
    // "global.base_price@token:steel", "ENG.pops@token:laborers", "ENG.modifier@incentives_modifier".
    bool eval(const std::string& ref, pdx::Value& out, std::string& error);
    // Runs effect text in a scope (like the in-game `effect` console command). Returns log lines.
    bool run_effect(pdx::ScopeRef scope, const std::string& text, std::vector<std::string>& log_out, std::string& error);
    pdx::ScopeRef parse_scope(const std::string& text) const; // "ENG", "952", "global"

    world::World& world() { return world_; }
    pdx::Interp& interp() { return *interp_; }
    Diagnostics& diagnostics() { return diags_; }
    const loader::Scenario& scenario() const { return scenario_; }
    const loader::ModData& data() const { return data_; }
    ModifierEngine& modifiers() { return *mods_; }
    Events& events() { return *events_; }
    const std::vector<std::string>& log() const { return log_; }
    int months_elapsed() const { return months_; }
    std::string mod_root() const { return data_.root; }

    // Timing of the last load/startup and cumulative tick time (seconds).
    double load_seconds = 0, startup_seconds = 0, tick_seconds = 0;

private:
    struct OnActionBlock {
        const pdx::ScriptFile* file = nullptr;
        const pdx::Node* effect = nullptr;
        std::shared_ptr<pdx::EffectBlock> compiled;
    };
    void collect_on_actions();
    void fire(const std::string& on_action, pdx::ScopeRef scope);
    void apply_overrides();
    void apply_pins();

    world::World world_;
    Diagnostics diags_;
    std::unique_ptr<pdx::Interp> interp_;
    loader::ModData data_;
    loader::Scenario scenario_;
    loader::HistoryPrescan prescan_;
    std::unique_ptr<ModifierEngine> mods_;
    std::unique_ptr<Events> events_;
    std::unordered_map<std::string, std::vector<OnActionBlock>> on_actions_;
    std::vector<std::string> log_;
    int64_t start_ordinal_ = 0;
    int months_ = 0;
};

} // namespace sim
