// Engine ("game") variables readable from script: modifier@x, resource@x, building levels, unit counts,
// population, country stats, `random`, ... Anything the simulator does not model reads the scope's
// `engine` map (scenario-settable, default 0) so unmodelled values are explicit rather than silently wrong.

#include "builtins/common.h"

namespace pdx {

namespace {

using sim::DiagKind;

// The @target of a game variable as a NAME (modifier name, resource, unit type, building).
Sym target_name(Ctx& c, const VarRef& ref, SrcLoc l) {
    const Target& t = ref.target;
    switch (t.kind) {
    case TargetKind::Bare:
    case TargetKind::Token: return t.sym;
    case TargetKind::Var: {
        const Value v = c.in.read(c, *t.var, l);
        if (v.kind != Kind::Token) {
            c.in.diag(DiagKind::TokenExpected, l, "'" + ref.text + "': @var: target does not hold a token");
            return kNoSym;
        }
        return v.token_sym();
    }
    default:
        c.in.diag(DiagKind::BadParameter, l, "'" + ref.text + "' needs an @<name> target");
        return kNoSym;
    }
}

const world::Country* country_of(Ctx& c, ScopeRef s) {
    const ScopeRef cs = bi::country_of(c, s);
    return cs.is_country() ? c.w.country(cs.id) : nullptr;
}

Fixed engine_value(Ctx& c, ScopeRef s, Sym name) {
    if (s.is_country()) {
        if (const world::Country* co = c.w.country(s.id)) {
            auto it = co->engine.find(name);
            if (it != co->engine.end()) return it->second;
        }
    } else if (s.is_state()) {
        if (const world::State* st = c.w.state(s.id)) {
            auto it = st->engine.find(name);
            if (it != st->engine.end()) return it->second;
        }
    }
    return Fixed{};
}

// ---- modifiers ----

Value gv_modifier(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym m = target_name(c, ref, l);
    if (m == kNoSym) return {};
    if (c.in.hooks.modifier_defined && !c.in.hooks.modifier_defined(m)) {
        c.in.diag(DiagKind::UndefinedModifier, l, "modifier@" + sym_str(m) + " is not defined anywhere (always 0)");
    }
    return c.in.hooks.modifier_value ? Value::number(c.in.hooks.modifier_value(s, m)) : Value{};
}

// ---- native resources ----

Fixed state_resource(const world::State& st, Sym r) {
    auto it = st.resources.find(r);
    return it == st.resources.end() ? Fixed{} : it->second;
}

Fixed country_produced(Ctx& c, const world::Country& co, Sym r) {
    OpStatus os;
    Fixed sum;
    for (int32_t id : co.controlled_states) {
        if (const world::State* st = c.w.state(id)) sum = Fixed::add(sum, state_resource(*st, r), c.in.semantics(), os);
    }
    return sum;
}

// HOI4 native resource ledger (approximation; see docs/SEMANTICS.md):
//   country_resource_cost_<r>  modifier: positive = flat consumption, NEGATIVE = flat gain. The eco sets it to
//                              -(own production + import share) via the `resource_trade` dynamic modifier, i.e.
//                              it hands its production to the vanilla resource pool.
//   resource_consumed@r        production-line consumption (not modelled: scenario engine value
//                              `resource_consumed_<r>`, default 0) + positive flat costs.
//   resource@r                 surplus = produced - cost - production-line consumption.
Fixed country_cost(Ctx& c, const world::Country& co, Sym r) {
    if (!c.in.hooks.modifier_value) return Fixed{};
    return c.in.hooks.modifier_value(ScopeRef::country(co.index), intern("country_resource_cost_" + sym_str(r)));
}

Fixed line_consumption(const world::Country& co, Sym r) {
    auto it = co.engine.find(intern("resource_consumed_" + sym_str(r)));
    return it == co.engine.end() ? Fixed{} : it->second;
}

Fixed country_consumed(Ctx& c, const world::Country& co, Sym r) {
    const Fixed cost = country_cost(c, co, r);
    OpStatus os;
    return Fixed::add(line_consumption(co, r), cost.raw() > 0 ? cost : Fixed{}, c.in.semantics(), os);
}

Value gv_resource(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym r = target_name(c, ref, l);
    if (s.is_state()) {
        const world::State* st = c.w.state(s.id);
        return st ? Value::number(state_resource(*st, r)) : Value{};
    }
    const world::Country* co = country_of(c, s);
    if (!co) return {};
    OpStatus os;
    Fixed v = Fixed::sub(country_produced(c, *co, r), country_cost(c, *co, r), c.in.semantics(), os);
    v = Fixed::sub(v, line_consumption(*co, r), c.in.semantics(), os);
    return Value::number(v);
}

Value gv_resource_produced(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym r = target_name(c, ref, l);
    if (s.is_state()) {
        const world::State* st = c.w.state(s.id);
        return st ? Value::number(state_resource(*st, r)) : Value{};
    }
    const world::Country* co = country_of(c, s);
    return co ? Value::number(country_produced(c, *co, r)) : Value{};
}

Value gv_resource_consumed(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym r = target_name(c, ref, l);
    const world::Country* co = country_of(c, s);
    return co ? Value::number(country_consumed(c, *co, r)) : Value{};
}

Value gv_resource_zero(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    (void)target_name(c, ref, l);
    (void)s;
    return {}; // exports/imports through the vanilla trade screen are not modelled
}

// ---- buildings / units ----

Value gv_building_level(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym b = target_name(c, ref, l);
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    if (!st) return {};
    auto it = st->buildings.find(b);
    return Value::integer(it == st->buildings.end() ? 0 : it->second);
}

Value gv_damaged_building_level(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym b = target_name(c, ref, l);
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    if (!st) return {};
    auto it = st->damaged_buildings.find(b);
    return Value::integer(it == st->damaged_buildings.end() ? 0 : it->second);
}

template <const char* Building>
Value gv_named_building(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    if (!st) return {};
    auto it = st->buildings.find(intern(Building));
    return Value::integer(it == st->buildings.end() ? 0 : it->second);
}

template <const char* Building>
Value gv_country_building_count(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::Country* co = country_of(c, s);
    if (!co) return {};
    const Sym b = intern(Building);
    int64_t n = 0;
    for (int32_t id : co->controlled_states) {
        if (const world::State* st = c.w.state(id)) {
            auto it = st->buildings.find(b);
            if (it != st->buildings.end()) n += it->second;
        }
    }
    return Value::integer(n);
}

enum class UnitKind { Battalion, Ship, Plane };

template <UnitKind K>
Value gv_units_with_type(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc l) {
    const Sym u = target_name(c, ref, l);
    const world::Country* co = country_of(c, s);
    if (!co) return {};
    const auto& m = K == UnitKind::Battalion ? co->battalions : K == UnitKind::Ship ? co->ships : co->planes;
    auto it = m.find(u);
    return Value::integer(it == m.end() ? 0 : it->second);
}

template <UnitKind K>
Value gv_units_total(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::Country* co = country_of(c, s);
    if (!co) return {};
    const auto& m = K == UnitKind::Battalion ? co->battalions : K == UnitKind::Ship ? co->ships : co->planes;
    int64_t n = 0;
    for (const auto& [k, v] : m) n += v;
    return Value::integer(n);
}

// ---- population / ids / stats ----

Value gv_state_population_k(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    if (!st) return {};
    OpStatus os;
    return Value::number(Fixed::div(st->manpower, Fixed::from_int(1000), c.in.semantics(), os));
}

Value gv_state_population(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    return st ? Value::number(st->manpower) : Value{};
}

Value gv_capital(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::Country* co = country_of(c, s);
    return co && co->capital > 0 ? Value::state(co->capital) : Value{};
}

Value gv_id(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    (void)c;
    if (s.is_state()) return Value::integer(s.id);
    if (s.is_country()) return Value::country(s.id);
    return {};
}

Value gv_random(Ctx& c, ScopeRef, const VarRef&, SrcLoc) { return Value::number(c.w.rng.unit()); }
Value gv_year(Ctx& c, ScopeRef, const VarRef&, SrcLoc) { return Value::integer(c.w.date.year); }
Value gv_num_days(Ctx& c, ScopeRef, const VarRef&, SrcLoc) { return Value::integer(c.w.date.ordinal() + 365); }

// `controller` / `owner`: built-in links in state scope. In country scope HOI4 has none; the eco stores its
// own `controller = THIS` variable there, so read that (falling back to the country itself).
template <bool Controller>
Value gv_controller(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc) {
    if (s.is_state()) {
        const world::State* st = c.w.state(s.id);
        const int32_t ci = st ? (Controller ? st->controller : st->owner) : -1;
        return ci >= 0 ? Value::country(ci) : Value{};
    }
    if (world::VarStore* vs = c.w.vars(s)) {
        if (const Value* v = vs->find(world::make_key(ref.name))) return *v;
    }
    return s.is_country() ? Value::country(s.id) : Value{};
}

Value gv_stability(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return co ? Value::number(co->stability) : Value{}; }
Value gv_war_support(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return co ? Value::number(co->war_support) : Value{}; }
Value gv_political_power(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return co ? Value::number(co->political_power) : Value{}; }

Value gv_num_owned_states(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return Value::integer(co ? static_cast<int64_t>(co->owned_states.size()) : 0); }
Value gv_num_controlled_states(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return Value::integer(co ? static_cast<int64_t>(co->controlled_states.size()) : 0); }
Value gv_num_core_states(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) { const auto* co = country_of(c, s); return Value::integer(co ? static_cast<int64_t>(co->core_states.size()) : 0); }

Value gv_resistance(Ctx& c, ScopeRef s, const VarRef&, SrcLoc) {
    const world::State* st = s.is_state() ? c.w.state(s.id) : nullptr;
    return st ? Value::number(st->resistance) : Value{};
}

// Everything else: engine map (scenario-settable), default 0.
Value gv_engine(Ctx& c, ScopeRef s, const VarRef& ref, SrcLoc) { return Value::number(engine_value(c, s, ref.name)); }

constexpr char kInfrastructure[] = "infrastructure";
constexpr char kIndustrialComplex[] = "industrial_complex";
constexpr char kArmsFactory[] = "arms_factory";
constexpr char kDockyard[] = "dockyard";

} // namespace

void register_game_vars(Registry& r) {
    r.game_var("modifier", gv_modifier, true);
    r.game_var("resource", gv_resource, true, BuiltinStatus::Approx);
    r.game_var("resource_produced", gv_resource_produced, true);
    r.game_var("resource_consumed", gv_resource_consumed, true, BuiltinStatus::Approx);
    r.game_var("resource_exported", gv_resource_zero, true, BuiltinStatus::Approx);
    r.game_var("resource_imported", gv_resource_zero, true, BuiltinStatus::Approx);
    r.game_var("building_level", gv_building_level, true);
    r.game_var("damaged_building_level", gv_damaged_building_level, true);
    r.game_var("infrastructure_level", gv_named_building<kInfrastructure>);
    r.game_var("industrial_complex_level", gv_named_building<kIndustrialComplex>);
    r.game_var("arms_factory_level", gv_named_building<kArmsFactory>);
    r.game_var("num_of_civilian_factories", gv_country_building_count<kIndustrialComplex>);
    r.game_var("num_of_military_factories", gv_country_building_count<kArmsFactory>);
    r.game_var("num_of_naval_factories", gv_country_building_count<kDockyard>);
    r.game_var("num_battalions_with_type", gv_units_with_type<UnitKind::Battalion>, true);
    r.game_var("num_ships_with_type", gv_units_with_type<UnitKind::Ship>, true);
    r.game_var("num_deployed_planes_with_type", gv_units_with_type<UnitKind::Plane>, true);
    r.game_var("num_battalions", gv_units_total<UnitKind::Battalion>);
    r.game_var("num_ships", gv_units_total<UnitKind::Ship>);
    r.game_var("state_population_k", gv_state_population_k);
    r.game_var("state_population", gv_state_population);
    r.game_var("capital", gv_capital);
    r.game_var("controller", gv_controller<true>);
    r.game_var("owner", gv_controller<false>);
    r.game_var("id", gv_id);
    r.game_var("random", gv_random);
    r.game_var("year", gv_year);
    r.game_var("num_days", gv_num_days, false, BuiltinStatus::Approx);
    r.game_var("stability", gv_stability);
    r.game_var("has_war_support", gv_war_support);
    r.game_var("war_support", gv_war_support);
    r.game_var("political_power", gv_political_power);
    r.game_var("num_owned_states", gv_num_owned_states);
    r.game_var("num_controlled_states", gv_num_controlled_states);
    r.game_var("num_owned_controlled_states", gv_num_controlled_states, false, BuiltinStatus::Approx);
    r.game_var("num_core_states", gv_num_core_states);
    r.game_var("resistance", gv_resistance);
    for (const char* name : {"manpower_k", "max_manpower_k", "max_available_manpower_k", "manpower",
                             "deployed_army_manpower_k", "deployed_navy_manpower_k", "deployed_airforce_manpower_k",
                             "garrison_manpower_need", "amount_manpower_in_deployment_queue", "num_divisions",
                             "land_doctrine_level", "num_of_factories", "num_of_available_civilian_factories",
                             "num_of_available_military_factories", "num_of_available_naval_factories"}) {
        r.game_var(name, gv_engine, false, BuiltinStatus::Approx);
    }
}

const Registry& builtin_registry() {
    static const Registry reg = [] {
        Registry r;
        register_effects_vars(r);
        register_effects_arrays(r);
        register_effects_flow(r);
        register_effects_scopes(r);
        register_effects_world(r);
        register_triggers_core(r);
        register_triggers_world(r);
        register_game_vars(r);
        register_effects_stubs(r);
        return r;
    }();
    return reg;
}

} // namespace pdx
