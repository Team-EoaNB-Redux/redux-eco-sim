#!/usr/bin/env python
"""Writes scenarios/save_inputs_1857.txt: everything the economy reads that the sim doesn't simulate, taken from real saves.

  python tools/import_save_inputs.py --start ENG_1857_05_11_01.hoi4 --save autosave.hoi4

--start is a save made at the bookmark, --save one made after the first monthly tick (production lines have started and
military_cost is filled in). Runs the sim itself, twice: stability can only be read once ideas and techs are in place.

From --save:
  units / ships                 num_battalions_with_type, num_ships_with_type: military_cost / military_cost_per
  deployed_army_manpower_k      military payroll, backed out through the formula in _economy_budget.txt
From --start:
  resource_consumed_<good>      production lines: (local_need - military_demand - the country's state demand)
                                / (1 + military_production_cost_factor), since national_market_update applies that factor.
                                Only valid where control matches the sim's map, which is why it isn't taken from --save:
                                by June, Qing held three states the map gives to others, and read as 2,900 food of consumption
  ideas, techs                  what the sim's history import misses: the engine's default = yes laws, startup content
  pins                          has_afr_empire_mechanic_system, set by an on_startup the sim doesn't run. is_tribal_tag
                                reads it, and tribal tags are left out of global.markets
  stability                     the save's format for it is unknown, so it's backed out: with everything else in place, the
                                gap in industry_throughput_factor is the static stability modifier (+0.05 at 100%, -0.10 at
                                0%). ENG keeps its value from world_1857_eng.txt, which came from consumer confidence
"""
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path

from read_save import SAVES, load, read_ideas, read_techs

SIM = Path(__file__).resolve().parent.parent
SCENARIO = SIM / "scenarios" / "world_1857_eng.txt"
THROUGHPUT_PROBE = "industry_throughput_factor@textile_mill"
MIN_CONSUMED = 0.5  # below this it's rounding, or a state that ticked after its market leader
PINNED_FLAGS = ["has_afr_empire_mechanic_system"]


def country_modifiers():
    """{tag: (bureaucratic efficiency, payroll cost, production cost)}, as the sim computes them."""
    script = ('every_country = { log = "MOD|[THIS.GetTag]|[?modifier@bureaucratic_efficiency_factor|5]'
              '|[?modifier@military_payroll_cost_factor|5]|[?modifier@military_production_cost_factor|5]" }')
    out = subprocess.run([str(SIM / "bin" / "ecosim_cli.exe"), "effect", "--scenario", str(SCENARIO),
                          "--scope", "global", script], cwd=SIM, capture_output=True, text=True).stdout
    rows = re.findall(r"MOD\|(\w+)\|(-?[\d.]+)\|(-?[\d.]+)\|(-?[\d.]+)", out)
    return {tag: (float(a), float(b), float(c)) for tag, a, b, c in rows}


def sim_state():
    """The sim at startup under the current inputs file."""
    with tempfile.TemporaryDirectory() as folder:
        dump = Path(folder) / "sim.json"
        subprocess.run([str(SIM / "bin" / "ecosim_cli.exe"), "run", "--scenario", str(SCENARIO), "--months", "0", "--dump-json", str(dump)],
                       cwd=SIM, capture_output=True, text=True)
        return json.loads(dump.read_text("utf-8"))


def fmt(value):
    return f"{value:.5f}".rstrip("0").rstrip(".")


def difference(game, sim):
    """{name: yes/no} turning the sim's list into the game's. The game writes some idea names in lower case."""
    game_lower = {n.lower() for n in game}
    sim_lower = {n.lower() for n in sim}
    changes = {n: "yes" for n in game if n.lower() not in sim_lower}
    changes.update({n: "no" for n in sim if n.lower() not in game_lower})
    return changes


def block(name, table):
    lines = [f"{name} = {{"]
    for tag in sorted(table):
        lines.append(f"\t{tag} = {{ " + " ".join(f"{k} = {v if isinstance(v, str) else fmt(v)}" for k, v in sorted(table[tag].items())) + " }")
    lines.append("}")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--start", type=Path, required=True)
    ap.add_argument("--save", type=Path, default=SAVES / "autosave.hoi4")
    ap.add_argument("--out", type=Path, default=SIM / "scenarios" / "save_inputs_1857.txt")
    args = ap.parse_args()

    scopes, _ = load(args.save)
    start_scopes, _ = load(args.start)
    args.out.write_text("# being regenerated\n", encoding="utf-8")
    sim = sim_state()
    g = scopes["global"]
    goods = [k.split("@")[1] for k in g if k.startswith("base_price@")]
    unit_types = {sim["global"][f"unit_types^{i}"] for i in range(int(sim["global"]["unit_types^num"]))}
    ship_types = {sim["global"][f"ship_types^{i}"] for i in range(int(sim["global"]["ship_types^num"]))}

    state_demand = {}
    for state, tag in sim["controllers"].items():
        for good in goods:
            value = start_scopes.get(state, {}).get(f"demand@{good}", 0)
            if value:
                state_demand[tag, good] = state_demand.get((tag, good), 0) + value

    def engine_values(modifiers):
        engine = {}
        for tag, c in sorted(scopes.items()):
            if tag == "global" or tag.isdigit() or tag not in sim:
                continue
            efficiency, payroll_cost, production_cost = modifiers.get(tag, (1, 0, 0))
            values = {}
            at_start = start_scopes.get(tag, {})
            for good in goods:
                consumed = at_start.get(f"local_need@{good}", 0) - at_start.get(f"military_demand@{good}", 0) - state_demand.get((tag, good), 0)
                if consumed > MIN_CONSUMED:
                    values[f"resource_consumed_{good}"] = consumed / (1 + production_cost)
            # military_payroll = manpower x earnings / 2 / participation / bureaucratic efficiency x (1 + payroll cost)
            earnings = c.get("pops_earnings_avg", 0)
            if earnings > 0 and c.get("military_payroll_spending", 0) > 0 and tag != "ENG":
                participation = g.get("pops_workforce_participation", 1)
                values["deployed_army_manpower_k"] = (c["military_payroll_spending"] * 2 * participation * efficiency
                                                      / earnings / (1 + payroll_cost))
            if values:
                engine[tag] = values
        return engine

    units, ships = {}, {}
    for tag, c in sorted(scopes.items()):
        if tag == "global" or tag.isdigit() or tag not in sim:
            continue
        for key, cost in c.items():
            m = re.fullmatch(r"military_cost@(\w+)", key)
            per = c.get(f"military_cost_per@{m.group(1)}", 0) if m else 0
            if cost > 0 and per > 0:
                table = ships if m.group(1) in ship_types else units if m.group(1) in unit_types else None
                if table is not None:
                    table.setdefault(tag, {})[m.group(1)] = round(cost / per)

    ideas = {}
    for tag, names in read_ideas(args.start).items():
        if tag in sim["ideas"]:
            changes = difference(names, sim["ideas"][tag])
            if changes:
                ideas[tag] = changes
    techs = {}
    for tag, names in read_techs(args.start).items():
        if tag in sim["techs"]:
            changes = difference(names, sim["techs"][tag])
            if changes:
                techs[tag] = changes

    pins = [f"\t{tag}.{flag} = 1" for tag, c in sorted(start_scopes.items()) if tag in sim and tag != "global" and not tag.isdigit()
            for flag in PINNED_FLAGS if c.get(flag)]

    def write(engine):
        text = [f"# Generated by tools/import_save_inputs.py from {args.start.name} and {args.save.name}: don't edit. That script says what each value is."]
        text += block("units", units) + block("ships", ships) + block("engine", engine) + block("ideas", ideas) + block("techs", techs)
        text += ["pins = {"] + pins + ["}"]
        args.out.write_text("\n".join(text) + "\n", encoding="utf-8")

    write(engine_values({}))
    applied = sim_state()
    engine = engine_values(country_modifiers())
    fitted = 0
    for tag, c in start_scopes.items():
        if tag == "global" or tag.isdigit() or tag == "ENG" or tag not in applied:
            continue
        gap = c.get(THROUGHPUT_PROBE, 0) - applied[tag].get(THROUGHPUT_PROBE, 0)
        if abs(gap) > 0.0005:
            stability = 0.5 + gap * 10 if gap > 0 else 0.5 + gap * 5
            engine.setdefault(tag, {})["stability"] = min(1, max(0, stability))
            fitted += 1
    write(engine)
    print(f"wrote {args.out}: {len(units)} armies, {len(ships)} navies, {len(ideas)} ideas, {len(techs)} techs, {len(pins)} pins, {fitted} stabilities")


if __name__ == "__main__":
    main()
