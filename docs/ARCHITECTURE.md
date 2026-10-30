# Architecture

```
 toi/ (the mod)                ecosim_core (pure C++17, no godot-cpp)
 ─────────────                 ──────────────────────────────────────────────────────────────
 common/scripted_effects  ──►  loader/mod_data   tokens, tags, ideas, techs, modifiers, script index
 common/... definitions   ──►  loader/history    states + countries (natives) + history effects
 history/, map/           ──►  loader/scenario   tag subset, whitelists, overrides, semantics
                               pdx/              parser → AST → compiled op trees → execution
                               builtins/         effects / triggers / game variables (registry)
                               world/            World, VarStore, calendar, RNG
                               sim/simulation    load → startup (on_startup) → day loop
                                                 (daily modifier refresh, events, on_weekly/monthly_<TAG>)
                               sim/report        recorder, report.md, compare
        ┌──────────────────────────────┬──────────────────────────────┐
   cli/ecosim_cli.exe            tests/ecosim_tests.exe        godot/EcoSim (GDExtension)
   (AI tool, headless)                                          └─► project/ (GDScript UI)
```

## Execution model
- Every script file the mod loads is parsed once into a flat AST arena (`pdx/ast.h`); strings are interned.
- Blocks compile lazily into trees of ops with pre-resolved builtin handlers and variable references
  (`pdx/varref.h`: scope path + name + `@target` + `^index` + `?default`). Scripted effects/triggers
  compile on first call.
- Variables live per scope in hash maps keyed by `(name symbol, @target code)`; `pops@var:t` is an integer
  lookup at runtime. Temps are one unscoped store per top-level execution.
- The interpreter never silently ignores anything: unknown keys, bad parameters and engine edge cases
  become structured diagnostics (`sim/diagnostics.h`) with file:line and hit counts.

## Tick schedule
`Simulation::startup()` runs the whitelisted `on_startup` blocks in file order (`_eoanbr_constants_on_startup`
then `_eoanbr_eco` → `_reset`). `step_day()`: date+1 → dynamic-modifier refresh → delayed events →
`on_monthly_<TAG>` on the 1st (UCT → `_advance_time`) → `on_weekly_<TAG>` every 7th day (`r_weekly_trade`).

## UI
`project/app/sim_service.gd` (autoload `Sim`) owns the EcoSim object and the `{country}`/`{state}` context.
`project/app/main.gd` builds the shell in code and adds one tab per panel discovered by
`ui/core/panel_registry.gd`. Panels declare content through `ui/core/panel_builder.gd`; the map panel uses
`ui/map/state_map.gdshader` (ID texture + palette row, Rota Fortunae technique) and `ui/map_modes/*.gd`.
Simulation runs on a worker thread; the UI polls `poll_finished()` and refreshes.
