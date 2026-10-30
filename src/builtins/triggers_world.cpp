// World-state triggers: identity, ownership, ideas/techs/modifiers, relations.

#include "builtins/common.h"

#include <algorithm>

namespace pdx {

namespace {

using sim::DiagKind;

// A trigger/effect argument naming a scope: TAG, THIS/ROOT/PREV/FROM, var:x, or a bare temp holding a scope.
struct ScopeArg {
    bool is_tag = false;
    Sym tag = kNoSym;
    Operand op;
};

bool compile_scope_arg(Interp& in, const ScriptFile& f, const Node& n, ScopeArg& out) {
    const std::string_view v = sym_view(n.value);
    if (looks_like_tag(v)) {
        out.is_tag = true;
        out.tag = n.value;
        return true;
    }
    return in.compile_operand(f, n, out.op);
}

ScopeRef eval_scope_arg(Ctx& c, const ScopeArg& a, SrcLoc loc) {
    if (a.is_tag) {
        const int32_t i = c.w.country_index(a.tag);
        return i >= 0 ? ScopeRef::country(i) : ScopeRef::none();
    }
    return c.in.eval(c, a.op, loc).as_scope();
}

const world::Country* self_country(Ctx& c) {
    const ScopeRef s = c.self();
    return s.is_country() ? c.w.country(s.id) : nullptr;
}
const world::State* self_state(Ctx& c) {
    const ScopeRef s = c.self();
    return s.is_state() ? c.w.state(s.id) : nullptr;
}

// ---- yes/no triggers on the current scope ----

using BoolFn = bool (*)(Ctx&);

struct BoolTrigger : TriggerOp {
    BoolFn fn = nullptr;
    bool want = true;
    bool eval(Ctx& c) const override { return fn(c) == want; }
};

template <BoolFn F>
std::unique_ptr<TriggerOp> make_bool(Interp&, const ScriptFile&, const Node& n) {
    auto t = std::make_unique<BoolTrigger>();
    t->fn = F;
    t->want = util::is_yes(n.value);
    return t;
}

bool fn_is_ai(Ctx& c) { const auto* co = self_country(c); return co && co->is_ai; }
bool fn_exists(Ctx& c) { const auto* co = self_country(c); return co && co->exists; }
bool fn_has_war(Ctx& c) { const auto* co = self_country(c); return co && !co->at_war_with.empty(); }
bool fn_is_subject(Ctx& c) { const auto* co = self_country(c); return co && co->overlord >= 0; }
bool fn_is_coastal(Ctx& c) { const auto* s = self_state(c); return s && s->coastal; }
bool fn_is_capital(Ctx& c) {
    const auto* s = self_state(c);
    if (!s) return false;
    const auto* o = c.w.country(s->owner);
    return o && o->capital == s->id;
}
bool fn_has_damaged_buildings(Ctx& c) {
    const auto* s = self_state(c);
    if (!s) return false;
    for (const auto& [k, v] : s->damaged_buildings) {
        if (v > 0) return true;
    }
    return false;
}
bool fn_has_resistance(Ctx& c) { const auto* s = self_state(c); return s && s->resistance.raw() > 0; }

// ---- triggers taking a scope argument ----

using RelFn = bool (*)(Ctx&, ScopeRef target);

struct RelTrigger : TriggerOp {
    RelFn fn = nullptr;
    ScopeArg arg;
    bool eval(Ctx& c) const override {
        const ScopeRef t = eval_scope_arg(c, arg, loc);
        return t.valid() && fn(c, t);
    }
};

template <RelFn F>
std::unique_ptr<TriggerOp> make_rel(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<RelTrigger>();
    t->fn = F;
    if (!compile_scope_arg(in, f, n, t->arg)) return nullptr;
    return t;
}

bool rel_tag(Ctx& c, ScopeRef t) { return c.self() == t; }
bool rel_original_tag(Ctx& c, ScopeRef t) {
    const auto* co = self_country(c);
    const auto* other = t.is_country() ? c.w.country(t.id) : nullptr;
    return co && other && co->original_tag == other->tag;
}
bool rel_country_exists(Ctx& c, ScopeRef t) {
    const auto* co = t.is_country() ? c.w.country(t.id) : nullptr;
    return co && co->exists;
}
bool rel_is_core_of(Ctx& c, ScopeRef t) {
    const auto* s = self_state(c);
    if (!s || !t.is_country()) return false;
    for (int32_t core : s->cores) {
        if (core == t.id) return true;
    }
    return false;
}
bool rel_is_owned_by(Ctx& c, ScopeRef t) { const auto* s = self_state(c); return s && t.is_country() && s->owner == t.id; }
bool rel_is_controlled_by(Ctx& c, ScopeRef t) { const auto* s = self_state(c); return s && t.is_country() && s->controller == t.id; }
bool rel_has_war_with(Ctx& c, ScopeRef t) { const auto* co = self_country(c); return co && t.is_country() && co->at_war_with.count(t.id); }
bool rel_is_subject_of(Ctx& c, ScopeRef t) { const auto* co = self_country(c); return co && t.is_country() && co->overlord == t.id; }
bool rel_is_in_faction_with(Ctx& c, ScopeRef t) {
    const auto* co = self_country(c);
    const auto* other = t.is_country() ? c.w.country(t.id) : nullptr;
    return co && other && co->faction >= 0 && co->faction == other->faction;
}
bool rel_is_embargoed_by(Ctx& c, ScopeRef t) {
    const auto* co = self_country(c);
    const auto* other = t.is_country() ? c.w.country(t.id) : nullptr;
    return co && other && other->embargoing.count(co->index);
}
bool rel_is_embargoing(Ctx& c, ScopeRef t) { const auto* co = self_country(c); return co && t.is_country() && co->embargoing.count(t.id); }
bool rel_is_guaranteed_by(Ctx& c, ScopeRef t) { const auto* co = self_country(c); return co && t.is_country() && co->guaranteed_by.count(t.id); }

// ---- name-argument triggers ----

struct NameTrigger : TriggerOp {
    enum class Kind : uint8_t { Idea, Tech, DynamicModifier, Continent, StateId } kind = Kind::Idea;
    Sym name = kNoSym;
    bool from_var = false;
    VarRef var;
    int32_t state_id = 0;
    bool eval(Ctx& c) const override {
        Sym n = name;
        if (from_var) {
            const Value v = c.in.read(c, var, loc);
            n = v.kind == pdx::Kind::Token ? v.token_sym() : kNoSym;
        }
        switch (kind) {
        case Kind::Idea: {
            const auto* co = self_country(c);
            return co && std::find(co->ideas.begin(), co->ideas.end(), n) != co->ideas.end();
        }
        case Kind::Tech: {
            const auto* co = self_country(c);
            return co && co->techs.count(n);
        }
        case Kind::DynamicModifier: {
            const auto* dm = c.w.dynamic_modifiers(c.self());
            if (!dm) return false;
            for (const auto& inst : *dm) {
                if (inst.name == n) return true;
            }
            return false;
        }
        case Kind::Continent: {
            const auto* s = self_state(c);
            return s && s->continent == n;
        }
        case Kind::StateId: {
            const ScopeRef s = c.self();
            return s.is_state() && s.id == state_id;
        }
        }
        return false;
    }
};

std::unique_ptr<TriggerOp> make_name_trigger(Interp& in, const ScriptFile& f, const Node& n, NameTrigger::Kind k) {
    auto t = std::make_unique<NameTrigger>();
    t->kind = k;
    const Node* src = &n;
    if (n.is_block) {
        src = bi::find_child(f, n, "modifier");
        if (!src) return nullptr;
    }
    const std::string_view v = sym_view(src->value);
    if (k == NameTrigger::Kind::StateId) {
        Fixed id;
        Fixed::parse(v, id);
        t->state_id = static_cast<int32_t>(id.whole_trunc());
        return t;
    }
    if (v.size() > 4 && v.substr(0, 4) == "var:") {
        t->from_var = true;
        if (!in.compile_ref(v.substr(4), in.loc(f, n), t->var)) return nullptr;
    } else {
        t->name = src->value;
    }
    return t;
}

std::unique_ptr<TriggerOp> make_has_idea(Interp& in, const ScriptFile& f, const Node& n) { return make_name_trigger(in, f, n, NameTrigger::Kind::Idea); }
std::unique_ptr<TriggerOp> make_has_tech(Interp& in, const ScriptFile& f, const Node& n) { return make_name_trigger(in, f, n, NameTrigger::Kind::Tech); }
std::unique_ptr<TriggerOp> make_has_dynmod(Interp& in, const ScriptFile& f, const Node& n) { return make_name_trigger(in, f, n, NameTrigger::Kind::DynamicModifier); }
std::unique_ptr<TriggerOp> make_is_on_continent(Interp& in, const ScriptFile& f, const Node& n) { return make_name_trigger(in, f, n, NameTrigger::Kind::Continent); }
std::unique_ptr<TriggerOp> make_state(Interp& in, const ScriptFile& f, const Node& n) { return make_name_trigger(in, f, n, NameTrigger::Kind::StateId); }

// ---- numeric comparison triggers on engine stats ----

struct StatTrigger : TriggerOp {
    enum class Stat : uint8_t { Stability, WarSupport, PoliticalPower, EnemiesStrengthRatio } stat = Stat::Stability;
    Op op = Op::Gt;
    Fixed value;
    bool eval(Ctx& c) const override {
        const auto* co = self_country(c);
        Fixed x;
        switch (stat) {
        case Stat::Stability: x = co ? co->stability : Fixed{}; break;
        case Stat::WarSupport: x = co ? co->war_support : Fixed{}; break;
        case Stat::PoliticalPower: x = co ? co->political_power : Fixed{}; break;
        case Stat::EnemiesStrengthRatio: x = Fixed{}; break; // no combat model: 0 (knob in SEMANTICS.md)
        }
        switch (op) {
        case Op::Lt: return x < value;
        case Op::Le: return x <= value;
        case Op::Ge: return x >= value;
        case Op::Eq: return x >= value; // HOI4 `has_stability = 0.5` means at least
        case Op::Gt:
        default: return x > value;
        }
    }
};

template <StatTrigger::Stat S>
std::unique_ptr<TriggerOp> make_stat(Interp& in, const ScriptFile& f, const Node& n) {
    auto t = std::make_unique<StatTrigger>();
    t->stat = S;
    t->op = n.op;
    if (!Fixed::parse(sym_view(n.value), t->value)) {
        in.diag(DiagKind::BadParameter, in.loc(f, n), sym_str(n.key) + " needs a number");
        return nullptr;
    }
    return t;
}

} // namespace

void register_triggers_world(Registry& r) {
    r.trigger("is_ai", make_bool<fn_is_ai>);
    r.trigger("exists", make_bool<fn_exists>);
    r.trigger("has_war", make_bool<fn_has_war>);
    r.trigger("is_subject", make_bool<fn_is_subject>);
    r.trigger("is_puppet", make_bool<fn_is_subject>, BuiltinStatus::Approx);
    r.trigger("is_coastal", make_bool<fn_is_coastal>);
    r.trigger("is_capital", make_bool<fn_is_capital>);
    r.trigger("has_damaged_buildings", make_bool<fn_has_damaged_buildings>);
    r.trigger("has_resistance", make_bool<fn_has_resistance>);

    r.trigger("tag", make_rel<rel_tag>);
    r.trigger("original_tag", make_rel<rel_original_tag>);
    r.trigger("country_exists", make_rel<rel_country_exists>);
    r.trigger("is_core_of", make_rel<rel_is_core_of>);
    r.trigger("is_owned_by", make_rel<rel_is_owned_by>);
    r.trigger("is_controlled_by", make_rel<rel_is_controlled_by>);
    r.trigger("is_fully_controlled_by", make_rel<rel_is_controlled_by>, BuiltinStatus::Approx);
    r.trigger("has_war_with", make_rel<rel_has_war_with>);
    r.trigger("is_subject_of", make_rel<rel_is_subject_of>);
    r.trigger("is_in_faction_with", make_rel<rel_is_in_faction_with>);
    r.trigger("is_embargoed_by", make_rel<rel_is_embargoed_by>);
    r.trigger("is_embargoing", make_rel<rel_is_embargoing>);
    r.trigger("is_guaranteed_by", make_rel<rel_is_guaranteed_by>);

    r.trigger("has_idea", make_has_idea);
    r.trigger("has_tech", make_has_tech);
    r.trigger("has_dynamic_modifier", make_has_dynmod);
    r.trigger("is_on_continent", make_is_on_continent);
    r.trigger("state", make_state);

    r.trigger("has_stability", make_stat<StatTrigger::Stat::Stability>);
    r.trigger("has_war_support", make_stat<StatTrigger::Stat::WarSupport>);
    r.trigger("has_political_power", make_stat<StatTrigger::Stat::PoliticalPower>);
    r.trigger("enemies_strength_ratio", make_stat<StatTrigger::Stat::EnemiesStrengthRatio>, BuiltinStatus::Approx);
}

} // namespace pdx
