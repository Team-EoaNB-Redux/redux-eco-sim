#pragma once

// Minimal event system for script-fired events (`country_event = { id = economy.100 days = 5 }`).
// Supports: trigger, immediate, options with ai_chance { base factor modifier = { factor add <triggers> } },
// option `trigger`, delayed firing. Only events in the scenario's event-file whitelist are loaded; firing
// any other event raises an `event_not_loaded` diagnostic (and does nothing), so missing coverage is visible.
// Player countries pick by the same ai_chance weights unless the scenario says `first_option`.

#include "pdx/interp.h"
#include "world/calendar.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim {

class Events {
public:
    explicit Events(pdx::Interp& in) : in_(in) {}

    void load(const pdx::ScriptFile& f);
    void install_hooks();
    void set_player_first_option(bool v) { player_first_option_ = v; }

    // Runs events whose delay has elapsed. Call once per day.
    void tick(const world::Date& today);
    void fire(pdx::ScopeRef target, pdx::ScopeRef from, pdx::Sym id, int64_t days, pdx::SrcLoc loc);

    size_t loaded() const { return events_.size(); }
    struct FiredRecord {
        std::string date;
        std::string id;
        std::string target;
        int option = -1;
    };
    const std::vector<FiredRecord>& history() const { return history_; }

private:
    struct Option {
        pdx::Fixed base = pdx::Fixed::from_int(1);
        pdx::Fixed factor = pdx::Fixed::from_int(1);
        struct Mod {
            pdx::Fixed factor = pdx::Fixed::from_int(1);
            pdx::Fixed add;
            bool has_add = false;
            pdx::TriggerBlock cond;
        };
        std::vector<Mod> mods;
        pdx::TriggerBlock trigger;
        bool has_trigger = false;
        pdx::EffectBlock effects;
    };
    struct EventDef {
        const pdx::ScriptFile* file = nullptr;
        const pdx::Node* node = nullptr;
        bool compiled = false;
        bool has_trigger = false;
        pdx::TriggerBlock trigger;
        pdx::EffectBlock immediate;
        std::vector<Option> options;
    };
    struct Pending {
        int64_t due = 0;
        pdx::ScopeRef target, from;
        pdx::Sym id = pdx::kNoSym;
        pdx::SrcLoc loc;
    };

    void compile(EventDef& e);
    void execute(EventDef& e, pdx::Sym id, pdx::ScopeRef target, pdx::ScopeRef from);

    pdx::Interp& in_;
    std::unordered_map<pdx::Sym, EventDef> events_;
    std::vector<Pending> pending_;
    std::vector<FiredRecord> history_;
    bool player_first_option_ = false;
};

} // namespace sim
