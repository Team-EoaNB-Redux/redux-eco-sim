# Engine semantics: verified vs. knob

Every engine behaviour the simulator depends on is either **VERIFIED** (HOI4 wiki or in-game) or a
**KNOB** with a default we believe matches HOI4. Knobs live in `src/pdx/semantics.h`; override them per
scenario (`semantics = { div_by_zero = zero }`) or per run (`--sem div_by_zero=zero`). Promote a knob to
VERIFIED with an in-game probe (see Calibration).

## Numbers and variables
| Behaviour | Status | Notes |
|---|---|---|
| Precision 0.00001; finer digits truncated (incl. literals) | VERIFIED | wiki *Data structures* |
| Whole part signed 32-bit; past max wraps to min | VERIFIED | `overflow = wrap32 / saturate / none` (none = diagnostic mode) |
| Unset variable reads 0; `x?default` | VERIFIED | |
| Temps unscoped, cleared at the end of the top-level execution | VERIFIED | memory note + wiki |
| `round_variable`: nearest, halves away from zero | VERIFIED | |
| `modulo_variable`: a negative value comes back unchanged (-7 mod 3 = -7) | VERIFIED (probe, one case) | the wiki's Euclidean claim is wrong in-game |
| Mul/div rounding: truncation | VERIFIED (Strat) | 1/3×3 = 0.99999 |
| Multiply: raw × raw wraps in 64 bits, then divides by the scale | VERIFIED (probe) | a product above ~922 million goes wrong: 100000 × 100000 = 776627963.14522 |
| Division by zero | VERIFIED (probe) | `divide_variable` gives 0; a math expression gives 42949.67295 (5 / 0, one case) |
| Bare name read when a temp AND a scope var exist | KNOB `temp_precedence` = temp_first | every collision is reported (`temp_shadows_var`); none occur in the current eco |
| `x@bare_identifier` is a literal name | VERIFIED (probe) | `size@industrial_complex` works as the token; `x@some_temp` never reads the temp. Unknown names are reported |
| Array elements: arr^i are variables, ^num separate; writes past ^num are kept and readable | VERIFIED (Strat) | `array_ghost_write` lint; they survive `clear_array` (probe) |
| Country in a variable = raw 2^30 + tag bytes little-endian (ENG reads 10784.14917) | VERIFIED (probe) | > 10,500, which the companies financier encoding relies on |
| State in a variable = raw id − 2^30 (state 1 reads -10737.41823, London 952 -10737.40872) | VERIFIED (probe) | `parity` turns such dump keys back into ids |
| Token in a variable = some engine table index (food 19999) | not reproducible | sim uses its symbol id; only matters if script does arithmetic on tokens |

## Script language
| Behaviour | Status | Notes |
|---|---|---|
| `check_variable` `<` / `>` strict | VERIFIED | |
| `check_variable = { x = 5 }` is equality | VERIFIED (Strat) | long form default is `>=` |
| `check_variable` shorthand `<=` `>=` unsupported | VERIFIED (Strat) | evaluates false + lint |
| while/for loops capped at 1000 iterations | VERIFIED | `max_effect_iterations` |
| `break = x` exits as soon as TEMP `x` is non-zero | VERIFIED (wiki) | a `set_variable` of the same name does not break |
| The break temp is reset to 0 when a loop starts | VERIFIED (parity) | a stale `b = 1` from an earlier loop otherwise skips the loop: calculate_standard_of_living gave SoL 1 instead of 30–74 |
| `for_each_loop` defaults `value = v`, `index = i` | VERIFIED | |
| Temp effects + if/else inside triggers | VERIFIED (eco depends on it) | |
| `NOT = { a b }` is NOR | VERIFIED | |
| `*_temp_*` effect on `global.x` / scoped name | modelled: scoped store wins | scoring code relies on it; lint `temp_with_scope` |
| `[?x]` prints as many decimals as needed (2.3, 2.432, 7) | VERIFIED (Strat) | `|N` rounding vs truncation: probe |
| `[X.GetName]` = localised name ("United Kingdom", "London"); `[X.GetTag]` = tag | modelled | localisation/english/*.yml; falls back to the key |
| Math-expression parse failure → whole expression 0 | VERIFIED | also reported as `math_expr_error` |
| A load error zeroes every **later** math expression in the same file | VERIFIED (error.log, 2026-11-12): a `log = ""` ("Not a valid value"), a call to an unknown effect, and `clamp = { max .. min .. }` each did it. An "unexpected token" inside `is_in_array` didn't, nor did math in a trigger block after one | Startup load pass (`Interp::find_load_errors`): compiles every scripted effects and triggers file once, unreported, and finds its first syntax error, unknown effect or trigger, rejected expression or empty log. Names in the game's documentation (`data/game_*.txt`, `tools/gen_game_names.py`) and ideology groups aren't errors. Reported as `load_error`, and later expressions in that file read 0 with a `math_expr_error` |
| `clamp` argument order | VERIFIED (error.log): `min` must come first. Max first is "Expected 'min' as argument 1", a load error | compile error |
| `clamp_variable` / `clamp_temp_variable` with crossed bounds (min above max) | VERIFIED: min wins in either order. Probe `clamp_effect_crossed_min_first` (min written first) gave 0, 2026-11-12. With max written first, Strat's 1857.6.1 autosave shows it too: `pops_handle_savings` clamps `x_temp` to `max = net_money_temp min = 0`, and with net money below 0 the game leaves 0. The old max-wins rule zeroed pops' net money there, so every class lost 2 SoL fewer than in-game in the first month, and ENG's month-1 revenue read 4.4% high | max, then min |
| `arr^temp` in a math expression | VERIFIED from an autosave: reads the right element in ticks. **0 from the console** (probe), cause unconfirmed | reported as `math_array_read`. Knob `math_array_read_is_zero` (default no) forces the 0 for what-if runs. Stage into a temp to be safe everywhere |
| Math expression in a trigger block (`all_of`, scripted trigger, `limit`) | VERIFIED from an autosave: works at startup and in ticks, with `arr^i` inside `all_of`. **0 from the console** (probe), cause unconfirmed. GUI and localisation untested | reported as `math_in_trigger`. Knob `math_in_trigger_is_zero` (default no). Plain temp effects work everywhere |
| `add_to_variable = { global.x@var:t = { value = arr^i multiply = y } }` at startup | 0 in-game: `pm_construction_value`, 216 of 216 PMs, while the sim read it correctly. NOT MODELLED, cause unknown | it was the only `add_to_variable` with a math RHS in the economy and is gone. Probes `add_to_*` |
| Math `min`/`max` = smaller/larger of accumulator and argument; `clamp = { min max }` = bounds | VERIFIED (Strat) | clamp with min > max gives 0 (probe, one case) |
| `pow` integer exponent = repeated fixed-point multiply | VERIFIED | fractional pow/root/log/trig use double (approx, as in-game) |

## World / engine model
| Behaviour | Status | Notes |
|---|---|---|
| Dynamic-modifier variable values re-read daily and on `force_update_dynamic_modifier` | VERIFIED | one-day lag reproduced |
| `country_is_at_peace` / `_at_war` static modifiers applied by the engine | modelled | supply base confidence/incentives values |
| `stability_good_modifier` scales 0..1 over stability 50..100%, `stability_bad_modifier` over 50..0% | VERIFIED (parity) | consumer confidence matches in-game; gives throughput, capacity and confidence |
| Stability value | scenario `engine = { TAG = { stability = x } }`, then moved by `stability_factor` on ideas gained or lost after startup | not simulated otherwise; default 0.5 (both modifiers off). ENG 0.7165 is backed out of in-game confidence, every other country out of its throughput gap (`tools/import_save_inputs.py`). Bankruptcy is -0.65, and it's what drives the post-bankruptcy demand spike. `has_stability` and `stability` still read the stored value |
| Default laws | from a save | the engine gives each country the `default = yes` idea of any slot it has empty (`steady_growth`, `no_press`, ... 300+ countries). Not derived here: scenario key `ideas`, generated from a save |
| Difficulty | scenario `difficulty = easy` | the engine adds static `diff_<x>_player` to humans and `diff_<x>_ai` to the rest. VERIFIED: it's in the save header. Easy is -0.02 input factor and +1% of GDP a month |
| `modifier@x` = ideas + techs + static + dynamic (country); category + dynamic (state) | modelled | tech modifiers are the numeric keys directly in the tech body (`tax_efficiency_factor = 0.02`) plus any `modifier = {}` block |
| `resource@r`, `resource_produced@r`, `resource_consumed@r` | APPROX | produced = Σ controlled states' resources; `country_resource_cost_<r>` < 0 is a gain; production-line use = scenario engine value `resource_consumed_<r>` (default 0) |
| History: `controller = X` with no owner → X owns the state, without a core | VERIFIED (Strat) | e.g. 391 Minnesota (USA) before 1870 |
| `controller` / `owner` | modelled | state: built-in link; country: the eco's own `controller` variable, else self |
| No tick at start (on_startup only); 1857 bookmark starts 1857.5.11 | VERIFIED (Strat) | monthly on day 1, weekly every 7 days from start: KNOB |
| A weekly tick doesn't follow the monthly on the same day | VERIFIED from the 1857.6.1 autosave | `global.market_supply` there is still the pre-tick value. KNOB `monthly_before_weekly`, default no. 5.11 + 21 days is June 1, so the old default re-priced the world on month one and flipped ENG's trade profit from +4.8K to -11.9K. Whether the game runs weekly first or on another day isn't known |
| Event option choice | modelled | `ai_chance` weights via seeded RNG; `player_event_choice = first_option` |
| Manpower pools, divisions, doctrine levels, factories in use | not modelled | read the scenario `engine = { TAG = { name = value } }` block (default 0) |
| Battalions, ships | scenario keys `units`, `ships` | `num_battalions_with_type`, `num_ships_with_type`: exact against a save, military_consumption matches to the digit. Fixed at the save's values |
| Production lines | scenario `engine`, `resource_consumed_<good>` | read from a startup save, where control matches. Fixed at those values |
| `declare_war_on` | both sides are at war, no allies pulled in | changes `country_is_at_war` and the trade filters: warlord prices were 2x off without it. Fighting and control changes are NOT modelled |
| State control | fixed at start | identical to the game at startup (0 of 332 countries differ). By 1857.6.1 the game's wars had moved 8 countries' totals |
| Research, fighting, diplomacy, focus trees | not modelled | |
| Rest of the world in a subset scenario | scenario `pins` | `eng_1857` pins ENG's in-game startup trade/offers and the world-sim global market prices; a pinned variable keeps its value whatever script writes. Startup parity only: prices drift over long runs, use `world_1857` there |

## Calibration
`toi/common/scripted_effects/_economy_sim_probes.txt` (debug only, never called automatically):
- `effect eco_sim_probes = yes` in-game → `bin\ecosim_cli.exe calibrate` (30 probes, table with meanings).
- `effect eco_sim_parity_dump = yes` in-game → `bin\ecosim_cli.exe parity` (~1240 values: ENG country +
  market, London/West Midlands industries/pops/resources, every ENG state), sorted by largest difference.
  A second dump after the first monthly tick: `parity --dump 2 --months 1`. Use the country you played as
  `players = { TAG }` in the scenario: otherwise `ai_budget` runs for it and taxes, tariffs and trade diverge.
Both read Documents/Paradox Interactive/Hearts of Iron IV/logs/game.log by default.

The probes run from the console, which is not the same as a tick: both probes that read a temp in a math expression
gave 0 there, while the same forms work in real ticks. To run them in a tick: `effect set_global_flag = eosim_tick_probes`,
let a month pass, then `calibrate --dump 2`. **Before trusting a console probe about script, check it against a save.**

**Comparing with a save:** `run --dump-json` writes the whole sim state, and `tools/compare_save.py` diffs it against a save,
grouped by variable. Use `scenarios/world_1857_eng.txt`. A startup save shows setup gaps with no feedback mixed in, then one
tick shows the rest. `tools/import_save_inputs.py` regenerates `scenarios/save_inputs_1857.txt` from such saves: rerun it if
history changes. It needs a save at the bookmark and one after the first monthly tick.

`python tools/read_save.py` reads real in-game variables straight from a binary save, autosaves included, with no dump
effect needed (`--country`, `--state`, `--var`, `--json`). This is the strongest evidence available: it's what the game
actually computed. On 2026-10-30 it showed production matching the sim to about 10% after 4 months, disproving the
theory that array reads had zeroed production.
