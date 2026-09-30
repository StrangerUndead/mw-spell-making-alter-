# Testing Lost Art of Spellmaking in game

Unit tests for the portable core run in CI (`ctest --preset linux-tests`, see BUILDING.md). This
page covers what only the game can check: the in-game test runner, and the manual save and load
matrix from OUTLINE.md ("QA & acceptance").

## In-game test runner

Run a suite from the console or from Papyrus:

```text
la test <suite>                         console (suite: parity, effects, save, compiler, all)
LostArt.RunTests("save")                Papyrus, returns the number of failed checks (-1: unknown suite)
devbench call LostArt RunTests '["all"]'
```

Each run replaces `Documents/My Games/Skyrim Special Edition/SKSE/LostArt_Tests.log` (GOG:
`Skyrim Special Edition GOG`). There is one line per check, then a summary per suite:

```text
[PASS] compiler.mw.fire_damage: 3 spell record(s) over 3 range(s)
[FAIL] save.slot_integrity: 'Fireball II': expected cost override 57, got 0
[SUMMARY] save: 6/7
```

Paste the log verbatim into a bug report. The same lines also go to `SKSE/LostArt.log`.

### Suite `compiler`

One case per catalog effect (pass-through effects excluded). For each allowed range the effect is
planned at magnitude 5-10, duration 10 and area 5, using the first target of attribute and skill
families, and compiled into scratch sub-spell slots (the highest free `LA_Sub_*` records, freed
afterwards). A case passes when, for every range:

- the plan fits (no "too complex");
- every record holds 1 to 15 effects and no more than the plan has entries;
- the record is Fire and Forget with delivery Self (Self range) or Aimed (Touch and Target);
- every effect has a real magic effect (never the `LA_BlankEffect` placeholder), every magic
  effect is Fire and Forget, and every `LA_` variant has the spell's delivery (vanilla riders are
  exempt);
- the cost-override flag is set, with the spell's cost on the equipped spell and 0 on linked
  sub-spells;
- the engine's hostility (`MagicItem::IsHostile`) matches the plan;
- blanking the scratch record leaves exactly the placeholder.

An effect whose variants are missing from `LostArt.esp` fails with "missing variants": the
load log lists which ones.

### Suite `save`

Checks that run on the live game without reloading:

| Case | Passes when |
| --- | --- |
| `slot_integrity` | every custom spell's `LA_Slot` record has cost override = the definition's cost and name = its name, is in the player's spell list, and has as many compiled zero-cost sub-spells as its plan needs |
| `pool_counts` | used primary/sub slots equal the definitions' slots, every sub slot is owned by its spell, and every free slot record is blank |
| `cosave_roundtrip` | encoding then decoding LADF, LAMK, LALG, LACP and LAVR gives back identical data |
| `cosave_parser` | a synthetic `.skse` file (SKSE64 layout, a foreign plugin block first) parses back to the current definitions, and a truncated one is rejected |
| `cosave_file` | the co-save of the save loaded this session parses (reports how many definitions it holds) |
| `rebuild_idempotent` | "Rebuild all slots" leaves every record's content unchanged |
| `revert_rebuild_copy` | compiling every definition into scratch records (what a revert and reload would do) gives records identical to the live ones |

## Save and load matrix (manual)

Run every row on Steam SE 1.5.97, Steam AE (current) and GOG AE. Use a fresh profile with only
SKSE, Address Library, SkyUI, MCM Helper and Lost Art unless the row says otherwise. Set
`iLogLevel=2` in the MCM (Maintenance) so `LostArt.log` is verbose. After each row run
`la test save` and keep the log.

"Clean log" below means: no `[error]` or `[critical]` lines in `LostArt.log`, and no
`[warning]` lines except those the row expects.

Preparation (once): at an altar or with `la menu`, make these spells (sandbox mode, MCM Effects,
makes it quick):

- **A** "Test Bolt": Fire Damage 10-20 pts on Target
- **B** "Test Ward": Shield 20 pts for 60 secs on Self
- **C** "Test Mixed": Fire Damage 10 pts on Target + Fortify Strength 10 pts for 30 secs on Self
  (a mixed-range spell: uses one sub slot)
- **D** "Test Touch": Open 20 pts on Touch

Favorite A and B and bind them to hotkeys 1 and 2.

| # | Scenario | Steps | Pass criteria |
| --- | --- | --- | --- |
| 1 | Create, save, reload | Save (hard save, not quicksave). Quit to main menu. Load it. | Magic menu lists A-D with the same names, costs and item-card lines as before saving; hotkeys 1 and 2 cast A and B; favorites still hold A and B; `la test save` all PASS; log line `co-save loaded: 4 custom spells`; clean log |
| 2 | Load a save with different custom spells, no restart | Save as S1. Delete D (Shift+click, confirm), create **E** "Test Heal" (Restore Health 20 pts on Self), save as S2. Load S1, then S2, then S1 | After each load the Magic menu shows exactly that save's custom spells (S1: A-D, no E; S2: A-C and E, no D); `la slots` lists the same; `la test save` all PASS (`pool_counts` proves free slots are blank) |
| 3 | New game after playing | From a save with A-D loaded, start a new game (main menu → New, or `coc qasmoke` from a fresh start) | `la slots` reports 0 custom spells; `la test save` `pool_counts` PASS (every slot blank) |
| 4 | Custom spell in each hand | Equip A in the left hand and C in the right hand. Save, quit to main menu, load | Both hands show the fire hand art immediately after loading and cast correctly; log shows `re-equipped` lines at verbose level |
| 5 | Custom buff active | Cast B (60 s). Wait until about 40 s remain (check Active Effects). Save, reload. Then cast B again | After reload Active Effects shows "Test Ward" with the remaining time within ±2 s of the time at saving; armor rating still raised; recasting refreshes B (one entry in Active Effects, not two). Log: `early restore: 4 of 4 custom spells compiled` and `already compiled before the save loaded`; no `early restore disagreed` warning |
| 6 | Save while levitating / slowfalling / with a summon | Make and cast Levitate 10 for 60 s; save in the air; reload. Repeat with Slowfall 20 for 60 s while falling from a ledge, and with a summon out | Each state is either restored (still levitating, slow fall continues, summon present with remaining time) or ended safely (player lands without fall damage from the levitation height, summon gone); no stuck controls; clean log |
| 7 | Mark and Recall | Make Mark and Recall spells. Cast Mark in Whiterun market. Save, reload, travel elsewhere, cast Recall | Player arrives at the marked spot, facing the marked direction |
| 8 | Attribute damage | Make Damage Strength 10 pts for 5 secs on Target; cast on an NPC dummy until Strength is lowered (or on self through a reflect). Save, reload | Ledger intact: damaged Strength is still lowered after reload; a Restore Strength spell repairs it fully |
| 9 | Update the mod between saves | Save with A-D on the previous release. Install the new release (same slots plugin). Load | Log shows `co-save: migrating schema X -> Y` when the schema changed; `la slots` shows A-D in the same slot numbers as before (compare with `la slots` output taken before updating); `la test save` all PASS |
| 10 | Prepare for uninstall | MCM → Maintenance → Prepare for uninstall. Save. Quit. Disable LostArt.esp, LostArt_Slots.esp and remove LostArt.dll. Load that save | Before saving: Magic menu has no custom spells, no custom active effects. After loading without the mod: no crash, no missing-master prompt other than the expected plugin warning, and no custom spells anywhere |
| 11 | Unrelated mod added or removed mid-playthrough | Save with A-D. Add an unrelated plugin (e.g. a new armor mod) before LostArt in the load order, load; save; remove it again, load | Custom spells work in both loads, same names and costs; hotkeys intact; `la test save` all PASS |

Record for each row: game version, row number, PASS/FAIL, and the `LostArt_Tests.log` plus the
relevant `LostArt.log` lines.
