# Papyrus, MCM and installer — developer notes

Owner files: `scripts/**`, `mcm/**`, `fomod/**`, `tools/package.py`, `tools/validate_mcm.py`,
`tools/validate_fomod.py`, this file. Interfaces are fixed by `docs/dev/CONTRACTS.md`
(§5 settings, §6 translations, §8 Papyrus API); this file explains how they are wired.

Design rule: Papyrus is glue. The DLL does all the work; no script polls, registers for updates,
waits in loops or keeps state that matters. Everything below is event- or call-driven.

## Scripts

| Script | Attached to | What it does |
| --- | --- | --- |
| `scripts/Source/LostArt.psc` | nothing (global API) | `Scriptname LostArt Hidden`; the 14 `global native` functions of CONTRACTS §8, each with a doc comment. Implemented by `LostArt.dll`. No state, no events. |
| `scripts/Source/LostArt_MCM.psc` | the MCM quest in `LostArt.esp` (see below) | `extends MCM_ConfigBase`. `OnSettingChange(a_ID)` sends the mod event `LostArt_SettingsChanged` (strArg = the id) and calls `LostArt.ReloadSettings()`. Fills three string properties used as readouts (spell slots, sub-spell slots, DLL version) on `OnConfigOpen` / `OnPageSelect`. Maintenance buttons call `DoRebalanceAll`, `DoRebuildAllSlots`, `DoPrepareForUninstall`, which confirm where needed, call the native and show the result with SkyUI's `ShowMessage`. |
| `scripts/Stubs/*.psc` | — | **Compile stubs, never shipped.** Minimal `Form`, `Quest`, `ObjectReference`, `Actor`, `Spell`, `TopicInfo`, `Debug`, `Game`, `Utility` (CK wiki signatures, SKSE's `Form.SendModEvent`), SkyUI's `SKI_QuestBase` / `SKI_ConfigBase` and MCM Helper's `MCM_ConfigBase` (reduced from MCM Helper's MIT public headers). Only an import path for the compiler; `tools/package.py` ships `scripts/Source/*` only. |

Altars need no script: the DLL handles the furniture activation (CONTRACTS §2 `LA_AltarCollege`,
`LA_AltarTelvanni`) itself.

### Records the plugin generator must create for these scripts

These are not in CONTRACTS §2 yet; the names below are proposals the generator owner should
confirm (and add to §2).

1. **MCM quest** in `LostArt.esp` (proposed EditorID `LA_MCMQuest`). MCM Helper finds
   `MCM/Config/<modName>/config.json` from the file name of the plugin that owns the quest, so the
   quest must live in `LostArt.esp` for `"modName": "LostArt"` to match (MCM Helper reports
   `modName ... did not match plugin` otherwise).
   - Flags: *Start Game Enabled*, not *Run Once*; priority 0; no stages needed.
   - Script: `LostArt_MCM` (no properties to fill; all are set at runtime).
   - Alias 0: Reference alias `PlayerAlias`, *Specific Reference* `PlayerRef`
     (`Skyrim.esm|0x000014`), script `SKI_PlayerLoadGameAlias` (ships with SkyUI). SkyUI uses it to
     re-register the menu on every game load.
2. **Dialogue (no fragment)**: the INFOs under `LA_Topic_MakeSpell` carry no script. The DLL
   listens for `TESTopicInfoEvent` on the serve INFO and opens the menu once the Dialogue Menu has
   closed; serve and refuse INFOs are conditioned on `GetGlobalValue LA_ServiceRefusal`, which the
   DLL sets from `GetRefusalReason` for the speaker. `OpenSpellmaking` re-checks and refuses safely.

## MCM → DLL wiring

- Layout: `mcm/config.json` (MCM Helper; installed as `Data/MCM/Config/LostArt/config.json`).
- Defaults: `mcm/settings.ini` (installed next to it). MCM Helper writes the player's changes to
  `Data/MCM/Settings/LostArt.ini` — only keys the player changed, created on the first change. Under
  Mod Organizer 2 that file lands in *overwrite*; relative `Data/...` paths still resolve through the
  VFS.
- The DLL reads both files (user file wins) at data load and again on `LostArt.ReloadSettings()` and
  on the mod event `LostArt_SettingsChanged`. `OnSettingChange` triggers **both**, so the DLL should
  treat a reload as idempotent (or ignore the event when a native reload happened in the same
  frame). MCM Helper writes the INI synchronously *before* it dispatches `OnSettingChange`, so the
  file is current when the DLL reads it. Values are written as `1`/`0` for bools, integers as
  decimal, floats as `std::to_string` output (`1.000000`) — parse leniently.
- *Reset to default* on an option also fires `OnSettingChange` with that id.
- `bAltars:General` / `bSpellmakers:General` gate the altars and dialogue; the DLL should mirror them
  into the globals `LA_AltarsEnabled` / `LA_SpellmakersEnabled` on every reload, since dialogue
  conditions and furniture scripts cannot read the INI.
- Readouts are `text` controls bound through `valueOptions.propertyName` to string auto-properties of
  `LostArt_MCM` (`SpellSlotsText`, `SubSlotsText`, `VersionText`). Spells: `used / (used + free)` from
  `GetCustomSpellCount` + `GetFreeSlotCount`; sub-spells: `500 - GetFreeSubSlotCount()` of 500
  (`SubSlotTotal`, matching `LA_Sub_000..499`). Without the DLL they show `-` and the version shows
  `$LA_MCM_DllMissing`.
- Buttons are `text` controls with `"action": {"type": "CallFunction", "function": "Do..."}`. They
  have no `id` on purpose: a `text` control with an id becomes a string ModSetting.
- Casting page: `bCastingFailure` is a group control (`groupControl: 1`); `iCastingBonus` and
  `bNPCFailure` are greyed out while it is off (`groupCondition: 1`). Their values are still stored.

### Expected native return values (buttons)

| Native | MCM shows |
| --- | --- |
| `RebalanceAll()` ≥ 0 | `$LA_MCM_Msg_Rebalanced{n}` — "Rebalanced n custom spells." (after a confirm dialog) |
| `RebuildAllSlots()` ≥ 0 | `$LA_MCM_Msg_Rebuilt{n}` |
| either < 0 | `$LA_MCM_Msg_Failed` |
| `PrepareForUninstall()` true / false | `$LA_MCM_Msg_UninstallDone` / `$LA_MCM_Msg_UninstallFailed` (after a confirm dialog) |

The natives are called on a Papyrus VM thread while the game is in menu mode. They must return
promptly: count the work, queue record edits through SKSE's task interface (CONTRACTS threading
rule), and return the count. `{n}` uses SkyUI's nested translation (`{}` in the translated string).

### Settings map

Every id is `key:Section` exactly as in CONTRACTS §5; the type prefix picks the MCM Helper source
(`b` → `ModSettingBool`, `i` → `ModSettingInt`, `f` → `ModSettingFloat`). Enum values are the
integer the DLL reads.

| Page | MCM id (`key:Section`) | Control | Default | Values | Label |
| --- | --- | --- | --- | --- | --- |
| General | `bAltars:General` | toggle | 1 | 0 off, 1 on | Altars of Spellmaking |
| General | `bSpellmakers:General` | toggle | 1 | 0 off, 1 on | NPC spellmakers |
| General | `iEffectNames:General` | enum | 0 | 0 Morrowind, 1 Skyrim | Effect names |
| Costs | `iCostModel:Costs` | enum | 0 | 0 Skyrim-balanced, 1 Morrowind Classic, 2 Engine autocalc | Cost model |
| Costs | `bTargetRunningTotal:Costs` | toggle | 1 | 0 off, 1 on | Target x1.5 on the running total |
| Costs | `fGlobalCostMult:Costs` | slider | 1.0 | 0.25–4.0 step 0.05 | Global cost multiplier |
| Costs | `fPriceMult:Costs` | slider | 3.0 | 1.0–20.0 step 0.5 | Price multiplier |
| Costs | `fPriceMultClassic:Costs` | slider | 7.0 | 1.0–20.0 step 0.5 | Price multiplier (Classic) |
| Costs | `bHaggling:Costs` | toggle | 0 | 0 off, 1 on | Skyrim haggling |
| Costs | `iAltarFee:Costs` | enum | 0 | 0 Full price, 1 Half price, 2 Free, 3 One filled soul gem | Altar fee |
| Menu | `iMaxEffects:Menu` | slider | 8 | 1–8 step 1 | Max effects per spell |
| Menu | `iMagnitudeCap:Menu` | slider | 100 | 100–500 step 10 | Magnitude cap |
| Menu | `iDurationCap:Menu` | slider | 1440 | 60–3600 step 60 | Duration cap |
| Menu | `iAreaCap:Menu` | slider | 50 | 0–100 step 5 | Area cap |
| Menu | `bCloseAfterCreate:Menu` | toggle | 1 | 0 off, 1 on | Close after creating |
| Menu | `iStartingRange:Menu` | enum | 0 | 0 OpenMW rule, 1 First allowed | Starting range |
| Menu | `bShowCostMath:Menu` | toggle | 0 | 0 off, 1 on | Show cost math |
| Casting | `bCastingFailure:Casting` | toggle | 0 | 0 off, 1 on | Casting failure |
| Casting | `iCastingBonus:Casting` | slider | 15 | 0–30 step 1 | Casting bonus |
| Casting | `bNPCFailure:Casting` | toggle | 0 | 0 off, 1 on | Casting failure for NPCs |
| Casting | `iExperienceSchool:Casting` | enum | 0 | 0 Costliest effect, 1 Hardest effect | Experience school in Classic |
| Casting | `bMinMaxRolls:Casting` | toggle | 1 | 0 off, 1 on | Min-max rolls |
| Casting | `bElementalRiders:Casting` | toggle | 1 | 0 off, 1 on | Skyrim elemental riders |
| Casting | `iTouchReach:Casting` | slider | 192 | 128–320 step 8 | Touch reach |
| Casting | `bAreaLOS:Casting` | toggle | 1 | 0 off, 1 on | Area needs line of sight |
| Casting | `iSummonLimit:Casting` | enum | 0 | 0 Skyrim, 1 Morrowind (one per type) | Summon limit |
| Casting | `bGraceSlowfall:Casting` | toggle | 0 | 0 off, 1 on | Grace Slowfall after Levitate |
| Casting | `fJumpPerPoint:Casting` | slider | 3.0 | 1.0–10.0 step 0.5 | Jump height per point |
| Casting | `iMarks:Casting` | slider | 1 | 1–10 step 1 | Marks |
| Effects | `iAvailability:Effects` | enum | 0 | 0 Known spells, 1 Sandbox | Effect availability |
| Effects | `bSkyrimOnly:Effects` | toggle | 1 | 0 off, 1 on | Skyrim-only effects |
| Effects | `bExtended:Effects` | toggle | 0 | 0 off, 1 on | Extended Morrowind effects |
| Effects | `bPassThrough:Effects` | toggle | 1 | 0 off, 1 on | Pass-through modded effects |
| Effects | `bTomes:Effects` | toggle | 1 | 0 off, 1 on | Morrowind tomes in shops and loot |
| Effects | `iAttributeProfile:Effects` | enum | 0 | 0 Default, 1 Light, 2 Off | Attribute profile |
| Effects | `iSkillPicker:Effects` | enum | 0 | 0 Skyrim (18), 1 Morrowind (27) | Skill picker |
| Effects | `iFortifySkillTarget:Effects` | enum | 0 | 0 Skill level, 1 Percent modifier | Fortify Skill target |
| Effects | `bBoundSkillBonus:Effects` | toggle | 0 | 0 off, 1 on | Bound item skill bonuses |
| Services | `bCollegeMembership:Services` | toggle | 1 | 0 off, 1 on | College masters require membership |
| Services | `bRefuseWanted:Services` | toggle | 1 | 0 off, 1 on | Refuse wanted players |
| Services | `bPerNPCPrices:Services` | toggle | 0 | 0 off, 1 on | Per-NPC price modifiers |
| Spellbook | `iDeletable:Spellbook` | enum | 1 | 0 Custom only, 1 Any non-inherent | Deletable spells |
| Spellbook | `iLoadPricing:Spellbook` | enum | 0 | 0 Full price, 1 Difference only | Load pricing |
| Spellbook | `bReplaceOnLoad:Spellbook` | toggle | 0 | 0 off, 1 on | Replace original on load |
| Maintenance | `iLogLevel:Maintenance` | enum | 0 | 0 Off, 1 Info, 2 Verbose | Debug logging |

Landing page (before a page is picked): title, a hint, and the three readouts.

## Translations

`mcm/LostArt_MCM_ENGLISH.src.tsv` (UTF-8, `key<TAB>value`, no BOM) holds every `$LA_MCM_*` key used
by `config.json` and the Papyrus sources — 181 keys. The data owner merges it into
`data/translations/LostArt_ENGLISH.txt` (UTF-16 LE with BOM). Key families:

| Pattern | Use |
| --- | --- |
| `$LA_MCM_Title`, `$LA_MCM_Page_<Page>`, `$LA_MCM_Hdr_<Name>` | menu title, pages, headers |
| `$LA_MCM_<Key>` / `$LA_MCM_<Key>_Help` | option label / info text (`<Key>` = setting key without its type prefix, e.g. `CostModel`) |
| `$LA_MCM_Opt_<Key>_<Value>` (+ `_Short`) | enum option names and short names |
| `$LA_MCM_SpellSlots`, `SubSlots`, `Version` (+ `_Help`), `DllMissing`, `Landing` | readouts |
| `$LA_MCM_Rebalance`, `Rebuild`, `Uninstall` (+ `_Help`), `Run` | buttons |
| `$LA_MCM_Msg_*` | `ShowMessage` texts; `Msg_Rebalanced` / `Msg_Rebuilt` contain `{}` |

Slider unit suffixes (`{0} pts`, `{0} s`, `{0} ft`, `{1}%`, `{2}x`) are literal format strings, not
translated.

## Building the scripts

```bash
scripts/build.sh            # builds Caprica once into build/tools/caprica, compiles to build/papyrus/
scripts/build.sh --check    # compile into a temp dir and verify only (CI)
```

- Compiler: [Caprica](https://github.com/Orvid/Caprica) (MIT). Upstream only builds with MSVC; the
  script uses nikitalita's cross-platform branch `os-independent` pinned at
  `932705b74bd4380f2ead4f49455980856742b399` plus `scripts/caprica/caprica-linux.patch`:
  - GCC/Clang build fixes (`<x86intrin.h>`, `<unistd.h>` instead of `<io.h>`, missing
    `<algorithm>`, `open()` mode argument);
  - `getUserName()` falls back to `$USER` when there is no login terminal (CI, containers);
  - `debugBreak()` no longer raises SIGTRAP on every compile error unless `CAPRICA_DEBUGBREAK` is
    set (errors now exit with status 255 instead of dumping core);
  - **Skyrim semantics:** native functions are accepted in scripts without the Native flag when
    `--game skyrim`. Skyrim has no Native script flag; the Creation Kit compiler accepts
    `Function X() global native` in any script, and every SKSE plugin API (including CONTRACTS §8)
    depends on it. Caprica otherwise only allows it in its hard-coded list of vanilla native classes.
- Build prerequisites (Ubuntu 24.04): `cmake ninja-build clang libboost-filesystem-dev
  libboost-program-options-dev libboost-container-dev libfmt-dev libpugixml-dev`. The build adds
  `-msse4.2` on x86-64 (Caprica's string hashing uses CRC32 intrinsics). One-time build ≈ 1.5 min.
- Command the script runs (from `scripts/Source`, so the `.pex` records a short source path):
  `Caprica --game skyrim --ignorecwd --anonymize --quiet --import <PAPYRUS_IMPORTS...>
  --import scripts/Source --import scripts/Stubs --flags scripts/Stubs/TESV_Papyrus_Flags.flg
  --output build/papyrus LostArt.psc LostArt_MCM.psc`
- `PAPYRUS_IMPORTS=dir1:dir2 scripts/build.sh` puts real headers (SKSE64 `Scripts/Source`, the SkyUI
  SDK, MCM Helper `Source/Scripts`) ahead of the stubs. Do this on a machine with the game installed
  before a release; the stubs only cover what these scripts call.
- On Windows use a Caprica release or the CK's `PapyrusCompiler.exe` with the same import order
  (`CAPRICA=C:\path\Caprica.exe bash scripts/build.sh` works from Git Bash).
- Verification: `scripts/pexinfo.py` is an independent `.pex` reader (header, string table, debug
  info, objects, properties, every function including bytecode; fails on truncation or leftovers).
  `build.sh` runs it on every output and `pexinfo.py --check-api build/papyrus/LostArt.pex` checks
  that `LostArt.pex` contains exactly the 14 CONTRACTS §8 functions, all `global native`, with the
  contract's return and parameter types.
- Known quirk: Caprica writes each object's size field little-endian (upstream does the same on
  Windows). Skyrim's loader does not use it (Caprica's own reader ignores it, and Caprica-built
  Skyrim scripts load in game); `pexinfo.py` accepts it. Not verified in game for these scripts.

## Validating MCM and FOMOD

```bash
python3 tools/validate_mcm.py     # jsonschema + MCM Helper parser rules + CONTRACTS §5 + translations
python3 tools/validate_fomod.py   # ModuleConfig.xml vs FOMOD 5 XSD (lxml), info.xml fields
python3 tools/package.py --check  # lists every missing release input
python3 tools/package.py --version 1.0.0   # stage, validate references, zip to build/package/
```

- `mcm/schema/config.schema.json` is MCM Helper's schema, vendored unmodified. It has upstream bugs:
  its generic numeric rule sets `unevaluatedProperties: false` on the `valueOptions` of `enum` and
  `stepper`, so it rejects *every* enum with `options` (MCM Helper's own SkyUI_SE example included);
  `"type": "bool"`, `"type": {"enum": …}` and draft-04 `exclusiveMaximum: true` make it an invalid
  2020-12 schema. `validate_mcm.py` applies those corrections in memory (printed as notes) and then
  requires zero errors. It additionally checks the keys MCM Helper's parser reads
  (`src/Json/*Handler.cpp`), that every id/default/enum size equals CONTRACTS §5 (parsed from the
  contract file itself), slider defaults on range and step, action functions and readout properties
  exist in `LostArt_MCM.psc`, and translations are complete with no unused keys.
- `fomod/schema/ModuleConfig.xsd` (GandaG/fomod-schema, now fomod-lang/fomod; MIT) has a typo
  (`type=" xs:string"`) that libxml2 will not compile; `validate_fomod.py` fixes it in memory.
  Equivalent xmllint: `sed 's/type=" xs:string"/type="xs:string"/' fomod/schema/ModuleConfig.xsd >
  /tmp/mc.xsd && xmllint --noout --schema /tmp/mc.xsd fomod/ModuleConfig.xml`.

## FOMOD

- `requiredInstallFiles`: the whole `Core/` folder to `Data/` (CONTRACTS §1 installed layout).
- One step, *Game language* (SelectExactlyOne): *English only* (recommended, nothing extra) or one of
  French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese, each installing
  `Interface/Translations/LostArt_<LANG>.txt`. Skyrim does not fall back to English for Interface
  translations, so a non-English game without that file shows raw `$LA_…` keys; `package.py` ships the
  English text under the language's name until a real translation exists in `data/translations/`.
- Requirements are in `fomod/info.xml`'s description (SKSE64, Address Library, SkyUI, MCM Helper;
  SSE Engine Fixes recommended). FOMOD `fileDependency` only sees plugins, so they are not enforced.
- JSON data keeps its sub-folders under `SKSE/Plugins/LostArt/` (`effects/`, `content/`, `standins/`,
  `discovery/`); `data/generated/formmap.json` lands at `SKSE/Plugins/LostArt/formmap.json`. If the
  DLL owner wants a different mapping, change `DATA_FLATTEN_DIRS` / `DATA_EXCLUDE_DIRS` in
  `tools/package.py`.

## Pitfalls to remember

- A recompiled `.pex` is read only at game start; a quick-load keeps running the old bytecode for
  script instances baked into the save (the MCM quest's `LostArt_MCM` instance is one). After
  changing `LostArt_MCM.psc`, quit to desktop and relaunch; verify with a distinctive string in the
  `.pex` (`strings build/papyrus/LostArt_MCM.pex`).
- `LostArt_MCM` is persisted in saves. Adding properties or functions later is safe; renaming the
  script, removing properties or changing their types is not.
- Do not name anything `state` (reserved). Do not recompile `SKI_*` / `MCM_ConfigBase` — the stubs
  exist only for compiling our scripts and must never be packaged.
- `LostArt.*` natives return defaults (`None`/`0`/`false`/`""`) and log "unbound native" if the DLL
  failed to load; `GetVersion() == ""` is the check other mods should use.
- Nothing registers for updates, so removing the mod leaves no periodic Papyrus errors behind.
