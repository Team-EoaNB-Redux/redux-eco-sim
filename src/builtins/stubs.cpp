// (1) Real effects used by history and occasionally by script: set_autonomy/puppet, cores, capital, stats.
// (2) Explicit STUBS: HOI4 effects/triggers with no economic meaning (politics, characters, doctrines,
//     equipment, diplomacy UI). They compile and do nothing; `ecosim_cli coverage` lists any stub reachable
//     from the eco on_actions, so a stub that starts to matter is visible instead of silently ignored.

#include "builtins/common.h"

#include <algorithm>

namespace pdx {

namespace {

using sim::DiagKind;

struct Nothing : EffectOp {
    void run(Ctx&) const override {}
};
std::unique_ptr<EffectOp> stub_effect(Interp&, const ScriptFile&, const Node&) { return std::make_unique<Nothing>(); }

struct ConstTrigger : TriggerOp {
    bool v = false;
    bool eval(Ctx&) const override { return v; }
};
std::unique_ptr<TriggerOp> stub_false(Interp&, const ScriptFile&, const Node&) { return std::make_unique<ConstTrigger>(); }
std::unique_ptr<TriggerOp> stub_true(Interp&, const ScriptFile&, const Node&) {
    auto t = std::make_unique<ConstTrigger>();
    t->v = true;
    return t;
}

// ---- set_autonomy / puppet: subject relations ----

struct MakeSubject : EffectOp {
    Operand target;
    bool release = false;
    void run(Ctx& c) const override {
        const ScopeRef self = c.self();
        const ScopeRef t = c.in.eval(c, target, loc).as_scope();
        world::Country* overlord = self.is_country() ? c.w.country(self.id) : nullptr;
        world::Country* subject = t.is_country() ? c.w.country(t.id) : nullptr;
        if (!overlord || !subject) return;
        if (subject->overlord >= 0) {
            if (world::Country* old = c.w.country(subject->overlord)) {
                old->subjects.erase(std::remove(old->subjects.begin(), old->subjects.end(), subject->index), old->subjects.end());
            }
            subject->overlord = -1;
        }
        if (release) return;
        subject->overlord = overlord->index;
        overlord->subjects.push_back(subject->index);
    }
};

std::unique_ptr<EffectOp> make_set_autonomy(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<MakeSubject>();
    const Node* t = n.is_block ? bi::find_child(f, n, "target") : &n;
    if (!t || !in.compile_operand(f, *t, e->target)) return nullptr;
    // Fully free autonomy levels end the subject relation.
    if (const Node* lvl = n.is_block ? bi::find_child(f, n, "autonomous_state") : nullptr) {
        e->release = sym_view(lvl->value) == "autonomy_free";
    }
    return e;
}

std::unique_ptr<EffectOp> make_puppet(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<MakeSubject>();
    const Node* t = n.is_block ? bi::find_child(f, n, "target") : &n;
    if (!t || !in.compile_operand(f, *t, e->target)) return nullptr;
    return e;
}

std::unique_ptr<EffectOp> make_end_puppet(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<MakeSubject>();
    e->release = true;
    if (!in.compile_operand(f, n, e->target)) return nullptr;
    return e;
}

// ---- cores (state scope: `add_core_of = TAG`) ----

struct CoreEffect : EffectOp {
    Operand target;
    bool remove = false;
    void run(Ctx& c) const override {
        const ScopeRef self = c.self();
        world::State* st = self.is_state() ? c.w.state(self.id) : nullptr;
        const ScopeRef t = c.in.eval(c, target, loc).as_scope();
        if (!st || !t.is_country()) return;
        auto it = std::find(st->cores.begin(), st->cores.end(), t.id);
        if (remove && it != st->cores.end()) st->cores.erase(it);
        if (!remove && it == st->cores.end()) st->cores.push_back(t.id);
    }
};

template <bool Remove>
std::unique_ptr<EffectOp> make_core(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<CoreEffect>();
    e->remove = Remove;
    if (!in.compile_operand(f, n, e->target)) return nullptr;
    return e;
}

// ---- set_capital / set_stability / set_war_support ----

struct SetCapital : EffectOp {
    Operand state;
    void run(Ctx& c) const override {
        world::Country* co = c.self().is_country() ? c.w.country(c.self().id) : nullptr;
        const ScopeRef s = c.in.eval(c, state, loc).as_scope();
        if (co && s.is_state()) co->capital = s.id;
    }
};

std::unique_ptr<EffectOp> make_set_capital(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<SetCapital>();
    const Node* s = n.is_block ? bi::find_child(f, n, "state") : &n;
    if (!s || !in.compile_operand(f, *s, e->state)) return nullptr;
    return e;
}

struct SetStat : EffectOp {
    bool war_support = false;
    Operand value;
    void run(Ctx& c) const override {
        world::Country* co = c.self().is_country() ? c.w.country(c.self().id) : nullptr;
        if (!co) return;
        Fixed v = c.in.eval(c, value, loc).num;
        if (v < Fixed{}) v = Fixed{};
        if (v > Fixed::from_int(1)) v = Fixed::from_int(1);
        (war_support ? co->war_support : co->stability) = v;
        if (!war_support && c.in.hooks.modifiers_dirty) c.in.hooks.modifiers_dirty(c.self());
    }
};

template <bool WarSupport>
std::unique_ptr<EffectOp> make_set_stat(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<SetStat>();
    e->war_support = WarSupport;
    if (!in.compile_operand(f, n, e->value)) return nullptr;
    return e;
}

// add_timed_idea / add_timed_idea-like: treat as a permanent idea (approximation).
std::unique_ptr<EffectOp> make_add_timed_idea(Interp& in, const ScriptFile& f, const Node& n) {
    const Node* idea = n.is_block ? bi::find_child(f, n, "idea") : &n;
    if (!idea) return nullptr;
    Node fake = *idea;
    fake.key = intern("add_ideas");
    fake.is_block = false;
    auto it = in.registry().effects.find(intern("add_ideas"));
    return it->second.make(in, f, fake);
}

} // namespace

void register_effects_stubs(Registry& r) {
    r.effect("set_autonomy", make_set_autonomy);
    r.effect("puppet", make_puppet);
    r.effect("end_puppet", make_end_puppet);
    r.effect("release_puppet", make_end_puppet);
    r.effect("add_core_of", make_core<false>);
    r.effect("remove_core_of", make_core<true>);
    r.effect("set_capital", make_set_capital);
    r.effect("set_stability", make_set_stat<false>);
    r.effect("set_war_support", make_set_stat<true>);
    r.effect("add_timed_idea", make_add_timed_idea, BuiltinStatus::Approx);

    for (const char* name : {
             "recruit_character", "retire_character", "add_equipment_to_stockpile", "set_politics", "set_popularities",
             "add_popularity", "set_convoys", "unlock_national_focus", "complete_national_focus", "add_opinion_modifier",
             "reverse_add_opinion_modifier", "remove_opinion_modifier", "create_equipment_variant", "set_cosmetic_tag",
             "drop_cosmetic_tag", "set_naval_oob", "set_oob", "load_oob", "create_country_leader", "set_party_name",
             "add_equipment_production", "give_military_access", "activate_targeted_decision", "activate_decision",
             "set_truce", "diplomatic_relation", "set_sub_doctrine", "set_fuel_ratio", "starting_train_buffer",
             "add_named_threat", "add_manpower", "set_rule", "add_to_faction", "create_faction", "create_wargoal",
             "declare_war_on", "white_peace", "add_claim_by", "remove_claim_by", "set_province_controller",
             "add_country_leader_trait", "remove_country_leader_trait", "add_country_leader_role", "set_country_leader_ideology",
             "promote_character", "add_command_power", "add_offsite_building", "add_doctrine_cost_reduction",
             "add_tech_bonus", "add_ai_strategy", "mark_focus_tree_layout_dirty", "set_occupation_law",
             "add_resistance", "add_compliance", "damage_building", "set_state_name", "reset_state_name",
             "add_victory_points", "set_victory_points", "unlock_decision_tooltip", "show_ideas_tooltip",
             "add_relation_modifier", "set_border_war_data", "add_extra_state_shared_building_slots",
             "set_party_rule", "set_character_name", "set_country_flag_tooltip", "add_war_support_tooltip",
             "custom_alert", "play_song", "set_key", "set_temp_cosmetic_tag", "add_research_bonus",
             "modify_building_resources", "add_to_tech_sharing_group", "remove_from_tech_sharing_group",
             "add_province_modifier", "remove_province_modifier", "add_state_modifier", "remove_state_modifier",
             "set_grand_doctrine", "every_army_leader", "random_army_leader", "every_navy_leader",
             "every_unit_leader", "every_character", "random_character", "add_mastery", "set_doctrine"}) {
        r.effect(name, stub_effect, BuiltinStatus::Stub);
    }
    r.trigger("has_dlc", stub_true, BuiltinStatus::Stub);
    for (const char* name : {"has_doctrine", "has_mastery", "has_ideology", "has_government", "has_completed_focus", "is_in_tech_sharing_group", "has_opinion_modifier",
                             "has_opinion", "is_historical_focus_on", "has_game_rule", "is_debug", "difficulty"}) {
        r.trigger(name, stub_false, BuiltinStatus::Stub);
    }
}

} // namespace pdx
