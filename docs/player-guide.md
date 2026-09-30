# Lost Art of Spellmaking — Player Guide

Morrowind's spellmaker, rebuilt for Skyrim SE and AE. You combine up to eight effects into your own spells at an Altar of Spellmaking, or you pay one of fifteen NPC spellmakers to do it for you. The rules, limits, formulas and effects are Morrowind's.

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x (Steam or GOG).
- SKSE64 matching your game version.
- Address Library for SKSE Plugins.
- SkyUI.
- MCM Helper.
- Dawnguard and Dragonborn, which every SE/AE copy includes.
- Recommended: SSE Engine Fixes.

Install the FOMOD with your mod manager. Both plugins are ESL-flagged, so they don't take a load-order slot.

## Where to make spells

| Place | Who can use it | Fee |
| --- | --- | --- |
| Altar of Spellmaking, Arcanaeum annex, College of Winterhold | College members | The spell's price, paid to the College. Free for the Arch-Mage |
| Altar of Spellmaking, Neloth's upper tower, Tel Mithryn | Anyone | The spell's price, paid to Neloth |
| NPC spellmakers ("I'd like to make a spell.") | Depends on the spellmaker, see below | The spell's price, flat and not haggled, as in Morrowind |

Spellmakers:
- **College of Winterhold:** Tolfdir, Faralda, Drevis Neloren, Colette Marence and Phinis Gestor. They serve College members only.
- **Court wizards:** Sybille Stentor, Farengar, Wuunferth, Wylandriah, Calcelmo and Madena, plus Falion in Morthal. They won't serve you while you're wanted in their hold.
- **Solstheim:**
  - Neloth and Talvas: after *Reluctant Steward*.
  - Elder Othreloth: after *Clean Sweep*.

No spellmaker serves a stage-4 vampire, or someone they dislike.

## Learning effects

As in Morrowind, the spellmaker only offers effects from spells you already know. Only normal spells count; powers, abilities and diseases don't. A single Fortify Attribute spell unlocks every attribute.

Spellmakers sell Morrowind spell tomes in their specialty: Levitate, Mark, Recall, Ondusi's Open Door, the elemental shields, and more. Other tomes turn up as dungeon loot. MCM → Effects → *Sandbox* unlocks everything.

## The menu

- **Name** (top): up to 40 characters. Press Enter to create.
- **Effects Known** (left): alphabetical, with school tabs (All, A, C, D, I, R). Press `/` to search.
- **Spell Effects** (right): up to 8 effects, shown in Morrowind's line format, such as *Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target*.
- **Effect editor:**
  - Range cycles Self → Touch → Target.
  - Magnitude is set with min and max sliders.
  - Duration is 1–1440 s.
  - Area is 0–50 ft; it's hidden on Self.
- **Readouts:** Magicka, Rank, Chance (only when casting failure is on), Price and your gold.
- **Buttons:** Create (Buy at an NPC), Load (start from one of your custom spells), Clear, Exit.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Move / switch pane | Arrows | D-pad |
| School tab | A / D | LB / RB |
| Reorder effect | Shift+Up/Down | LB / RB |
| Add or edit | Click, E, Enter | A |
| Remove | Delete | X in the editor |
| Cycle range | F | Y |
| Slider small / big step | Left/Right, Shift+Left/Right | D-pad, LT/RT |
| Rename | T | Y |
| Create / Buy | R | X |
| Cost math | F1 | Left stick click |
| Exit | Tab, Esc | B |

You'll see Morrowind's own messages when something isn't allowed. For example, *You can only add eight effects to a spell.* or *This effect has already been added.*

## Costs

The default **Skyrim-balanced** model prices each effect on Skyrim's own curve, so a rebuilt Firebolt costs 41 magicka, just like the vanilla one.

**Morrowind Classic** uses Morrowind's exact formula. In that mode:
- Effect order matters, because every Target effect multiplies the running total by 1.5.
- Control spells are cheap and long buffs are expensive.

The cost-math panel (F1) shows each effect's share. See [formulas.md](formulas.md) for the exact numbers.

Crafted spells use Skyrim's skill reductions, half-cost perks and dual-cast rules like any other spell. Keyword perks apply to their effects: Augmented Flames, Twin Souls, Mage Armor and the Regeneration perk.

## Casting

- **Mixed ranges:** one spell can buff you and launch a bolt, as in Morrowind. The Self part lands when you release the cast.
- **Touch:** touch spells reach about an arm's length (192 units), and can open doors and containers.
- **Magnitude:** rolls between min and max for each target. Effects that tick over time apply the average every second.
- **Area:** hits everyone within the radius except you. It needs line of sight by default.
- **Casting failure:** off by default. Turn it on in the MCM to cast by Morrowind's chance formula, with stamina standing in for fatigue.

## Managing spells

- **Delete a spell:** in the Magic menu, Shift+click it, press Delete, or click the left stick on gamepad.
  - You can delete any spell that isn't racial, a power, from a standing stone, or given by a quest.
  - MCM → Spellbook can restrict deleting to custom spells only.
- **Load:** at an altar or spellmaker, Load starts a new spell from a custom one, at full price. Replacing the original, and paying only the difference, are MCM options.
- **Hotkeys and favorites:** they survive saves, loads and restarts.
- **Capacity:** up to 500 custom spells.

## Settings

Everything is in the MCM (Lost Art of Spellmaking). Defaults favor Skyrim for magicka, gold, summons, skills and perk riders, and Morrowind for the spellmaking rules themselves.

## Uninstalling

1. MCM → Maintenance → **Prepare for uninstall**. This removes custom spells from you and your followers, ends their effects, and clears marks and ledgers.
2. Save, quit, and remove the mod.

If you remove the mod without step 1, blank spells stay in your spell list, and orphaned co-save data is left behind.

## Known limits (v1.0)

- **Spell types:** no concentration, rune or wall spells yet (v1.x).
- **Perks that swap effects:** Elemental Potency and Mystic Binding don't yet affect crafted summons and bound weapons. Both work by replacing the spell's effect, which crafted spells can't do yet.
- **Stand-ins:** summons of creatures Skyrim lacks use Skyrim stand-ins (Scamp → Familiar, Golden Saint → Dremora Valkynaz, …). The item card credits the Morrowind original.
- **Voice:** spellmaker dialogue has no voice acting.
- **Multiplayer:** Skyrim Together and SkyMP are not supported.

## Troubleshooting

- **Log:** `Documents/My Games/Skyrim Special Edition/SKSE/LostArt.log`. Set MCM → Maintenance → Debug logging to *Verbose* before reporting a bug.
- **Console:**
  - `la slots` shows slot usage.
  - `la rebuild` re-applies every custom spell.
  - `la discover` writes the effects you know to the log.
