# Lost Art of Spellmaking

Morrowind's complete spellmaker for Skyrim SE/AE: every rule, limit, formula and effect of the
original. You use it at a Skyrim-native Altar of Spellmaking, or buy the service from one of
fifteen NPC spellmakers.

- **Players:** [docs/player-guide.md](docs/player-guide.md)
- **Modders:** [docs/modder-guide.md](docs/modder-guide.md) covers JSON packs, the Papyrus API and mod events.
- **Formulas:** [docs/formulas.md](docs/formulas.md)
- **Build spec:** [docs/OUTLINE.md](docs/OUTLINE.md). The interfaces between components are in [docs/dev/CONTRACTS.md](docs/dev/CONTRACTS.md).

## Status

**v1.0 release candidate, pending in-game QA.** The whole v1.0 scope is implemented and builds
from this repository, but it hasn't been run in Skyrim yet. The first-run checklist is
[docs/dev/IN-GAME-CHECKLIST.md](docs/dev/IN-GAME-CHECKLIST.md).

| Component | Where | Verified here |
| --- | --- | --- |
| Portable core: cost engine, menu rules, compiler planning, discovery, mechanics, co-save format | `skse/core` | 96 Catch2 tests, including every golden vector in the outline |
| SKSE plugin `LostArt.dll` (CommonLibSSE-NG, SE + AE) | `skse/plugin` | Cross-compiles and links (clang-cl + xwin) |
| Effect catalog and content (144 effects, 75 tomes, 15 spellmakers, 2 altars) | `data/` | `tools/validate_data.py`: 5,127 checks; FormIDs checked against Mutagen FormKeys |
| `LostArt.esp` (953 records) and `LostArt_Slots.esp` (1,002 records), ESL | `tools/Generator`, `plugin/` | Mutagen generator; byte-identical Spriggit round trip; 11 xunit tests |
| Spellmaking menu `LostArt_Spellmaking.swf` (ActionScript 2) | `interface/` | MTASC strict compile, structural check, offline harness |
| Papyrus API and MCM script | `scripts/`, `mcm/` | Caprica compile; MCM Helper schema validation |
| FOMOD installer and release zip | `fomod/`, `tools/package.py` | FOMOD schema validation |

## Building

Full commands and host setup are in [docs/dev/BUILDING.md](docs/dev/BUILDING.md).

| What | Command |
| --- | --- |
| Core tests (any host) | `cmake --preset linux-tests && cmake --build --preset linux-tests && ctest --preset linux-tests` |
| The DLL on Windows | `cmake --preset windows-msvc && cmake --build --preset windows-msvc` |
| The DLL from Linux | preset `linux-clangcl` |
| Data checks | `python3 tools/validate_data.py && python3 -m pytest tools/tests` |
| Plugins | `dotnet run --project tools/Generator -- --data data --out build/plugins`, then `tools/Generator/spriggit.sh build/plugins plugin` |
| Menu | `interface/build.sh` |
| Scripts | `scripts/build.sh` |
| Release | `python3 tools/package.py --version 1.0.0` produces `build/package/LostArtOfSpellmaking-1.0.0.zip` |

## Requirements (players)

- SKSE64
- Address Library for SKSE Plugins
- SkyUI
- MCM Helper
- Recommended: SSE Engine Fixes

## License

- **Code:** MIT.
- **Original assets:** open permissions for patches and translations.
- **No other Bethesda games:** nothing in this repository is taken from Morrowind or Oblivion.
