#pragma once

// The HOI4 script interpreter: compiles AST blocks into op trees once, then executes them against the World.
//
// Key engine behaviours (see docs/SEMANTICS.md):
//   * temp variables are UNSCOPED: one frame per top-level execution (on_action block, console effect, test)
//   * unset reads are 0; `?default` overrides
//   * loops: `break = <temp>` exits as soon as that temp is non-zero; while/for loops cap at
//     MAX_EFFECT_ITERATION (1000) iterations
//   * triggers may contain temp-variable effects and if/else (evaluated in order, AND short-circuits)
//   * scripted effects/triggers compile lazily on first call

#include "pdx/ast.h"
#include "pdx/semantics.h"
#include "pdx/value.h"
#include "pdx/varref.h"
#include "sim/diagnostics.h"
#include "world/world.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pdx {

class Interp;
struct MathExpr;

// ---------------------------------------------------------------------------------------------
// Execution context
// ---------------------------------------------------------------------------------------------

struct Ctx {
    Interp& in;
    world::World& w;
    std::vector<ScopeRef> scopes; // back() = THIS; PREV = scopes[size-2] ...
    ScopeRef root;
    ScopeRef from;
    world::VarStore* temps = nullptr;
    std::vector<Sym> break_stack; // active loop break temps (innermost last)
    int depth = 0;

    Ctx(Interp& i, world::World& wo) : in(i), w(wo) {}
    ScopeRef self() const { return scopes.empty() ? ScopeRef::none() : scopes.back(); }
    ScopeRef prev(int n = 1) const {
        const int idx = static_cast<int>(scopes.size()) - 1 - n;
        return idx >= 0 ? scopes[static_cast<size_t>(idx)] : ScopeRef::none();
    }
    bool break_requested() const;
};

struct ScopeGuard {
    Ctx& c;
    ScopeGuard(Ctx& ctx, ScopeRef s) : c(ctx) { c.scopes.push_back(s); }
    ~ScopeGuard() { c.scopes.pop_back(); }
};

// ---------------------------------------------------------------------------------------------
// Compiled ops
// ---------------------------------------------------------------------------------------------

struct EffectOp {
    SrcLoc loc;
    virtual ~EffectOp() = default;
    virtual void run(Ctx& c) const = 0;
};

struct TriggerOp {
    SrcLoc loc;
    virtual ~TriggerOp() = default;
    virtual bool eval(Ctx& c) const = 0;
};

using EffectBlock = std::vector<std::unique_ptr<EffectOp>>;
using TriggerBlock = std::vector<std::unique_ptr<TriggerOp>>; // implicit AND

// An r-value in script: literal, variable reference, scope keyword, math expression, or `random`.
struct Operand {
    enum class Type : uint8_t { Literal, Ref, Scope, Expr, Random } type = Type::Literal;
    Value literal;
    VarRef ref;
    Step scope;
    std::shared_ptr<MathExpr> expr;
};

// ---------------------------------------------------------------------------------------------
// Builtin registry
// ---------------------------------------------------------------------------------------------

enum class BuiltinStatus : uint8_t { Full, Approx, Stub };
const char* builtin_status_name(BuiltinStatus s);

using EffectFactory = std::unique_ptr<EffectOp> (*)(Interp&, const ScriptFile&, const Node&);
using TriggerFactory = std::unique_ptr<TriggerOp> (*)(Interp&, const ScriptFile&, const Node&);
// Game variable: evaluated in the resolved scope with the reference's @target (if any).
using GameVarFn = Value (*)(Ctx&, ScopeRef scope, const VarRef& ref, SrcLoc loc);

struct EffectBuiltin {
    EffectFactory make = nullptr;
    BuiltinStatus status = BuiltinStatus::Full;
};
struct TriggerBuiltin {
    TriggerFactory make = nullptr;
    BuiltinStatus status = BuiltinStatus::Full;
};
struct GameVarBuiltin {
    GameVarFn fn = nullptr;
    BuiltinStatus status = BuiltinStatus::Full;
    bool target_is_name = false; // `modifier@x` / `num_battalions_with_type@x`: bare target is a name, not a token
};

struct Registry {
    std::unordered_map<Sym, EffectBuiltin> effects;
    std::unordered_map<Sym, TriggerBuiltin> triggers;
    std::unordered_map<Sym, GameVarBuiltin> game_vars;

    void effect(const char* name, EffectFactory f, BuiltinStatus s = BuiltinStatus::Full) { effects[intern(name)] = {f, s}; }
    void trigger(const char* name, TriggerFactory f, BuiltinStatus s = BuiltinStatus::Full) { triggers[intern(name)] = {f, s}; }
    void game_var(const char* name, GameVarFn f, bool target_is_name = false, BuiltinStatus s = BuiltinStatus::Full) {
        game_vars[intern(name)] = {f, s, target_is_name};
    }
};

// Each builtins/*.cpp file provides one of these; registry.cpp calls them all.
void register_effects_vars(Registry& r);
void register_effects_arrays(Registry& r);
void register_effects_flow(Registry& r);
void register_effects_scopes(Registry& r);
void register_effects_world(Registry& r);
void register_triggers_core(Registry& r);
void register_triggers_world(Registry& r);
void register_game_vars(Registry& r);
void register_effects_stubs(Registry& r);
const Registry& builtin_registry();

// ---------------------------------------------------------------------------------------------
// Hooks the interpreter needs from the rest of the simulator (events, modifiers, tokens...).
// The defaults are inert so the interpreter is testable on its own.
// ---------------------------------------------------------------------------------------------

struct Hooks {
    // modifier@name in a scope
    std::function<Fixed(ScopeRef, Sym)> modifier_value;
    // modifier name is defined somewhere (for the undefined_modifier lint)
    std::function<bool(Sym)> modifier_defined;
    // known token (script_enums, ideas, techs, resources, ...)
    std::function<bool(Sym)> is_token;
    // fire `country_event` / `state_event`
    std::function<void(Ctx&, Sym id, int64_t days, SrcLoc)> fire_event;
    // ideas / techs / dynamic modifiers changed for a scope (modifier cache invalidation)
    std::function<void(ScopeRef)> modifiers_dirty;
    // force_update_dynamic_modifier
    std::function<void(ScopeRef)> refresh_dynamic_modifiers;
    // add_resource (native resource ledger)
    std::function<void(ScopeRef state, Sym resource, Fixed amount)> add_resource;
};

// ---------------------------------------------------------------------------------------------
// Interpreter
// ---------------------------------------------------------------------------------------------

struct ScriptedDef {
    const ScriptFile* file = nullptr;
    const Node* node = nullptr;
    std::unique_ptr<EffectBlock> effects;
    std::unique_ptr<TriggerBlock> triggers;
    uint64_t calls = 0;
};

class Interp {
public:
    Interp(world::World& w, sim::Diagnostics& diags, Semantics sem = {});

    world::World& world() { return w_; }
    sim::Diagnostics& diagnostics() { return diags_; }
    const Semantics& semantics() const { return sem_; }
    Semantics& semantics_mut() { return sem_; }
    const Registry& registry() const { return reg_; }
    Hooks hooks;

    // ---- script library ----
    // Takes ownership; returns a stable pointer.
    const ScriptFile* add_file(ScriptFile&& f);
    const ScriptFile* file(uint32_t file_id) const;
    std::string file_path(uint32_t file_id) const;
    // Registers every top-level `name = { ... }` of the file as a scripted effect / trigger.
    void index_scripted_effects(const ScriptFile& f);
    void index_scripted_triggers(const ScriptFile& f);
    bool has_scripted_effect(Sym name) const { return scripted_effects_.count(name) != 0; }
    bool has_scripted_trigger(Sym name) const { return scripted_triggers_.count(name) != 0; }
    ScriptedDef* scripted_effect(Sym name);
    ScriptedDef* scripted_trigger(Sym name);
    const std::unordered_map<Sym, ScriptedDef>& scripted_effects() const { return scripted_effects_; }
    // Scripted effects named here run as no-ops (scenario `skip_effects`, e.g. GUI-only graph rendering).
    void set_skipped_effects(std::vector<Sym> names);
    bool is_skipped(Sym name) const;

    // ---- compilation ----
    EffectBlock compile_effects(const ScriptFile& f, const Node& block);
    TriggerBlock compile_triggers(const ScriptFile& f, const Node& block);
    std::unique_ptr<EffectOp> compile_effect(const ScriptFile& f, const Node& n, const Node* siblings_end = nullptr,
                                             const Node** consumed_until = nullptr);
    std::unique_ptr<TriggerOp> compile_trigger(const ScriptFile& f, const Node& n, const Node* siblings_end = nullptr,
                                               const Node** consumed_until = nullptr);
    // Compiles the value side of a node: literal, reference, scope keyword or math-expression block.
    bool compile_operand(const ScriptFile& f, const Node& n, Operand& out);
    bool compile_operand_text(std::string_view text, SrcLoc loc, Operand& out);
    // Variable reference from a key/value word; reports a diagnostic and returns false on bad syntax.
    bool compile_ref(std::string_view text, SrcLoc loc, VarRef& out);
    SrcLoc loc(const ScriptFile& f, const Node& n) const { return SrcLoc{f.file_id, n.line}; }

    // ---- execution ----
    // Runs a block as a top-level execution with a fresh temp frame (or the given one).
    void run(const EffectBlock& block, ScopeRef self, ScopeRef root = ScopeRef::none(),
             ScopeRef from = ScopeRef::none(), world::VarStore* temps = nullptr);
    bool test(const TriggerBlock& block, ScopeRef self, ScopeRef root = ScopeRef::none(),
              ScopeRef from = ScopeRef::none(), world::VarStore* temps = nullptr);
    void run_block(Ctx& c, const EffectBlock& block) const;
    bool eval_and(Ctx& c, const TriggerBlock& block) const;

    // ---- values ----
    Value eval(Ctx& c, const Operand& op, SrcLoc loc);
    Value read(Ctx& c, const VarRef& ref, SrcLoc loc);
    bool has(Ctx& c, const VarRef& ref, SrcLoc loc);
    // Resolves where a write goes. `temp` = the effect is a *_temp_variable effect.
    struct Slot {
        world::VarStore* store = nullptr;
        world::VarKey key;
        bool is_temp = false;
        bool is_array_element = false;
        int64_t element = 0;
    };
    bool resolve_slot(Ctx& c, const VarRef& ref, bool temp, SrcLoc loc, Slot& out);
    Value slot_get(const Slot& s) const;
    void slot_set(Ctx& c, const Slot& s, const Value& v, SrcLoc loc);
    // Array addressed by a reference (`arr`, `controller:list@var:x`, `global.arr`). temp = *_temp_array.
    std::vector<Value>* array(Ctx& c, const VarRef& ref, bool temp, bool create, SrcLoc loc);
    // Elements written past ^num (see VarStore::ghost). clear_array also drops them.
    const Value* find_ghost(Ctx& c, const VarRef& ref, int64_t index, SrcLoc loc);
    void clear_ghosts(Ctx& c, const VarRef& ref, bool temp, SrcLoc loc);

    // Scope resolution
    ScopeRef resolve_step(Ctx& c, const Step& s, ScopeRef from_scope, SrcLoc loc);
    bool resolve_path(Ctx& c, const VarRef& ref, ScopeRef& out, SrcLoc loc);
    uint32_t target_code(Ctx& c, const VarRef& ref, SrcLoc loc);

    // Arithmetic with diagnostics attached at `loc`.
    Fixed arith(Ctx& c, char op, Fixed a, Fixed b, SrcLoc loc);
    Value check_number(Ctx& c, const Value& v, SrcLoc loc);

    // ---- diagnostics / output ----
    void diag(sim::DiagKind k, SrcLoc loc, const std::string& msg);
    std::function<void(const std::string& line)> log_sink;
    std::string format_text(Ctx& c, std::string_view text, SrcLoc loc); // [?x|fmt], [THIS.GetName], ...
    std::string value_text(Ctx& c, const Value& v, std::string_view fmt);

    // ---- profiling ----
    struct CallStats {
        uint64_t calls = 0;
        double inclusive_seconds = 0;
    };
    bool profiling = false;
    std::unordered_map<Sym, CallStats> profile;
    // Builtin effect/trigger/game-variable sites compiled so far (coverage + fidelity reporting).
    std::unordered_map<Sym, uint64_t> builtin_uses;

    std::string format_loc(SrcLoc loc) const { return file_path(loc.file_id) + ":" + std::to_string(loc.line); }

private:
    world::World& w_;
    sim::Diagnostics& diags_;
    Semantics sem_;
    const Registry& reg_;
    std::deque<ScriptFile> files_;
    std::unordered_map<Sym, ScriptedDef> scripted_effects_;
    std::unordered_map<Sym, ScriptedDef> scripted_triggers_;
    std::vector<Sym> skipped_;
};

// Shared helpers for builtin implementations.
namespace util {
const Node* child(const ScriptFile& f, const Node& block, const char* key);
bool is_yes(Sym v);
Sym sym_of(const char* text);
} // namespace util

} // namespace pdx
