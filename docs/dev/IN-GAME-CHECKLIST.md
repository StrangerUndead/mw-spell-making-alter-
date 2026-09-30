# In-game checklist (first run)

Everything in this repository was built and tested without Skyrim:
- `lostart_core` has unit tests.
- The DLL cross-compiles and links.
- The plugins round-trip through Spriggit.
- The SWF compiles and passes a structural check.
- The Papyrus scripts compile against stubs.

The items below can only be settled in the running game or the Creation Kit. Work through them in
order: each stage assumes the previous one works. The code marks each engine assumption with
`// VERIFY(in-game)`. Run `grep -rn "VERIFY(in-game)" skse/plugin/src` to list them.

**Tools.**
- DevBench (Nexus SE 181326) drives the scripted checks: `devbench call LostArt RunTests '["all"]'`.
  Alternatively, run the console command `la test all`.
- Read `Documents/My Games/Skyrim Special Edition/SKSE/LostArt_Tests.log` afterwards.
- `LostArt.log` in the same folder has the load summary.

## Stage 0 — install and load

1. Install the FOMOD from `build/package`.
2. Start the game to the main menu. `LostArt.log` must show `data loaded … all components installed`, with:
   - 0 variants missing;
   - 500+500 slots;
   - every warning line reviewed.
3. Start a new game (or `coc` to a test cell), open the console and run `la slots`.

## Stage 1 — gates G1/G2 (the persistence spike, OUTLINE S2)

Run `la test save` and `la test compiler`: both suites must pass. Then work through the save matrix
in `docs/dev/TESTING.md` by hand. It covers:
- creating spells, then saving and reloading;
- loading a save with different custom spells;
- starting a new game;
- saving with a custom spell in each hand;
- saving with a custom buff active;
- saving while levitating, under Slowfall, or with a summon out;
- Mark, then reload, then Recall;
- reloading with attribute damage active;
- *Prepare for uninstall*, save, remove the mod, load.

## Stage 2 — the menu (OUTLINE S3)

| Check | Code |
| --- | --- |
| `la menu` opens the SWF; `LA_Ready` arrives (otherwise an error is logged after 30 frames) | UI/SpellmakingMenu.cpp:168 |
| Tweens and text entry work; tweens run at real speed | UI/SpellmakingMenu.cpp:254 |
| `la test parity` passes (message order, buy checks, readouts, order quirk) | UI/ParityTests.cpp |
| Layout at 720p, 1080p, 1440p, 4K and 21:9; gamepad and mouse | interface/, docs/dev/MENU.md |
| Opens in under 150 ms with 200 known effects; each step under 2 ms (debug log lines) | UI/MenuController.cpp |

## Stage 3 — casting (OUTLINE S1, S6, S7)

| Check | Code |
| --- | --- |
| SpellCast fires once per release, including dual casts | Casting/CastRouter.cpp:12 |
| Rolled magnitudes land before perks (Augmented Flames stacks on the roll) | CastRouter.cpp:145 |
| The fizzle charges the release cost | CastRouter.cpp:296 |
| AddImpact runs for missiles; the area resolver hits once per actor; the ray blocks walls | CastRouter.cpp:487, 525 |
| The visual explosion reference detonates | CastRouter.cpp:554 |
| Magic menu files the spell under the costliest effect; hand art follows the lead effect | Core/SpellCompiler.cpp:103 |
| Casting experience tracks the cost override (spike S7) | — |
| Buffs from different slots that share a magic effect stack (spike S6) | — |
| `CalculateMagickaCost(nullptr)` gives Firebolt 41 in an unmodded game (live refit) | Core/DataLoader.cpp:419 |

## Stage 4 — effects (`la test effects`, plus docs/dev/TESTING-EFFECTS.md)

| Check | Code |
| --- | --- |
| Levitate lift-off and feel (spike S4); Slowfall; Jump with and without po3's Tweaks | Effects/Movement.cpp:165, 180, 280 |
| Water Walking actor value on player and NPCs (spike S5) | native effect |
| Hit-hook call site on SE 1.5.97 and AE (bytes checked before patching) | Effects/Combat.cpp:14 |
| Blind and Chameleon detection weights; Unarmored armor rating | Combat.cpp:165, 175, 398 |
| Attack speed from Fortify Agility | Effects/Stats.cpp:68 |
| Reflect bounces to the caster | Effects/Magic.cpp:128 |
| Recall and Interventions: followers arrive, markers inside the temples, fast-travel block | Effects/World.cpp:308, 340, 420, 439 |
| Command follow package; Charm at Ally rank; SpeechcraftMod prices; owned-lock crime | World.cpp:612, 623, 706, 746, 836 |
| Telekinesis reach setting | World.cpp:71 |
| Every shrine blessing carries the MagicBlessing keyword (ledger repair) | Effects/EffectRegistry.cpp:341 |

## Stage 5 — services and content

| Check | Code |
| --- | --- |
| Altar positions and rotations: data/content/altars.json is all zeros; set them in CK. Placement then survives save/load without duplicating | Services/Altars.cpp:185 |
| Leaving the altar stands the player up | Altars.cpp:266 |
| Serve/refuse INFOs pick the right line (`LA_ServiceRefusal` set in time); relationship lookup | Services/Services.cpp:76, 199 |
| Haggling entry point argument order; Fortify Barter AV | Services.cpp:479, 503 |
| Purchase flash colour | Services/Purchase.cpp:117 |
| Injected tome entries roll in loot and vendors | Services/Distribution.cpp:25 |
| Delete hotkey confirmation (button 0 = Yes); vanilla MagicMenu item cards | UI/MagicMenuExt.cpp:111, 213 |
| Hand art after load; DeselectSpell after delete | Core/Persistence.cpp:444, Core/Spellbook.cpp:472 |

## Stage 6 — Creation Kit / art pass (docs/dev/GENERATOR.md "Verify")

- MGEF variants get their art from `skyrim.vanillaEffect` at load. Spot-check the derived flags.
- Summon and Bound variants use Self delivery: the creature must appear beside the caster.
- `LA_TouchProjectile` has no model: it must fly, and hit doors and containers.
- Explosion, bound-weapon, bound-armor and book mesh paths were typed by hand.
- Stand-in race and template warnings; `levelScale` for Hunger (Lurker) and Bonewolf.
- `LA_Package_CommandFollow` inputs; `LA_Perk_SummonLimit` entry point.
- Lore-book text is placeholder; there is no voice for the dialogue.
- Data flagged `verify: true`:
  - Elemental Potency, Mystic Binding and Soul Stealer riders;
  - frost/shock companions;
  - `DLC2TelMithryn` as Neloth's tower;
  - Clean Sweep stage 200.

## Known v1.0 limitations (documented, not bugs)

- **Swap-style perks:** Elemental Potency and Mystic Binding replace the spell's effect rather than add a rider, so they don't apply to crafted summons and bound weapons.
- **Gamepad naming:** needs a keyboard. The SWF sends `LA_RequestKeyboard`, but no virtual-keyboard API is reachable.
- **Tome setting:** changing `bTomes` takes effect at the next game start.
- **Failure module:** it covers crafted spells only. Vanilla spells get the plain Sound fizzle.
