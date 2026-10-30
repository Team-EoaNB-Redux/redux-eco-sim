# Redux Eco Sim

A headless simulator for the EoaNB-R economy. It parses and runs the mod's real HOI4 eco scripts
(`../toi/common/...`) on a mocked HOI4 world. It has a C++ core with a CLI (`ecosim_cli`) and a Godot 4.5 UI.

Expects to sit inside the EoaNBR mod folder (`EoaNBR/redux-eco-sim/`), next to `toi/`.

## Fresh clone setup (Windows)
Needs Python 3 with SCons (`pip install scons`) and Visual Studio 2022 (C++ workload). The Godot UI also needs Godot 4.5.1.

1. **godot-cpp** (not in the repo, ~260 MB). Either copy `godot-cpp/` from another project that already built it,
   or clone and build it once:
   ```
   git clone -b 4.5 https://github.com/godotengine/godot-cpp godot-cpp
   cd godot-cpp && python -m SCons platform=windows target=template_debug -j8 && cd ..
   ```
   The CLI and tests don't need godot-cpp. Only the Godot UI extension does.
2. Build:
   ```
   python -m SCons cli=yes tests=yes -j8   # bin/ecosim_cli.exe, bin/ecosim_tests.exe
   python -m SCons -j8                     # Godot extension DLL -> project/bin/windows/
   ```
3. Run: `bin\ecosim_cli.exe run --months 12 --out runs\first` and read `runs\first\report.md`, or open
   `project/` in Godot.

## Scenarios
- `scenarios/eng_1857.txt` (default): ENG plus its subjects and market, with startup trade pinned to in-game
  values. Startup GDP is within 2% of the game.
- `scenarios/world_1857.txt`: the whole world (~4.5 s/month).
- `scenarios/british_isles.txt`: a fantasy setup. The British Isles as one closed country, with no trade.

See `CLAUDE.md` for the full workflow, diagnostics and how to extend it. `docs/SEMANTICS.md` lists which
engine behaviours are verified in-game.
