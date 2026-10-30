# Fantasy history sets

Each folder here is a self-contained history set that replaces the mod's `toi/history`. Point a scenario at one
with `history = "../fantasy/<name>"` (the path is relative to the scenario file). Everything else
(`common/`, techs, ideas, eco scripts, map) still comes from the mod.

```
fantasy/<name>/
  history/countries/ENG - Whatever.txt   # the tag is the first 3 characters of the file name
  history/states/952 - London.txt        # same format as toi/history/states
  localisation/english/*.yml             # optional names (STATE_952: "..."), loaded on top of the mod's
```

Notes:
- **Map:** reuse real state ids with their real `provinces = { }` lists. The map is the mod's
  `provinces.bmp`, so a new id, or a state without provinces, has no pixels.
- **UCT** hosts the monthly and weekly eco ticks and only exists if it owns a state. If no state in your set is
  owned by UCT, the mod's UCT islands are added automatically.
- **What the eco reads:** state `manpower` (population), `state_category`, `resources`, `buildings`, and
  `set_variable`s in the state history (`<industry>_size`, `arable_land`, `starting_pm@...`). It also reads the
  country history: `capital`, `add_ideas` (laws feed modifiers such as tax efficiency), `set_technology` and the
  `set_tech_<tier>` effects (these seed `base_wage`), `starting_debt` and `market_leader`. Dated blocks apply up
  to the scenario's `start_date`.
- Country history runs non-eco setup too, e.g. `set_internal_politics` (legitimacy → tax efficiency). A minimal
  file that skips it gets different modifiers. Copying `ENG - Britain.txt` and trimming it is the easy start.
- Use `countries = { UCT <your tags> }` with `include_subjects = no` and `include_market_members = no`, or the
  scenario pulls in subjects and markets from your history.
