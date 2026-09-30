# Lost Art of Spellmaking — engineering contracts

This file is the single source of truth for every interface that crosses a component
boundary. The build spec is `docs/OUTLINE.md`; where this file is more specific, this file
wins. Anything not fixed here is the owning component's choice.

## 1. Repository layout

```
CMakeLists.txt            top-level; builds skse/core + tests on any host, skse/plugin on Windows/clang-cl
CMakePresets.json
vcpkg.json                plugin dependencies (commonlibsse-ng, nlohmann-json, spdlog, catch2)
skse/
  core/                   PORTABLE C++20 library "lostart_core": no Windows, no CommonLib
    include/lostart/*.h
    src/*.cpp
  plugin/                 CommonLibSSE-NG SKSE plugin "LostArt" (DLL); links lostart_core
    src/...
  tests/                  Catch2 v3 unit tests for lostart_core (+ golden vectors)
interface/                ActionScript 2 source of LostArt_Spellmaking.swf + build script
scripts/Source/           Papyrus sources (.psc)
mcm/                      MCM Helper config (installed to Data/MCM/Config/LostArt/)
data/
  schema/                 JSON Schemas (draft 2020-12) for every data file
  effects/                effect catalog, one file per family (*.json, array of EffectDef)
  standins/               stand-ins.json
  discovery/              vanilla.json (lookup table) + rules.json
  content/                attributes.json, skills.json, tomes.json, spellmakers.json,
                          altars.json, ranks.json (default cost thresholds), riders.json
  translations/           LostArt_ENGLISH.txt (UTF-16 LE with BOM) + other languages
  generated/              formmap.json written by the generator (EditorID -> local FormID)
plugin/                   Spriggit YAML of LostArt.esp and LostArt_Slots.esp
tools/Generator/          C# Mutagen generator (net8.0)
tools/*.py                validators, calibration
fomod/                    FOMOD installer (fomod/info.xml, fomod/ModuleConfig.xml)
docs/                     player guide, modder guide, formulas, dev docs
```

Installed layout (Data/):
`SKSE/Plugins/LostArt.dll`, `SKSE/Plugins/LostArt/` (all JSON from data/ except schema,
plus formmap.json), `LostArt.esp`, `LostArt_Slots.esp`, `Interface/LostArt_Spellmaking.swf`,
`Interface/Translations/LostArt_<LANG>.txt`, `Scripts/*.pex`, `Scripts/Source/*.psc`,
`MCM/Config/LostArt/config.json`, `MCM/Config/LostArt/settings.ini`.

## 2. Identifiers

### Effect ids
`<set>.<snake>`: set is `mw` (the 119 Morrowind spellmaking effects), `mwx` (Extended
Morrowind, off by default), `sk` (Skyrim-only effects). `snake` is the English effect name,
lowercase, spaces and hyphens to `_`, apostrophes dropped:
`mw.fire_damage`, `mw.summon_ancestral_ghost`, `mw.cure_blight_disease`, `mwx.summon_centurion_sphere`,
`sk.muffle`.

`Pascal` form (used in EditorIDs and translation keys) = the name with spaces, hyphens and
apostrophes removed: `FireDamage`, `SummonAncestralGhost`, `NightEye`.

### Attribute and skill sub-indices (`sub` in a spell effect)
- `-1` = none.
- Attributes, Morrowind order: 0 Strength, 1 Intelligence, 2 Willpower, 3 Agility, 4 Speed,
  5 Endurance, 6 Personality, 7 Luck.
- Skyrim skills 0–17, in RE::ActorValue order: 0 OneHanded, 1 TwoHanded, 2 Archery, 3 Block,
  4 Smithing, 5 HeavyArmor, 6 LightArmor, 7 Pickpocket, 8 Lockpicking, 9 Sneak, 10 Alchemy,
  11 Speech, 12 Alteration, 13 Conjuration, 14 Destruction, 15 Illusion, 16 Restoration,
  17 Enchanting.  (ActorValue = 6 + index.)
- Morrowind skills (option "Skill picker = Morrowind (27)"): `100 + i`, Morrowind order:
  0 Block, 1 Armorer, 2 MediumArmor, 3 HeavyArmor, 4 BluntWeapon, 5 LongBlade, 6 Axe, 7 Spear,
  8 Athletics, 9 Enchant, 10 Destruction, 11 Alteration, 12 Illusion, 13 Conjuration,
  14 Mysticism, 15 Restoration, 16 Alchemy, 17 Unarmored, 18 Security, 19 Sneak,
  20 Acrobatics, 21 LightArmor, 22 ShortBlade, 23 Marksman, 24 Mercantile, 25 Speechcraft,
  26 HandToHand.  Their mapping to Skyrim targets is `data/content/skills.json`.

### Generated EditorIDs (all records in LostArt.esp / LostArt_Slots.esp are prefixed `LA_`)
| Record | EditorID |
| --- | --- |
| MGEF variant | `LA_<Pascal>_<Self|Touch|Target>`; attribute/skill families add the target: `LA_FortifyAttribute_Strength_Self`, `LA_FortifySkill_OneHanded_Target` |
| Hidden rider MGEF (ours) | `LA_Rider_<Name>` |
| Primary slot SPEL | `LA_Slot_000` … `LA_Slot_499` (LostArt_Slots.esp) |
| Sub-spell slot SPEL | `LA_Sub_000` … `LA_Sub_499` (LostArt_Slots.esp) |
| Placeholder MGEF used by blank slots | `LA_BlankEffect` (LostArt_Slots.esp) |
| Touch projectile | `LA_TouchProjectile` |
| Area visual explosions | `LA_AreaFX_<School>_<Small|Medium|Large>` |
| Tome BOOK / taught SPEL | `LA_Tome_<Pascal>` / `LA_TomeSpell_<Pascal>` |
| Keywords | `LA_KW_Custom` (every custom-logic MGEF), `LA_KW_Rider`, `LA_KW_Touch`, `LA_KW_Blight`, `LA_KW_CustomSpell` (slots) |
| Globals | `LA_ServiceRefusal`, `LA_AltarsEnabled`, `LA_SpellmakersEnabled` |
| Lore books | `LA_Book_FragmentsOnTheLostArt`, `LA_Book_SpellmakersPrimer`, `LA_Book_OnTelvanniInscription` |
| Altar furniture | `LA_AltarCollege`, `LA_AltarTelvanni` |
| Dialogue quest / topic | `LA_ServicesQuest`, `LA_Topic_MakeSpell` |
| Stand-in actors | `LA_StandIn_<Pascal>` |
| Bound items | `LA_Bound_<Item>` (WEAP/ARMO) |

`data/generated/formmap.json` (written by the generator, read by the DLL):
`{ "LostArt.esp": { "LA_FireDamage_Target": "0x000801", ... }, "LostArt_Slots.esp": {...} }`
Local FormIDs are ESL-range `0x800–0xFFF`. The DLL resolves forms with
`TESDataHandler::LookupForm(localId, plugin)`; it never relies on runtime EditorIDs.

Vanilla references in data are written `Skyrim.esm|0x012FD0` (plugin|local id). Every vanilla
FormID in data/ must be checked against Mutagen.Bethesda.FormKeys.SkyrimSE (tools/check_formkeys.py).

## 3. Effect catalog JSON (`data/effects/*.json`)

Each file is an array of EffectDef objects. Schema: `data/schema/effect.schema.json`.

```jsonc
{
  "id": "mw.fire_damage",                 // required, unique
  "name": "$LA_Effect_FireDamage",       // translation key
  "set": "mw",                            // mw | mwx | sk
  "tier": "native",                       // native | custom | standin
  "morrowind": { "index": 14, "school": "Destruction", "baseCost": 5.0 },   // index: MW magic effect index (0-142); school may be Mysticism
  "skyrim": {
    "school": "Destruction",              // Alteration|Conjuration|Destruction|Illusion|Restoration
    "baseCost": 1.18866,                  // B^sk offline default (fitted); DLL may refit live
    "fitFrom": { "spell": "Skyrim.esm|0x012FD0", "label": "Firebolt", "magnitude": 25, "duration": 0, "area": 0, "cost": 41 },  // optional
    "vanillaTargetPriced": true,          // true => no Target x1.5 in Skyrim-balanced mode
    "archetype": "ValueModifier",        // Skyrim archetype for native effects (Script for custom)
    "actorValue": "Health",               // primary AV (native value effects), optional
    "secondAV": null,                     // optional
    "resist": "FireResist",               // optional resist AV
    "vanillaEffect": "Skyrim.esm|0x013CA9", // optional: vanilla MGEF this variant is modelled on
    "projectile": "Skyrim.esm|0x...",     // optional: projectile for the Target variant
    "flags": ["Hostile","Detrimental"]    // MGEF flags for generated variants
  },
  "ranges": ["self", "touch", "target"],  // allowed, in Morrowind order
  "magnitude": { "has": true, "unit": "pts", "ticking": true },   // unit: pts | percent | ft | level | none
  "duration": true,                       // has duration
  "area": true,                           // can have area
  "target": "none",                       // none | attribute | skill  (opens the picker)
  "keywords": ["MagicDamageFire"],        // vanilla keywords by EditorID
  "riders": ["IntenseFlames", "Impact"],  // rider ids from data/content/riders.json
  "rankLadder": { "Apprentice": 25, "Adept": 40, "Expert": 60, "Master": 100 },  // magnitude thresholds; omit => cost thresholds
  "reflectable": true,                    // Reflect can bounce it (false for Lock/Open/Calm/...)
  "hostile": true,                        // counts toward the spell's hostile flag
  "discovery": { "archetype": "ValueModifier", "actorValue": "Health", "resist": "FireResist", "hostile": true },
  "itemCard": "$LA_Card_FireDamage",      // one-line "In Skyrim" note key
  "sources": ["Skyrim.esm|0x012FD0"],     // spells or tomes that teach it (>=1 required)
  "notes": "free text for docs"
}
```

Morrowind rules the engine enforces from these fields: ranges list drives range cycling;
`area` false or range self => area hidden and 0; `duration` false => duration hidden, counts as
1 in cost; `magnitude.has` false => magnitude hidden, counts as 1+1 in cost.

## 4. Spell definition (runtime, co-save)

```cpp
struct SpellEffect { std::string effectId; int16_t sub = -1; Range range; uint16_t minMag, maxMag, duration, area; };
struct SpellDef {
  uint16_t slot; std::string name;            // <= 40 chars (UTF-8)
  std::vector<SpellEffect> effects;           // <= 8
  CostModel costModel; uint32_t cost; uint32_t pricePaid;
  float created; uint32_t provider;           // game days; provider FormID (full, resolved)
  uint32_t flags;                             // bit0 replaced, bit1 imported, bit2 needsRebalance
  std::vector<uint16_t> subSlots;             // sub-spell slots in use
};
```

Co-save: unique id `'LART'`. Records (all little-endian, each with a version number = schema):
`LADF` spell definitions, `LAMK` marks, `LALG` attribute-damage ledger, `LACP` condition pools,
`LAVR` schema version + migration history. Core provides byte-buffer encode/decode
(`lostart/Serialization.h`); the plugin only moves bytes through SKSE's interface.

## 5. Settings (MCM Helper)

MCM Helper reads defaults from `Data/MCM/Config/LostArt/settings.ini` and user values from
`Data/MCM/Settings/LostArt.ini`; ids in config.json are `key:Section`. The DLL reads both files
(user file wins) at data-load and on the mod event `LostArt_SettingsChanged` (sent by the MCM
script's OnSettingChange). Keys (type prefix b/i/f/s):

```
[General]   bAltars=1  bSpellmakers=1  iEffectNames=0 (0 Morrowind, 1 Skyrim)
[Costs]     iCostModel=0 (0 SkyrimBalanced, 1 Classic, 2 EngineAutocalc)
            bTargetRunningTotal=1  fGlobalCostMult=1.0  fPriceMult=3.0  fPriceMultClassic=7.0
            bHaggling=0  iAltarFee=0 (0 Full, 1 Half, 2 Free, 3 FilledSoulGem)
[Menu]      iMaxEffects=8  iMagnitudeCap=100  iDurationCap=1440  iAreaCap=50
            bCloseAfterCreate=1  iStartingRange=0 (0 OpenMW rule, 1 First allowed)  bShowCostMath=0
[Casting]   bCastingFailure=0  bMinMaxRolls=1  bElementalRiders=1  iTouchReach=192  bAreaLOS=1
            iSummonLimit=0 (0 Skyrim, 1 Morrowind)  bGraceSlowfall=0  fJumpPerPoint=3.0  iMarks=1
            iCastingBonus=15  bNPCFailure=0  iExperienceSchool=0 (0 Costliest, 1 Hardest)
[Effects]   iAvailability=0 (0 Known, 1 Sandbox)  bSkyrimOnly=1  bExtended=0  bPassThrough=1
            iAttributeProfile=0 (0 Default, 1 Light, 2 Off)  iSkillPicker=0 (0 Skyrim18, 1 Morrowind27)
            iFortifySkillTarget=0 (0 Level, 1 Percent)  bTomes=1  bBoundSkillBonus=0
[Services]  bCollegeMembership=1  bRefuseWanted=1  bPerNPCPrices=0
[Spellbook] iDeletable=1 (0 CustomOnly, 1 AnyNonInherent)  iLoadPricing=0 (0 Full, 1 Difference)
            bReplaceOnLoad=0
[Maintenance] iLogLevel=0 (0 Off, 1 Info, 2 Verbose)
```

## 6. Translations

`Interface/Translations/LostArt_<LANGUAGE>.txt`, UTF-16 LE with BOM, one `key<TAB>value` per
line, keys start with `$LA_`. Families: `$LA_Effect_<Pascal>` (menu name, Morrowind naming),
`$LA_EffectSk_<Pascal>` (Skyrim naming option, only where it differs), `$LA_Card_<Pascal>`
(item-card "In Skyrim" line), `$LA_Attr_<Name>`, `$LA_Skill_<Name>`, `$LA_MwSkill_<Name>`,
`$LA_Msg_<GMST>` for the Morrowind messages (sNotifyMessage28 …), `$LA_Msg_<Name>` for the
messages beyond Morrowind, `$LA_UI_*` menu labels, `$LA_MCM_*` MCM text, `$LA_Unit_*` units,
`$LA_Rank_*`, `$LA_School_*`. The DLL parses these files itself (core `StringTable`) to build
effect lines and item cards; the SWF and MCM use Skyrim's `$key` lookup.

Effect-line format (Morrowind's): `<Name>[ <min>[ to <max>] <unit>][ for <D> sec(s)][ in <A> ft] on <Self|Touch|Target>`
with keys `$LA_Fmt_To`, `$LA_Fmt_For`, `$LA_Fmt_In`, `$LA_Fmt_On`, `$LA_Unit_pt`, `$LA_Unit_pts`,
`$LA_Unit_percent`, `$LA_Unit_ft`, `$LA_Unit_Level`, `$LA_Unit_Levels`, `$LA_Unit_sec`, `$LA_Unit_secs`,
`$LA_Range_Self/Touch/Target`.

## 7. Menu protocol (SWF <-> DLL)

Menu name `LostArt_SpellmakingMenu`, movie `Interface/LostArt_Spellmaking.swf`, root clip
`_root.Menu_mc` (class `LostArtSpellmakingMenu`). All game rules live in the DLL
(core `MenuSession`); the SWF renders state and sends intents.

SWF -> DLL: `gfx.io.GameDelegate.call(name, argsArray)` with these names:

| Call | Args |
| --- | --- |
| `LA_Ready` | [] — movie loaded; DLL answers with LA_SetKnown + LA_SetState |
| `LA_SetName` | [string] |
| `LA_AddEffect` | [effectId:String] — opens the picker or the editor |
| `LA_PickTarget` | [sub:Number] — attribute/skill picker choice |
| `LA_EditEffect` | [index] — open editor on an existing row |
| `LA_RemoveEffect` | [index] |
| `LA_MoveEffect` | [index, delta(-1/+1)] |
| `LA_EditorRange` | [] — cycle range |
| `LA_EditorSet` | [field:String("min"/"max"/"duration"/"area"), value:Number] |
| `LA_EditorStep` | [field, steps:Number(+-1), big:Boolean] |
| `LA_EditorOk` / `LA_EditorCancel` / `LA_EditorDelete` | [] |
| `LA_Clear` | [] |
| `LA_LoadList` | [] — DLL answers LA_SetLoadList |
| `LA_Load` | [slot:Number] |
| `LA_Create` | [] |
| `LA_ToggleCostMath` | [] |
| `LA_Exit` | [] |
| `LA_PlaySound` | [soundKey:String] — UI feedback the SWF wants played |

DLL -> SWF (Invoke on `_root.Menu_mc`):

| Function | Payload |
| --- | --- |
| `LA_SetKnown(arr)` | array of `{id, text, school(0-4), schoolName, baseCost, ranges:"Self/Touch/Target", unit, card, icon, target(0 none,1 attribute,2 skill)}` sorted by name |
| `LA_SetState(obj)` | see below |
| `LA_SetLoadList(arr)` | `[{slot, name, text}]` |
| `LA_ShowMessage(text)` | Morrowind message box text |
| `LA_Close()` | play close animation; DLL closes the menu afterwards |

State object:
```
{ mode:"altar"|"npc", providerName, name, maxEffects, count,
  effects:[{index, id, text, school, canEdit}],
  dim:[effectIds that cannot be added now],
  cost, costText, price, gold, canAfford, chance(-1 when module off), rank(0-4), rankName,
  modelName, showCostMath, costMath:[{text, share, runningTotal}],
  picker: null | {effectId, title, options:[{sub, text}]},
  editor: null | {index(-1 new), id, title, school, icon, range, rangeText, canCycleRange,
                  hasMagnitude, min, max, magCap, hasDuration, duration, durCap,
                  hasArea, area, areaCap, unit, lineText, effectCost},
  buttons:{createLabel:"$LA_UI_Create"|"$LA_UI_Buy"} }
```

## 8. Papyrus API (`scripts/Source/LostArt.psc`, natives implemented by the DLL)

```papyrus
Scriptname LostArt Hidden
Function OpenSpellmaking(ObjectReference akProvider) global native
bool Function IsCustomSpell(Spell akSpell) global native
int Function GetCustomSpellCount() global native
int Function GetFreeSlotCount() global native
int Function GetFreeSubSlotCount() global native
bool Function DeleteCustomSpell(Spell akSpell) global native
Function SetAltarActive(ObjectReference akAltar, bool abActive) global native
int Function GetRefusalReason(Actor akSpellmaker) global native   ; 0 = serves
Function ReloadSettings() global native
int Function RebalanceAll() global native
int Function RebuildAllSlots() global native
bool Function PrepareForUninstall() global native
string Function GetVersion() global native
int Function RunTests(string asSuite) global native               ; in-game scripted checks
; Mod events: LostArt_SpellCreated(Form akSpell), LostArt_SpellDeleted(Form akSpell)
```

Refusal reasons: 0 serves, 1 not a College member, 2 wanted in the hold, 3 quest not done
(Neloth/Talvas: DLC2 "Reluctant Steward"; Othreloth: "Clean Sweep"), 4 stage-4 vampire,
5 relationship rank below Acquaintance (-1), 6 service disabled.

## 9. In-game test log

`RunTests(suite)` and the console command `la test <suite>` write
`Documents/My Games/Skyrim Special Edition/SKSE/LostArt_Tests.log`, one line per check:
`[PASS] <suite>.<case>: <detail>` / `[FAIL] <suite>.<case>: expected <x>, got <y>` and a final
`[SUMMARY] <suite>: <pass>/<total>`. Suites: `parity`, `effects`, `save`, `compiler`, `all`.
This is DevBench-friendly: `devbench call LostArt RunTests '["all"]'`, then read the log.
