#pragma once

// modifier@x = cached sum over a scope's modifier sources:
//   country: ideas, researched techs, the engine's static `country_is_at_peace` / `country_is_at_war`,
//            dynamic modifiers
//   state:   state category, dynamic modifiers
// Variable-valued dynamic-modifier entries are evaluated at refresh time only (daily, on add, and on
// force_update_dynamic_modifier) — the same one-day lag the game has.

#include "loader/mod_data.h"
#include "pdx/interp.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim {

class ModifierEngine {
public:
    ModifierEngine(const loader::ModData& d, world::World& w, pdx::Interp& in);

    pdx::Fixed value(pdx::ScopeRef s, pdx::Sym m);
    bool defined(pdx::Sym m) const { return d_.modifier_defined.count(m) != 0; }
    void mark_dirty(pdx::ScopeRef s);
    void mark_all_dirty();
    void refresh_dynamic(pdx::ScopeRef s);
    // Start-of-day processing: expiry, remove_trigger, re-evaluation of variable-valued entries.
    void daily_refresh();
    // Installs modifier_value / modifier_defined / modifiers_dirty / refresh_dynamic_modifiers hooks.
    void install_hooks();

    struct Source {
        std::string source; // "idea free_trade", "tech x", "static country_is_at_peace", "dynamic resource_trade"
        pdx::Fixed value;
    };
    std::vector<Source> breakdown(pdx::ScopeRef s, pdx::Sym m);

private:
    struct ScopeCache {
        bool dirty = true;
        bool dyn_valid = false;
        std::unordered_map<pdx::Sym, pdx::Fixed> sums;
        std::unordered_map<pdx::Sym, pdx::Fixed> dyn;
    };
    static uint64_t key(pdx::ScopeRef s) { return (static_cast<uint64_t>(s.type) << 32) | static_cast<uint32_t>(s.id); }
    ScopeCache& cache(pdx::ScopeRef s) { return caches_[key(s)]; }
    void rebuild(pdx::ScopeRef s, ScopeCache& c);
    void eval_dynamic(pdx::ScopeRef s, ScopeCache& c);
    bool test_trigger(const loader::DynamicModifierDef& def, const pdx::Node* node, pdx::ScopeRef s,
                      std::unordered_map<pdx::Sym, std::unique_ptr<pdx::TriggerBlock>>& cache);
    template <typename F>
    void for_static_sources(pdx::ScopeRef s, F&& fn);

    const loader::ModData& d_;
    world::World& w_;
    pdx::Interp& in_;
    std::unordered_map<uint64_t, ScopeCache> caches_;
    std::unordered_map<pdx::Sym, std::unique_ptr<pdx::TriggerBlock>> enable_cache_;
    std::unordered_map<pdx::Sym, std::unique_ptr<pdx::TriggerBlock>> remove_cache_;
};

} // namespace sim
