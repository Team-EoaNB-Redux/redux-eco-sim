#!/usr/bin/env python
"""Reads scripted variables straight out of a binary HOI4 save: real in-game state, with no dump effect to run.

  python tools/read_save.py                          ENG budget + market, from the newest autosave
  python tools/read_save.py --country FRA            another country
  python tools/read_save.py --state 952              one state's industries
  python tools/read_save.py --var ENG.gdp_amount --var 952.production_max@iron^0 --var global.base_price@coal
  python tools/read_save.py --save "x.hoi4" --json all.json   everything, keyed by scope

Binary saves store a variable as  0f 00 <u16 length> <name> 01 00 0d 00 <i64 raw, 5 decimals>. Names hold the token
as a number (production_max20001^0), mapped back here through the global token arrays. Each scope's variables are a
sorted run, states first and in id order (state N is run N-1). A country is named by its own `controller` variable.
Temps aren't saved, and a variable holding 0 is left out, so a missing name reads 0. Needs bin/ecosim_cli.exe for the
token names. To diff a save against the sim, use tools/compare_save.py.
"""
import argparse
import bisect
import json
import re
import struct
import subprocess
from pathlib import Path

SIM = Path(__file__).resolve().parent.parent
SAVES = SIM.parent.parent.parent / "save games"
VARIABLE = re.compile(rb"\x0f\x00(.{2})([A-Za-z_][A-Za-z0-9_@.^:]{1,80})\x01\x00\x0d\x00(.{8})", re.S)
IDEAS_BLOCK = 12271       # binary key of `ideas = { names }`, after the country's variables
TECHNOLOGY_BLOCK = 11869  # binary key of `technology = { name = { ... } }`, before them
TOKEN_ARRAYS = ["resource_token_array", "industry_token_array", "pm_token_array", "unit_types", "ship_types"]


def read_runs(path):
    """Returns every run of variables as [{name: value}]."""
    data = path.read_bytes()
    runs, cur, last_pos, last_name = [], {}, None, None
    for m in VARIABLE.finditer(data):
        name = m.group(2)
        if struct.unpack("<H", m.group(1))[0] != len(name):
            continue
        name = name.decode()
        if last_pos is not None and ((name < last_name and m.start() - last_pos > 60) or m.start() - last_pos > 2000):
            runs.append(cur)
            cur = {}
        cur[name] = struct.unpack("<q", m.group(3))[0] / 100000
        last_pos, last_name = m.start(), name
    runs.append(cur)
    return runs


def token_names(global_vars):
    """{token number: name}. Numbers follow eoanbr_tokens.txt with repeats removed, skipping tokens the game already
    knew, so each token array's order (read from the simulator) anchors the walk through that file."""
    names = {}
    for array in TOKEN_ARRAYS:
        script = f'for_each_loop = {{ array = global.{array} value = zz log = "TOK|[?zz.GetTokenKey]" }}'
        out = subprocess.run([str(SIM / "bin" / "ecosim_cli.exe"), "effect", "--scope", "ENG", script], cwd=SIM,
                             capture_output=True, text=True).stdout
        for i, name in enumerate(re.findall(r"TOK\|(\w+)", out)):
            number = global_vars.get(f"{array}^{i}")
            if number is not None:
                names[int(number)] = name
    known = {name: number for number, name in names.items()}
    number = None
    text = (SIM.parent / "toi" / "common" / "synchronized_dynamic_tokens" / "eoanbr_tokens.txt").read_text("utf-8-sig", "replace")
    for name in dict.fromkeys(re.sub(r"#.*", "", text).split()):
        if name in known:
            number = known[name] if known[name] >= 19000 else number
        elif number is not None:
            number += 1
            names.setdefault(number, name)
    return names


def tag_of(value):
    raw = int(round(value * 100000)) - (1 << 30)
    return raw.to_bytes(4, "little", signed=True).rstrip(b"\0").decode("ascii", "replace")


def load(path):
    runs = read_runs(path)
    global_vars = next(r for r in runs if "resource_token_array^0" in r)
    tokens = token_names(global_vars)

    def readable(name):
        def named(m):
            token = tokens.get(int(m.group(2)))
            return f"{m.group(1)}@{token}" if token else m.group(0)
        return re.sub(r"(\D)(\d{3,5})(?=$|\^)", named, name)

    scopes = {"global": {readable(k): v for k, v in global_vars.items()}}
    state = 0
    for run in runs:
        if run is global_vars:
            continue
        if run.get("controller") and "arable_land" not in run:
            scopes[tag_of(run["controller"])] = {readable(k): v for k, v in run.items()}
        elif "arable_land" in run:
            state += 1
            scopes[str(state)] = {readable(k): v for k, v in run.items()}
    return scopes, tokens


def country_marks(data):
    """[(position of the country's `controller` variable, tag)], sorted."""
    pattern = re.compile(rb"\x0f\x00\x0a\x00controller\x01\x00\x0d\x00(.{8})", re.S)
    return sorted((m.start(), tag_of(struct.unpack("<q", m.group(1))[0] / 100000)) for m in pattern.finditer(data))


def read_string(data, p):
    """(text, position after it), or (None, p) if p doesn't hold a string."""
    if struct.unpack("<H", data[p:p + 2])[0] not in (0x0f, 0x17):
        return None, p
    length = struct.unpack("<H", data[p + 2:p + 4])[0]
    return data[p + 4:p + 4 + length].decode("latin1"), p + 4 + length


def skip_block(data, p):
    """The position after the } closing a block whose contents start at p."""
    depth = 1
    while depth:
        t = struct.unpack("<H", data[p:p + 2])[0]
        p += 2
        if t == 3:
            depth += 1
        elif t == 4:
            depth -= 1
        elif t in (0x0f, 0x17):
            p += 2 + struct.unpack("<H", data[p:p + 2])[0]
        elif t in (0x0c, 0x14):
            p += 4
        elif t in (0x0d, 0x167):
            p += 8
        elif t == 0x0e:
            p += 1
    return p


def read_ideas(path):
    """{tag: [idea names]}, from each country's ideas block."""
    data = path.read_bytes()
    marks = country_marks(data)
    positions = [p for p, _ in marks]
    head = struct.pack("<H", IDEAS_BLOCK) + b"\x01\x00\x03\x00"
    out = {}
    i = data.find(head)
    while i >= 0:
        p, names = i + 6, []
        while True:
            name, p = read_string(data, p)
            if name is None:
                break
            names.append(name)
        k = bisect.bisect_right(positions, i) - 1
        if names and k >= 0 and data[p:p + 2] == b"\x04\x00":
            out.setdefault(marks[k][1], names)
        i = data.find(head, i + 6)
    return out


def read_techs(path):
    """{tag: [researched tech names]}. A tech at level 0 is one in the block that isn't researched yet."""
    data = path.read_bytes()
    marks = country_marks(data)
    positions = [p for p, _ in marks]
    head = struct.pack("<H", TECHNOLOGY_BLOCK) + b"\x01\x00\x03\x00"
    out = {}
    i = data.find(head)
    while i >= 0:
        p, names = i + 6, []
        while True:
            name, q = read_string(data, p)
            if name is None or data[q:q + 4] != b"\x01\x00\x03\x00":
                break
            names.append(name)
            p = skip_block(data, q + 4)
        k = bisect.bisect_right(positions, i)
        if names and k < len(marks):
            out[marks[k][1]] = names  # the closest block wins: tags that don't exist have one too
        i = data.find(head, i + 6)
    return out


def show_country(scopes, tag):
    c, g = scopes[tag], scopes["global"]
    for key in ["gdp_amount", "market_gdp", "treasury", "net_budget", "revenue", "spending"]:
        print(f"{key:14}{c.get(key, 0):14.1f}")
    print(f"\n{'good':18}{'price':>8}{'base':>7}{'x base':>7}{'supply':>10}{'demand':>10}{'production':>11}{'need':>10}{'trade':>9}")
    goods = [k.split("@")[1] for k in g if k.startswith("base_price@")]
    for r in goods:
        p, b = c.get(f"price@{r}", 0), g.get(f"base_price@{r}", 0)
        print(f"{r:18}{p:8.2f}{b:7.2f}{(p / b if b else 0):7.2f}{c.get(f'supply@{r}', 0):10.1f}{c.get(f'demand@{r}', 0):10.1f}"
              f"{c.get(f'production@{r}', 0):11.1f}{c.get(f'need@{r}', 0):10.1f}{c.get(f'trade@{r}', 0):9.1f}")


def show_state(scopes, tokens, state):
    s = scopes[state]
    print(f"pops {s.get('pops', 0):.1f}  gdp {s.get('gdp_amount', 0):.1f}  base wage {s.get('base_wage', 0):.3f}  IE {s.get('infrastructure_efficiency', 0):.3f}")
    print(f"\n{'industry':24}{'size':>7}{'employed':>9}{'emp %':>7}{'prod %':>7} {'method':30}{'max^0':>9}{'output^0':>10}{'revenue':>9}{'expenses':>9}{'profit':>9}")
    for i in range(int(s.get("industries^num", 0))):
        b = tokens.get(int(s[f"industries^{i}"]), "?")
        v = lambda k: s.get(f"{k}@{b}", 0)
        print(f"{b:24}{v('size'):7.2f}{v('employed'):9.2f}{v('employment_percentage'):7.1f}{v('production_percentage'):7.1f} "
              f"{tokens.get(int(v('production_method')), '-'):30}{s.get(f'production_max@{b}^0', 0):9.3f}{s.get(f'production@{b}^0', 0):10.3f}"
              f"{v('revenue'):9.1f}{v('expenses'):9.1f}{v('profit'):9.1f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", type=Path, help="default: newest save, autosave.hoi4 included")
    ap.add_argument("--country")
    ap.add_argument("--state")
    ap.add_argument("--var", action="append", default=[], help="scope.name, e.g. ENG.gdp_amount or 952.size@iron")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    path = args.save or max(SAVES.glob("*.hoi4"), key=lambda p: p.stat().st_mtime)
    print(f"save: {path}\n")
    scopes, tokens = load(path)
    if args.json:
        args.json.write_text(json.dumps(scopes, indent=0), encoding="utf-8")
    for ref in args.var:
        scope, name = ref.split(".", 1)
        print(f"{ref} = {scopes[scope].get(name, '(not in save)')}")
    if args.state:
        show_state(scopes, tokens, args.state)
    if args.country or not (args.state or args.var or args.json):
        show_country(scopes, args.country or "ENG")


if __name__ == "__main__":
    main()
