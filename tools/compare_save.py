#!/usr/bin/env python
"""Diffs every variable in a real save against a sim state dump, grouped by variable.

  bin/ecosim_cli.exe run --scenario scenarios/world_1857_eng.txt --months 1 --dump-json sim.json
  python tools/compare_save.py --save autosave.hoi4 --sim sim.json                       worst groups, all scopes
  python tools/compare_save.py --save autosave.hoi4 --sim sim.json --scope ENG --top 60  one scope
  python tools/compare_save.py --save autosave.hoi4 --sim sim.json --var gdp_amount      one variable, worst scopes
  python tools/compare_save.py --save x.hoi4 --sim y.json --grep "^price" --scope ENG    every matching key, values

A variable missing from either side reads 0: the game drops zeros from saves. Tokens, countries and states compare by
identity. Groups are ranked by summed absolute difference relative to the group's total, so large, broad errors lead.
"""
import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

from read_save import SAVES, load, tag_of


def scope_of_number(value):
    """A game value that holds a scope, as the sim writes it. None if it's a plain number."""
    raw = int(round(value * 100000))
    if 1 << 30 < raw < (1 << 30) + (1 << 24):
        return tag_of(value)
    if -(1 << 30) < raw < -(1 << 30) + 5000:
        return f"state:{raw + (1 << 30)}"
    return None


# Display-only variables: never economy inputs.
NOISE = re.compile(r"graph|pie_chart|_frames|_gfx|_seats|seats_|_bar_array|^(red|green|blue)[A-Za-z]{3}$|^internal_politics|^ideology_|"
                   r"^local_mhc|^market_list|^show_trade|^score_|_balance_(price_|amount_)?array|^resource_list|parl_|^percentage|^idea_token|^trait_level")


def group_of(key):
    return re.sub(r"\^\d+$", "^i", key.split("@")[0])


def normal_game_key(key):
    """@<state id> and @<tag> are appended to the name in-game with no @."""
    return key


def normal_sim_key(key):
    return re.sub(r"@(\d+|[A-Z][A-Z0-9]{2})(?=$|\^)", r"\1", key)


def as_number(value, tokens_back):
    if isinstance(value, str):
        return tokens_back.get(value)
    return value


def compare_scope(game, sim, tokens_back, identity_rows):
    """{key: (game, sim)} for every value that differs by more than rounding."""
    out = {}
    for key in set(game) | set(sim):
        a, b = game.get(key, 0), sim.get(key, 0)
        if isinstance(b, str):
            shown = scope_of_number(a) if isinstance(a, (int, float)) else a
            if shown == b or tokens_back.get(b) == a:
                continue
            identity_rows.append(key)
            out[key] = (shown if shown is not None else a, b)
            continue
        if abs(a - b) > max(0.00011, 1e-6 * max(abs(a), abs(b))):
            out[key] = (a, b)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", type=Path, default=SAVES / "autosave.hoi4")
    ap.add_argument("--sim", type=Path, required=True)
    ap.add_argument("--scope", help="global, a tag, a state id, 'countries' or 'states'. Default: everything")
    ap.add_argument("--var", help="exact group name: list the scopes where it differs most")
    ap.add_argument("--grep", help="regex over keys: list every match with both values")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--json", type=Path, help="also write every difference here")
    args = ap.parse_args()

    scopes, tokens = load(args.save)
    tokens_back = {name: number for number, name in tokens.items()}
    sim_scopes = json.loads(args.sim.read_text("utf-8"))

    wanted = []
    for scope in sim_scopes:
        if scope not in scopes:
            continue
        kind = "global" if scope == "global" else "states" if scope.isdigit() else "countries"
        if args.scope in (None, scope, kind):
            wanted.append(scope)

    extra = sorted(set(scopes) - set(sim_scopes)), sorted(set(sim_scopes) - set(scopes))
    print(f"scopes: {len(wanted)} compared; only in save {extra[0][:12]}; only in sim {extra[1][:12]}\n")

    groups = defaultdict(lambda: [0, 0.0, 0.0, 0.0, []])  # count, sum |diff|, sum |game|, sum |sim|, examples
    all_diffs = {}
    compared = 0
    for scope in wanted:
        game = scopes[scope]
        sim = {normal_sim_key(k): v for k, v in sim_scopes[scope].items()}
        compared += len(set(game) | set(sim))
        diffs = compare_scope(game, sim, tokens_back, [])
        if args.json:
            all_diffs[scope] = {k: list(v) for k, v in diffs.items()}
        for key, (a, b) in diffs.items():
            group = group_of(key)
            if NOISE.search(group):
                continue
            row = groups[group]
            row[0] += 1
            if isinstance(a, (int, float)) and isinstance(b, (int, float)):
                row[1] += abs(a - b)
                row[2] += abs(a)
                row[3] += abs(b)
            row[4].append((scope, key, a, b))

    if args.json:
        args.json.write_text(json.dumps(all_diffs), encoding="utf-8")

    if args.grep:
        pattern = re.compile(args.grep)
        for scope in wanted:
            game = scopes[scope]
            sim = {normal_sim_key(k): v for k, v in sim_scopes[scope].items()}
            for key in sorted(k for k in set(game) | set(sim) if pattern.search(k)):
                print(f"{scope:6} {key:48} game {game.get(key, 0)!s:>16}  sim {sim.get(key, 0)!s:>16}")
        return

    if args.var:
        rows = groups.get(args.var, [0, 0, 0, 0, []])[4]
        rows.sort(key=lambda r: -abs(r[2] - r[3]) if isinstance(r[2], (int, float)) and isinstance(r[3], (int, float)) else 0)
        for scope, key, a, b in rows[: args.top]:
            print(f"{scope:6} {key:48} game {a!s:>16}  sim {b!s:>16}")
        return

    print(f"{compared} values compared, {sum(g[0] for g in groups.values())} differ\n")
    print(f"{'variable':40}{'differ':>8}{'|diff|':>13}{'|game|':>13}{'|sim|':>13}{'rel':>7}  worst example")
    def rank(item):
        g = item[1]
        return -(g[1] / max(g[2], g[3], 1e-5)) * min(1.0, g[0] / 10) - (0.5 if g[1] == 0 else 0)
    for name, g in sorted(groups.items(), key=rank)[: args.top]:
        worst = max(g[4], key=lambda r: abs(r[2] - r[3]) if isinstance(r[2], (int, float)) and isinstance(r[3], (int, float)) else 0)
        print(f"{name:40}{g[0]:8}{g[1]:13.1f}{g[2]:13.1f}{g[3]:13.1f}{g[1] / max(g[2], g[3], 1e-5):7.2f}  {worst[0]}.{worst[1]}: {worst[2]} vs {worst[3]}")


if __name__ == "__main__":
    main()
