# Redux Eco Sim — guide for AI sessions

A side-car simulator for the EoaNB-R economy. It **parses and executes the mod's real HOI4 scripts**
(`../toi/common/scripted_effects/_economy_*.txt` and everything they call) on a mocked HOI4 world.
Edit eco script in `toi/`, re-run the sim, read the report. No reimplementation of the eco exists here:
if the sim disagrees with the game, the bug is in the interpreter or the world model, not in a copy of the eco.

## Layout
```
src/pdx/        interpreter core: fixed.h (HOI4 5-decimal fixed point), parser, varref grammar,
                interp (compile + execute), mathexpr, textfmt ([?x|fmt]), semantics.h (unverified knobs)
src/builtins/   HOI4 builtins, one table row each (effects_*.cpp, triggers_*.cpp, game_vars.cpp, stubs.cpp)
src/world/      World (countries, states, flags, dynamic modifiers), VarStore, calendar, RNG
src/loader/     mod definitions (tokens, ideas, techs, modifiers), history import, scenario files
src/sim/        Simulation facade, modifier engine, events, diagnostics, recorder/report, state map
src/cli/        ecosim_cli (headless — the main tool)
src/godot/      EcoSim GDExtension bridge (the ONLY class GDScript sees)
project/        Godot 4.5 UI (GDScript). app/ shell, ui/core/ framework, ui/panels/*, ui/map_modes/*
tests/unit/     C++ unit tests        tests/script/  HOI4-script golden tests (# expect: lines)
scenarios/      scenario configs      watches/       watch lists for recorded runs
runs/           CLI outputs (gitignored)   docs/     SEMANTICS.md, ARCHITECTURE.md
```

## Build (Windows, MSVC via SCons)
```
python -m SCons cli=yes tests=yes -j8     # bin/ecosim_cli.exe + bin/ecosim_tests.exe (seconds; no Godot)
python -m SCons -j8                       # GDExtension -> project/bin/windows/*.dll (godot-cpp is prebuilt)
bin\ecosim_tests.exe                      # C++ unit tests + all tests/script/*.txt
bin\ecosim_cli.exe test --verbose         # just the script golden tests, one line each
```
Godot 4.5.1: `C:\Users\white\Documents\Godot\Godot_v4.5.1-stable_win64_console.exe`
- UI smoke test: `<godot> --headless --path project -- --smoke` (must print `SMOKE OK`)
- Close the Godot editor before rebuilding the DLL (it locks the file).

## Workflow after editing eco script
1. `bin\ecosim_cli.exe run --months 24 --out runs\<name>` then **read `runs\<name>\report.md` first**:
   watch table (start/end/min/max), diagnostics split into *economy files* vs *elsewhere*, scripted-effect
   profile, builtins compiled at reduced fidelity. `series.csv` has every month; `diagnostics.json` everything.
2. A/B: run a baseline before the edit, then `bin\ecosim_cli.exe compare runs\baseline runs\<name>`.
   Quick parameter sweeps need no edit: `--set global.wage_change_cap=0.02` (repeatable).
3. Debug like the in-game console:
   `bin\ecosim_cli.exe effect --after-months 1 --scope ENG "log = \"[?treasury|0] [?market_leader.GetTag]\""`
   (scopes: tag, state id, `global`; `log` supports `[?ref|N]`, `|%`, `.GetTokenKey`, `[THIS.GetName]`).
   Monthly series without a recorded run: `run --months 6 --watch ENG.gdp_amount --watch 952.base_wage`.
4. Whole world instead of the ENG subset: `--scenario scenarios\world_1857.txt` (~4.5 s/month). The subset fakes
   the rest of the world with scenario `pins` (ENG's startup trade held fixed), so its startup matches the game
   but trade-driven prices drift over long runs; use the world scenario for multi-year balance questions.
   `scenarios\british_isles.txt` is a fantasy closed economy: its 19 states are all owned by ENG, with no trade
   partners and 0.05 s/month. It uses the scenario keys `states = { ... }` and `state_owner = ENG`; UCT's own
   states always load, because UCT hosts the ticks. As of 2026-10-30 it collapses within a year: food starts
   at its price cap without imports.
5. Game parity (debug effects in `toi/common/scripted_effects/_economy_sim_probes.txt`): after Strat runs
   `effect eco_sim_parity_dump = yes` / `effect eco_sim_probes = yes` in-game, `bin\ecosim_cli.exe parity`
   and `bin\ecosim_cli.exe calibrate` diff game.log against the sim. Fix the largest early divergence first.
6. `check --parse-only` parses all ~5100 mod files (<1 s) and lists real brace/syntax errors.

Refs (everywhere: CLI, UI, watch files) are HOI4 variable references with a scope prefix:
`ENG.treasury`, `952.base_wage`, `global.base_price@token:steel`, `ENG.pops@token:laborers`,
`ENG.market_leader:price@token:food`, `ENG.modifier@incentives_modifier`, `ENG.industries^num`.

## Reading diagnostics (what the game hides)
error: `unknown_effect` / `unknown_trigger` (evaluates false) / `math_expr_error` (in-game the whole
expression silently becomes 0) / `meta_effect_error` / `parse_error`.
warning: `unknown_scripted_effect` (called but defined nowhere), `overflow` (left ±2^31, wrapped),
`div_by_zero`, `loop_cap` (1000 iterations), `invalid_scope`, `array_index` (read/write past the end —
HOI4 elements must exist before being written), `token_expected`, `event_not_loaded`.
lint: `scope_unset` (a scope var holds 0 → block skipped), `temp_with_scope` (`*_temp_*` effect on a
`global.`/scoped name — the scoped store is used), `temp_shadows_var`, `undefined_modifier` (always 0),
`ambiguous_at_target` (`x@bare_name`: token or variable?). info: `out_of_subset`.
Counts are hits; sites are deduplicated by file:line. Non-eco files (politics, demographics on_startup)
produce noise — focus on the *economy files* section.

## Extending
**New HOI4 builtin** (effect/trigger/game variable): write a factory in the matching `src/builtins/*.cpp`
(see `common.h` helpers), add one `r.effect(...)` / `r.trigger(...)` / `r.game_var(...)` row in that file's
`register_*` function, mark it `BuiltinStatus::Approx`/`Stub` if not faithful, and add a golden test in
`tests/script/`. Unknown keys are reported, never silently ignored.

**New UI panel**: create `project/ui/panels/<id>/panel.gd` — nothing else to register:
```gdscript
extends EcoPanel
static func info() -> Dictionary:
	return {"id": "banking", "title": "Banking", "order": 45}
func build(b: PanelBuilder) -> void:
	b.section("Bank")
	b.stats([["Reserves", "{country}.bank_reserves"], ["Rate", "{country}.bank_interest_rate"]])
	b.table("Pop loans", "global.pops_array", [["Lent", "{state}.bank_lent_to_pops_k@token:{row}"]])
	b.chart("Reserves", ["{country}.bank_reserves"])
	b.effect_row("Rate", [["-1", "set_temp_variable = { ir_change_temp = -1 } change_interest_rate = yes"]])
```
Placeholders: `{country}`, `{state}` (sidebar selection), `{row}` (table row element). Delete the folder to
remove a feature. Custom drawing: override `refresh()`. UI is built in code, not .tscn.
**New map mode**: `project/ui/map_modes/<id>.gd` with `static func info()` and
`static func colors(ids) -> PackedColorArray` (heat maps: `return MapModes.heat_of(ids, "{row}.gdp_amount")`).
**New bridge method**: add to `src/godot/eco_sim.{h,cpp}` + `_bind_methods`, then rebuild the DLL.

## Rules
- Fidelity first: if engine behaviour is unknown, add a knob to `src/pdx/semantics.h`, document it in
  `docs/SEMANTICS.md`, and keep the current default. Don't "fix" mod bugs inside the simulator.
- Determinism: integer fixed point only in the sim, seeded RNG, stable iteration order. Same seed ⇒ same output.
- Never edit files under `toi/` from here without Strat's OK (e.g. the planned calibration probe file).
- Shell gotcha: heredocs in the Bash tool collapse `\\` and turn `"\n"` escapes into real newlines inside
  C++ strings. Edit C++ with the Edit/Write tools, or put helper scripts in files.
