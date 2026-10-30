// Scope iterators: every_* / random_* effects and any_* / all_* triggers over countries and states.
// Candidates come from one table, in deterministic order (country load order / state id). PREV inside
// the block is the scope outside it.

#include "builtins/common.h"

namespace pdx {

namespace {

using sim::DiagKind;

enum class Src : uint8_t {
    Country, OtherCountry, State, OwnedState, ControlledState, OwnedControlledState, CoreState, SubjectCountry,
    NeighborCountry
};

std::vector<ScopeRef> candidates(Ctx& c, Src src) {
    std::vector<ScopeRef> out;
    world::World& w = c.w;
    const ScopeRef self = c.self();
    const ScopeRef owner = bi::country_of(c, self);
    const world::Country* co = owner.is_country() ? w.country(owner.id) : nullptr;
    switch (src) {
    case Src::Country:
    case Src::OtherCountry:
    case Src::NeighborCountry:
        for (int32_t i : w.existing_countries()) {
            if (src != Src::Country && owner.is_country() && i == owner.id) continue;
            out.push_back(ScopeRef::country(i));
        }
        break;
    case Src::State:
        for (int32_t id : w.existing_states()) out.push_back(ScopeRef::state(id));
        break;
    case Src::OwnedState:
        if (co) for (int32_t id : co->owned_states) out.push_back(ScopeRef::state(id));
        break;
    case Src::ControlledState:
        if (co) for (int32_t id : co->controlled_states) out.push_back(ScopeRef::state(id));
        break;
    case Src::OwnedControlledState:
        if (co) {
            for (int32_t id : co->controlled_states) {
                const world::State* st = w.state(id);
                if (st && st->owner == co->index) out.push_back(ScopeRef::state(id));
            }
        }
        break;
    case Src::CoreState:
        if (co) for (int32_t id : co->core_states) out.push_back(ScopeRef::state(id));
        break;
    case Src::SubjectCountry:
        if (co) {
            for (int32_t s : co->subjects) {
                if (w.country(s) && w.country(s)->exists) out.push_back(ScopeRef::country(s));
            }
        }
        break;
    }
    return out;
}

struct EveryEffect : EffectOp {
    Src src = Src::Country;
    bool random = false;
    TriggerBlock limit;
    EffectBlock body;
    void run(Ctx& c) const override {
        std::vector<ScopeRef> list = candidates(c, src);
        std::vector<ScopeRef> matching;
        matching.reserve(list.size());
        for (ScopeRef s : list) {
            if (!limit.empty()) {
                ScopeGuard g(c, s);
                if (!c.in.eval_and(c, limit)) continue;
            }
            matching.push_back(s);
        }
        if (random) {
            if (matching.empty()) return;
            const ScopeRef pick = matching[static_cast<size_t>(c.w.rng.below(matching.size()))];
            ScopeGuard g(c, pick);
            c.in.run_block(c, body);
            return;
        }
        for (ScopeRef s : matching) {
            ScopeGuard g(c, s);
            c.in.run_block(c, body);
        }
    }
};

struct AnyAllTrigger : TriggerOp {
    Src src = Src::Country;
    bool any = true;
    TriggerBlock limit; // all_*: only candidates passing `limit` must satisfy the body
    TriggerBlock body;
    bool eval(Ctx& c) const override {
        for (ScopeRef s : candidates(c, src)) {
            ScopeGuard g(c, s);
            if (!limit.empty() && !c.in.eval_and(c, limit)) continue;
            const bool r = c.in.eval_and(c, body);
            if (any && r) return true;
            if (!any && !r) return false;
        }
        return !any;
    }
};

template <Src S, bool Random>
std::unique_ptr<EffectOp> make_every(Interp& in, const ScriptFile& f, const Node& n) {
    auto e = std::make_unique<EveryEffect>();
    e->src = S;
    e->random = Random;
    if (const Node* l = bi::find_child(f, n, "limit")) e->limit = in.compile_triggers(f, *l);
    e->body = bi::compile_effects_except(in, f, n, {"limit", "tooltip", "random_select_amount", "display_individual_scopes", "include_invisible"});
    return e;
}

template <Src S, bool Any>
std::unique_ptr<TriggerOp> make_any(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<AnyAllTrigger>();
    t->src = S;
    t->any = Any;
    if (const Node* l = bi::find_child(f, n, "limit")) t->limit = in.compile_triggers(f, *l);
    t->body = bi::compile_triggers_except(in, f, n, {"limit", "tooltip", "count", "percent"});
    return t;
}

} // namespace

void register_effects_scopes(Registry& r) {
    r.effect("every_country", make_every<Src::Country, false>);
    r.effect("every_other_country", make_every<Src::OtherCountry, false>);
    r.effect("every_state", make_every<Src::State, false>);
    r.effect("every_owned_state", make_every<Src::OwnedState, false>);
    r.effect("every_controlled_state", make_every<Src::ControlledState, false>);
    r.effect("every_core_state", make_every<Src::CoreState, false>);
    r.effect("every_subject_country", make_every<Src::SubjectCountry, false>);
    r.effect("every_neighbor_country", make_every<Src::NeighborCountry, false>, BuiltinStatus::Approx);
    r.effect("random_country", make_every<Src::Country, true>);
    r.effect("random_other_country", make_every<Src::OtherCountry, true>);
    r.effect("random_state", make_every<Src::State, true>);
    r.effect("random_owned_state", make_every<Src::OwnedState, true>);
    r.effect("random_controlled_state", make_every<Src::ControlledState, true>);
    r.effect("random_owned_controlled_state", make_every<Src::OwnedControlledState, true>);
    r.effect("random_core_state", make_every<Src::CoreState, true>);
    r.effect("random_subject_country", make_every<Src::SubjectCountry, true>);
    r.effect("random_neighbor_country", make_every<Src::NeighborCountry, true>, BuiltinStatus::Approx);

    r.trigger("any_country", make_any<Src::Country, true>);
    r.trigger("any_other_country", make_any<Src::OtherCountry, true>);
    r.trigger("any_state", make_any<Src::State, true>);
    r.trigger("any_owned_state", make_any<Src::OwnedState, true>);
    r.trigger("any_controlled_state", make_any<Src::ControlledState, true>);
    r.trigger("any_core_state", make_any<Src::CoreState, true>);
    r.trigger("any_subject_country", make_any<Src::SubjectCountry, true>);
    r.trigger("any_neighbor_country", make_any<Src::NeighborCountry, true>, BuiltinStatus::Approx);
    r.trigger("all_country", make_any<Src::Country, false>);
    r.trigger("all_other_country", make_any<Src::OtherCountry, false>);
    r.trigger("all_state", make_any<Src::State, false>);
    r.trigger("all_owned_state", make_any<Src::OwnedState, false>);
    r.trigger("all_controlled_state", make_any<Src::ControlledState, false>);
    r.trigger("all_core_state", make_any<Src::CoreState, false>);
    r.trigger("all_subject_countries", make_any<Src::SubjectCountry, false>);
    r.trigger("all_neighbor_country", make_any<Src::NeighborCountry, false>, BuiltinStatus::Approx);
}

} // namespace pdx
