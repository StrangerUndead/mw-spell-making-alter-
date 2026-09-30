# Changelog

## 1.0.0 — unreleased (release candidate, pending in-game QA)

The first complete build of the v1.0 scope in `docs/OUTLINE.md`. Before release, the in-game gates in
`docs/dev/IN-GAME-CHECKLIST.md` (G1–G4) must pass on SE 1.5.97 and AE.

### Spellmaking
- Morrowind's spellmaker with all 24 parity rules. Rules 1–15 and 18–24 are implemented as written
  or adapted as the outline specifies; rules 16, 17 and 23 are settings.
- Scaleform menu:
  - known effects with school tabs and search;
  - up to 8 effects in Morrowind's line format;
  - effect editor with range cycling and min/max, duration and area sliders;
  - attribute and skill picker;
  - live magicka, rank, chance and price readouts;
  - cost-math panel;
  - Load, Clear and Create/Buy;
  - keyboard, mouse and gamepad.
- Three cost models:
  - **Skyrim-balanced** (default): refitted live from vanilla spells.
  - **Morrowind Classic:** the exact formula, including Target ×1.5 on the running total.
  - **Engine autocalc.**
- Price 7× cost (Classic) or 3× (balanced), flat by default. Skyrim haggling is optional.
- Optional Morrowind casting-failure module.

### Effects
- All 119 Morrowind spellmaking effects: 65 on Skyrim's own effect types, 44 with custom logic in the plugin, and 10 creature or weapon stand-ins.
- 12 Skyrim-only effects.
- 13 Extended Morrowind effects, off by default.
- Attribute translation layer; 18 Skyrim skills or Morrowind's 27.
- Mixed-range spells, touch projectiles, Morrowind-style area, and min–max rolls.
- Perk riders and vanilla keywords, so Skyrim perks apply.

### Where spells are made
- Two Altars of Spellmaking, placed at runtime: the College of Winterhold and Tel Mithryn.
- 15 NPC spellmakers with Morrowind-style refusals.
- 75 spell tomes, sold by spellmakers and added to dungeon loot at runtime.

### Spellbook
- Delete from the Magic menu.
- Load as a starting point.
- 500 custom spells, plus 500 shared sub-spell slots.
- Save-safe fixed slots with a full re-apply on every load.
- *Prepare for uninstall*.

### Extensibility
- JSON effect, discovery, rider and stand-in packs.
- Papyrus API and mod events.

### Tooling
- **Core library:** 96 unit tests, including every golden vector in the outline.
- **Data:** validator with 5,127 checks; every FormID checked against Mutagen FormKeys.
- **Plugins:** Mutagen generator with byte-identical Spriggit round trips.
- **Builds:**
  - Linux cross-compile of the DLL (clang-cl + xwin);
  - MTASC build of the SWF;
  - Linux Caprica build of the scripts.
- **Packaging:** FOMOD package script and CI workflow.
