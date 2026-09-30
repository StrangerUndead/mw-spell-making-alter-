# Morrowind Spellmaking for Skyrim — Full Project Outline

Sep 29, 2026

## Vision & scope

**Lost Art of Spellmaking** (working title) puts Morrowind's complete spellmaker into Skyrim SE/AE: every rule, limit, formula and effect of the original, used at a Skyrim-native Altar of Spellmaking or bought from NPC spellmakers.

A note on the source: in Morrowind, spellmaking was a paid service sold by spellmaker NPCs, and the Altar of Spellmaking comes from Oblivion's Arcane University. The mod ships both, the altar as the headline feature and faithful NPC spellmakers beside it, sharing one menu and one rule set.

**Design pillars** (goal: the complete Morrowind spellmaker, playing like it shipped with Skyrim)

| Pillar | Means | Proof |
| --- | --- | --- |
| Faithful | Every MW rule covered; exact MW formulas; changes are opt-in | Parity checklist |
| Native | Skyrim perks apply; crafting-menu feel; gamepad and mouse | Vanilla perk tests |
| Robust | Save-safe slots; clean uninstall; no script lag | Save-test matrix |
| Extensible | JSON effect packs; reads modded spells; public Papyrus API | One-file add-on |

Foundation: SKSE plugin · two generated ESL plugins · Scaleform menu · MCM Helper

Each pillar has a test: Faithful is checked line by line against the parity checklist in Morrowind reference; Native, by the perk, keyword and UI tests in QA; Robust, by the save-test matrix in QA; Extensible, by an add-on built from one JSON file.

### Scope

| Area | v1.0 (ship) | v1.x (stretch) | Out of scope |
| --- | --- | --- | --- |
| Spellmaking menu | Name, known effects, up to 8 effects, range, min/max magnitude, duration, area, live cost, price and optional casting chance, create | — | — |
| Effects | Every Morrowind spellmaking effect, implemented or given a documented stand-in; Skyrim-only effects from spells you know; JSON effect packs from other authors | Concentration, rune and wall effects | Morrowind enchanting (a later, separate module) |
| Where | Altar of Spellmaking at the College of Winterhold and Tel Mithryn; NPC spellmakers charging Morrowind's flat, non-negotiable fee | Buildable altar for player homes | — |
| Learning effects | Morrowind rule (effects of spells you know) plus Morrowind spell tomes | Learn effects by studying scrolls | — |
| Casting | Mixed-range spells, min/max rolls, Morrowind durations, Skyrim perks and dual casting, optional Morrowind casting failure (off by default) | — | — |
| Spellbook | Delete/forget, edit as copy, hotkeys that survive reloads | Teach followers, scribe tomes, share builds as JSON | — |
| Platform | SE 1.5.97 (Steam) and AE 1.6.x (Steam and GOG) | SkyrimVR | Skyrim LE; multiplayer sync |
| Assets | Vanilla and DLC assets, original work | — | Anything ported from Morrowind or Oblivion |

### Platform and dependencies

| Item | Choice | Why |
| --- | --- | --- |
| Game | Skyrim SE 1.5.97 and AE 1.6.x | One DLL for both through CommonLibSSE-NG and Address Library |
| Script extender | SKSE64 | Runtime spell editing, co-save persistence, custom menu |
| Required mods | Address Library for SKSE Plugins, SkyUI, MCM Helper | Version-independent offsets; standard settings menu |
| DLC | Dawnguard and Dragonborn (in every SE/AE copy) | Solstheim altar, Raven Rock, Dunmer architecture, extra summons |
| Plugin | Two ESL-flagged ESPs: content and spell slots | Neither takes a load-order slot; budget in Data model |
| Prototype only | powerofthree's Papyrus Extender, UIExtensions | Papyrus-only demo before the C++ plugin exists |

Milestone 1 is a Papyrus-only prototype that validates persistence and the menu flow before the C++ plugin is built.

The spellmaking altar system and its implementation are the whole of v1.0. A questline is deliberately left out and can be added later.

**How to use this outline:** it is the build spec. Numbers, defaults and formulas are decisions unless a technical spike (see Roadmap) marks them as open. Work that needs the running game, such as in-game tests, visual tuning and balance, is specified as scripted tests or tunable JSON values with explicit pass criteria.

## Morrowind reference

Morrowind's spellmaker comes down to 24 rules. The plan keeps 15 exactly, adapts 6 to Skyrim, and turns 3 into settings; each row of the checklist doubles as a parity test.

### Formulas

Each effect's cost, where B is its base cost, M its magnitude, D its duration in seconds and A its area in feet (magnitude and duration count as 1 when an effect has none):

```latex
C_i = \frac{B_i\left[(M_{min}+M_{max})(D+1)+\max(1,A)\right]}{40}
```

The spell's cost adds effects in list order, and every Target effect multiplies the running total by 1.5:

```latex
y_i = \left(y_{i-1}+\max(1,C_i)\right)\times\begin{cases}1.5 & \text{effect } i \text{ on Target}\\ 1 & \text{otherwise}\end{cases}\qquad \text{Cost}=\lfloor y_n \rfloor
```

So order changes the price: Fire Damage 10 pts on Touch followed by Frost Damage 10 pts on Target costs 15 magicka; the reverse order costs 12.

```latex
\text{Price}=\max\left(1,\lfloor 7\,y_n\rfloor\right)
```

UESP says disposition and Mercantile don't change this fee, while OpenMW runs it through barter. The mod defaults to the flat fee.

```latex
\text{Chance}=\left(2S+\frac{W}{5}+\frac{L}{10}-\text{Cost}-\text{Sound}\right)\times\left(0.75+0.5\,\frac{F}{F_{max}}\right)
```

S is the skill of the spell's school: the school of whichever effect is hardest to cast, meaning the lowest 2 × skill − x, where x = B\[(Mmin + Mmax) × max(1, D) + A\] / 40, × 1.5 on Target (no +1 on duration and no 1-ft minimum area here). W is Willpower, L is Luck, F is Fatigue. The menu preview assumes half fatigue, a multiplier of 1.0.

Worked example: Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target costs 5 × (15 × 4 + 10) / 40 = 8.75, then × 1.5 = 13 magicka and 91 gold. With Destruction 30, Willpower 40 and Luck 40 the menu shows 59%; cast at full fatigue, the chance is 73%.

### Parity checklist

| # | Morrowind rule | Skyrim implementation | Fidelity |
| --- | --- | --- | --- |
| 1 | Spellmaking is a paid service sold by spellmaker NPCs (Mages Guild, Temple, Imperial Cult, the Great Houses and others) | 15 NPC spellmakers sell it through a dialogue topic; two altars add a self-service version | Adapted |
| 2 | An NPC can refuse service (faction rank, disposition) | Refusal conditions: College membership, a bounty in the hold, relationship rank, stage-4 vampirism; each spellmaker has a refusal line | Adapted |
| 3 | Only effects on normal spells you know count (not powers, abilities, diseases or curses), and only effects flagged for spellmaking | Same rule over the Skyrim spell list; Effect Discovery maps vanilla, DLC and modded spells to catalog effects | Exact |
| 4 | The known-effects list merges duplicates (one Fortify Attribute spell unlocks every attribute) and sorts by name | Same merge and sort; the list can also group by school | Exact |
| 5 | At most 8 effects: *You can only add eight effects to a spell.* | Same limit and message | Exact |
| 6 | No effect twice: *This effect has already been added.* Attribute and skill effects may repeat on a different attribute or skill | Same | Exact |
| 7 | Attribute and skill effects open a picker first | Same picker; attributes go through the translation layer, skills are Skyrim's 18 (Morrowind's 27 as an option) | Adapted |
| 8 | The range button cycles Self → Touch → Target, skipping ranges the effect can't use | Same button and order; Touch and Target become projectiles (see Casting behavior) | Exact |
| 9 | Min and max magnitude run 1–100; max can't drop below min; raising min pulls max up | Same; the 100 cap is a setting | Exact |
| 10 | Duration runs 1–1440 s and is hidden for effects without duration | Same | Exact |
| 11 | Area runs 0–50 ft, is hidden on Self, and resets to 0 when you switch to Self | Same | Exact |
| 12 | Clicking a slider track steps magnitude 10, duration 20, area 5; arrows step 1 | Same with the mouse; gamepad triggers take the big step, the D-pad the small one | Exact |
| 13 | A new effect starts at 1 to 1 pts, 1 sec, 0 ft | Same; the starting range follows OpenMW (Touch for three-range effects, Target for Touch/Target ones), to be confirmed against the original game in M0 | Exact |
| 14 | Effect lines read *Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target*, with pt/pts, %, ft and Level units | Same format, localized | Exact |
| 15 | The window shows spell cost, spell chance and price, updating live | Same readouts; chance shows when the casting-failure module is on, otherwise the spell's Skyrim rank shows | Adapted |
| 16 | Cost formula above, with Target ×1.5 applied to the running total | Exact in Classic mode; the default Skyrim-balanced mode prices each effect on Skyrim's curve (see Cost, price & casting chance) | Setting |
| 17 | Price is 7 × cost, no haggling | A flat, non-negotiable fee by default (7 × cost in Classic, 3 × in Skyrim-balanced); Skyrim haggling is a setting | Setting |
| 18 | Buy checks run in order: no effects, no name, zero cost, not enough gold, each with its own message | Same order, same four messages | Exact |
| 19 | On purchase the gold joins the spellmaker's barter gold, the Mysticism hit sound plays, the spell is added and the window closes | Gold goes to the spellmaker's merchant chest or the altar's owner; magic flash plus the spell-learned sound; the window closes (staying open is a setting) | Adapted |
| 20 | Cost is fixed at purchase; custom spells are not auto-calculated | Cost is stored with the spell and applied as a cost override; a Rebalance button recomputes on request | Exact |
| 21 | Magnitude rolls between min and max: once for one-time effects, effectively averaged for per-second effects | Same: rolled per target on landing; per-second effects use the average | Exact |
| 22 | Damage and restore effects tick every second for the whole duration | Same; a 1-second duration becomes one instant hit | Adapted |
| 23 | Casting can fail, using the hardest effect's school | Optional casting-failure module with the same formula; Skyrim stamina stands in for fatigue | Setting |
| 24 | Shift-click deletes a spell after a confirmation; powers, racial and birthsign spells can't be deleted | Magic-menu hotkey with the same confirmation; any non-inherent spell can go, as in Morrowind, and quest spells are protected too (custom-only is a setting) | Exact |

The original has no altar, no concentration spells and no way to edit a spell once bought; those arrive as clearly marked extensions.

### Messages

| GMST | Shown when | Text |
| --- | --- | --- |
| sNotifyMessage28 | Adding a ninth effect | You can only add eight effects to a spell. |
| sOnetypeEffectMessage | Adding a duplicate effect | This effect has already been added. |
| sNotifyMessage30 | Buying with no effects | You have to add at least one effect to a spell. |
| sNotifyMessage10 | Buying with no name | You have to name the spell before buying it. |
| sEnchantmentMenu8 | Buying at zero cost | You cannot buy a spell that has a zero point cost. |
| sNotifyMessage18 | Too little gold | You don't have enough gold to buy this spell. |
| sQuestionDeleteSpell | Deleting a spell | Are you sure you wish to delete %s? |
| sDeleteSpellError | Deleting an inherent spell | You cannot delete this item from the Magic Menu |

Sources: [UESP: Spellmaking](https://en.uesp.net/wiki/Morrowind:Spellmaking) · [OpenMW spellcreationdialog.cpp](https://github.com/OpenMW/openmw/blob/master/apps/openmw/mwgui/spellcreationdialog.cpp) · [OpenMW spellutil.cpp](https://github.com/OpenMW/openmw/blob/master/apps/openmw/mwmechanics/spellutil.cpp) · [MWSE game settings](https://mwse.github.io/MWSE/references/gmst/) · [Fandom: Spellmaking (Morrowind)](<https://elderscrolls.fandom.com/wiki/Spellmaking_(Morrowind)>)

## Player experience

The altars work from the first visit. After that the altar is a routine stop, like the enchanting table, and Morrowind's movement spells change how Skyrim gets explored.

1. **Finding the altars.** Altars stand in the College's Arcanaeum and in Neloth's tower at Tel Mithryn from the start, each beside a lore book explaining how the lost art was rediscovered.
2. **Learning effects.** As in Morrowind, the altar only offers effects from spells the player already knows. Spellmakers now also sell Morrowind spell tomes (Levitate, Mark, Recall, Ondusi's Open Door and more), and others turn up as dungeon loot.
3. **The first spell.** Activating the altar plays the enchanting-table approach and opens the menu. The player names the spell, adds Fire Damage and Shock Damage, flips both to Target, sets 10 to 20 pts, and watches cost and price update. On Create, the gold is taken, the altar flares, and *Spell learned* appears.
4. **Casting it.** The spell sits in the Destruction list at its computed rank. Each hit rolls between 10 and 20; Augmented Flames and Augmented Shock apply because the effects carry the vanilla keywords; dual casting works.
5. **Mixed ranges.** A spell with Fortify Health on Self and Frost Damage on Target buffs the caster and launches the bolt in one cast, exactly as in Morrowind.
6. **Paying a spellmaker.** Away from the altars, 15 NPCs (court wizards, the College masters, Falion, Neloth, Talvas and Elder Othreloth) sell the same service for a fee. Some refuse: Farengar won't serve anyone wanted in Whiterun.
7. **Living with it.** Old spells are deleted from the Magic menu with one key, and any custom spell can be loaded as the starting point for a new version. Hotkeys and favorites survive reloads.

End-to-end scenarios the finished mod must support; each becomes an in-game test script:

- Levitating up the Winterhold cliffs to the College instead of crossing the bridge.
- Setting a Mark in Raven Rock and Recalling out of Blackreach.
- Casting Ondusi's Open Door on an Expert lock, and earning a bounty when a guard sees it.
- Leaping off a mountainside with a Slowfall spell and landing unhurt.
- Divine Intervention out of a losing fight to the nearest temple.

## Spellmaking menu

The menu is a Scaleform (SWF) crafting menu registered by the SKSE plugin. It keeps Morrowind's layout, with the name on top, known effects beside the spell's effects, and cost, chance and price below, drawn in the visual language of Skyrim's enchanting table.

```text
+--------------------------------------------------------------------------+
| Name  [ Name your spell                               ]    Enter creates |
+----------------------------+---------------------------------------------+
| Effects Known  press / to  | Spell Effects                          2/8  |
|                  search    |  [ Fire Damage 10 to 20 pts on Target     ] |
| [All][A][C][D][I][R]       |  [ Fortify Health 20 pts for 30 secs on   ] |
|   Fire Damage              |  [   Self                                 ] |
|   Fortify Health           |  Shift+Up/Down reorders (order matters in   |
|   Frost Damage             |  Classic mode)                              |
| > Levitate   (focused)     +---------------------------------------------+
|   Mark                     | Item card: Levitate                         |
|   Restore Health           | Alteration . base cost 3 . Self/Touch/Target|
|   Shock Damage             | In Skyrim: jump rises, sneak descends       |
+----------------------------+---------------------------------------------+
| Magicka 42   Price 297 gold   (Classic cost model)  [Rank]  Load Create Exit |
+--------------------------------------------------------------------------+
```
Wireframe of the main view; numbers are the Classic worked example.

Focusing an effect on the left fills the item card; the right list holds the spell in Morrowind's line format; the readouts update on every change.

### Layout

- **Name field** (top): text entry; pressing Enter tries to create, as in Morrowind. Gamepad opens the on-screen keyboard. Empty on open, capped at 40 characters so names fit the Magic menu.
- **Effects Known** (left): every effect the player knows, alphabetical as in Morrowind, with school tabs (All, Alteration, Conjuration, Destruction, Illusion, Restoration) and a search box (press / or click it) that ignores case. Rows that can't be added right now (limit reached, duplicate) are dimmed.
- **Item card** (bottom centre, SkyUI style): the focused effect's description, Morrowind base cost, allowed ranges, magnitude unit, and a one-line *In Skyrim* note (Levitate: jump rises, sneak descends).
- **Spell Effects** (right): a *3/8* counter and one row per effect in Morrowind's line format. Rows can be reordered, and in Classic cost mode the cost changes as they move, because order matters there.
- **Readout bar** (bottom): Magicka Cost, Rank (Novice to Master badge in the school's colour), Spell Chance (casting-failure module only), Price, Your Gold.
- **Buttons:** Create (labelled Buy at an NPC spellmaker), Load (start from an existing custom spell), Clear, Exit.

### Effect editor

A modal panel over the right pane, mirroring Morrowind's dialog:

- Header: icon, effect name (with the attribute or skill, such as *Fortify Strength*) and school.
- Range button: cycles Self, Touch, Target through the allowed ranges only; choosing Self zeroes and hides Area.
- Magnitude: a min slider and a *to* max slider, 1 to 100; max never drops below min, and raising min pulls max up.
- Duration: 1 to 1440 seconds, hidden for effects without duration. Area: 0 to 50 ft, hidden on Self.
- A live preview of the finished line and this effect's share of the cost.
- OK, Cancel (a new effect is dropped, an edited one reverts) and Delete (editing only).
- Attribute and skill effects show a picker first: 8 attributes, and Skyrim's 18 skills or Morrowind's 27.

### Controls

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Move through a list | Mouse wheel, Up/Down | D-pad up/down, left stick |
| Switch pane | Left/Right | D-pad left/right |
| Switch school tab | A/D | LB/RB (Effects Known) |
| Reorder an effect | Shift+Up/Down | LB/RB (Spell Effects) |
| Add or edit an effect | Click, E, Enter | A |
| Remove an effect | Delete, or Delete in the editor | X in the editor |
| Cycle range (editor) | F, or click Range | Y |
| Adjust a slider (editor) | Drag; Left/Right = 1; Shift+Left/Right = big step | D-pad = 1; LT/RT = big step |
| Rename the spell | Click the name, T | Y |
| Create or buy | R, or the Create button | X |
| Show cost math | F1 | Left stick click |
| Back or exit | Tab, Esc | B |
| Search effects | / | Right stick click |

### Feedback

- Navigation and adding effects use the vanilla enchanting UI sounds. Errors show a message box with Morrowind's text and the UI error sound.
- Create plays a magic flash and the spell-learned sound; at an altar, the altar flares in the spell's school colour.
- The cost counts up or down as values change; Price turns red when the player can't afford it. Create still responds, so Morrowind's message appears, just as the original.
- *Show cost math* (off by default) breaks the cost into per-effect parts and shows each Target multiplier, which makes the order quirk visible.

### Messages beyond Morrowind

| When | Text |
| --- | --- |
| No free spell or sub-spell slot | Your spellbook is full. Delete a custom spell first. |
| A non-member uses the College altar | Only members of the College may use this altar. |
| College master refuses a non-member | Spoken refusal, then: This spellmaker only serves members of the College. |
| Name matches a known spell | You already know a spell called %s. (a notice, not a block) |
| Hidden riders exceed the engine's per-spell limit | This combination is too complex to inscribe. Remove an effect. |

### Build notes

- One ActionScript 2 movie, `LostArt_Spellmaking.swf`, patterned on SkyUI's open-source list, button and input classes, registered by the DLL as a menu that pauses the game and shows the cursor.
- C++ pushes data as GFx values and receives callbacks through registered functions; every form edit is queued to the main thread.
- Lists are virtualized; the menu must open in under 150 ms with 200 known effects.
- All strings live in `Interface/Translations/LostArt_<LANGUAGE>.txt` (UTF-16 LE, `$LA_` keys) and use Skyrim's font config.
- Layout is checked at 720p, 1080p, 1440p, 4K and 21:9.

## Effect catalog: Alteration, Conjuration, Destruction

All 119 effects Morrowind allows in spellmaking are covered: 65 run on Skyrim's own effect types, 44 need custom logic in the SKSE plugin, and 10 use a Skyrim stand-in for a creature or weapon Skyrim lacks. This part lists the 64 Alteration, Conjuration and Destruction effects.

The 21 effects Morrowind never offered in spellmaking stay out: 13 return in the optional Extended set (see part 2), while Corprus, Vampirism, Sun Damage, Stunted Magicka, Remove Curse, Cure Corprus Disease and the Blight and Corprus weaknesses have no place in Skyrim. Native effects still pass through the compiler, and a few need small plugin touches, such as Burden's slow on NPCs and Light following a target.

Base is Morrowind's base cost. M is magnitude, 1:1 with Skyrim points for damage, healing and armor; level-based effects read M as a level cap. Native means a Skyrim effect archetype or actor value (data only); Custom means logic in the SKSE plugin. Riders are hidden vanilla effects appended so perks that work through spell-embedded effects still fire (see Casting behavior).

### Alteration (14)

| Effect | Base | Ranges | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- |
| Burden | 1 | Self/Touch/Target | Player: −M carry weight. NPCs, who don't track encumbrance: −M/2 % speed, capped at 75% | Native |
| Feather | 1 | Self/Touch/Target | +M carry weight (vanilla Fortify Carry Weight archetype) | Native |
| Fire Shield | 3 | Self/Touch/Target | +M% fire resistance; melee attackers who land a hit take 0.1 × M fire damage, reduced by a save roll and their resistance (Morrowind's formula); flame shell shader. No armor bonus, matching Morrowind's actual behavior | Custom |
| Frost Shield | 3 | Self/Touch/Target | Same, with frost | Custom |
| Jump | 3 | Self/Touch/Target | +3% jump height per point through the JumpingBonus value, which vanilla ignores (the plugin implements it and defers to po3's Tweaks when installed); fall height counts M ft shorter | Custom |
| Levitate | 3 | Self/Touch/Target | Flight controller: gravity off, jump rises, sneak descends, air speed scales with M; normal falling resumes when it ends (grace Slowfall is a setting). Levitated NPCs float up and hover | Custom |
| Lightning Shield | 3 | Self/Touch/Target | Same as Fire Shield, with shock | Custom |
| Lock | 2 | Touch/Target | Raises a door or container's lock to the Skyrim tier at or below M (1, 25, 50, 75, 100); never touches Requires Key locks | Custom |
| Open | 6 | Touch/Target | Unlocks a door or container whose lock level is ≤ M; both games use a 1–100 scale. Owned locks count as lockpicking for crime | Custom |
| Shield | 2 | Self/Touch/Target | +M armor rating, like Oakflesh; carries MagicArmorSpell so Mage Armor applies | Native |
| Slowfall | 3 | Self/Touch/Target | Fall speed × (1 − M/200); any active Slowfall cancels fall damage, as in Morrowind | Custom |
| Swift Swim | 2 | Self/Touch/Target | +M% speed while swimming only, with the carry-weight nudge Skyrim needs before speed changes apply | Custom |
| Water Breathing | 3 | Self/Touch/Target | WaterBreathing actor value (vanilla archetype) | Native |
| Water Walking | 3 | Self/Touch/Target | WaterWalking actor value, which the engine honors (vanilla gives it to flame atronachs); invisible-platform fallback if the spike fails | Native |

### Conjuration (29)

| Effect | Base | Ranges | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- |
| Bound Battle Axe | 2 | Self | Vanilla Bound Battleaxe (Bound Weapon archetype) with Mystic Binding and Soul Stealer riders | Native |
| Bound Boots | 2 | Self | Weightless bound Daedric boots equipped when the effect starts and removed when it ends; armor has no bound archetype, so the plugin manages the item | Custom |
| Bound Cuirass | 2 | Self | Same, cuirass | Custom |
| Bound Dagger | 2 | Self | New bound Daedric dagger on the Bound Weapon archetype, with a Mystic Binding variant | Native |
| Bound Gloves | 2 | Self | Same as Bound Boots, gauntlets | Custom |
| Bound Helm | 2 | Self | Same as Bound Boots, helmet | Custom |
| Bound Longbow | 2 | Self | Vanilla Bound Bow with bound arrows | Native |
| Bound Longsword | 2 | Self | Vanilla Bound Sword | Native |
| Bound Mace | 2 | Self | New bound Daedric mace | Native |
| Bound Shield | 2 | Self | Same as Bound Boots, shield | Custom |
| Bound Spear | 2 | Self | Skyrim has no spears; a bound Daedric greatsword stands in | Stand-in |
| Command Creature | 15 | Touch/Target | A creature of level ≤ M becomes a temporary follower (teammate plus follow package), then returns to its old behavior | Custom |
| Command Humanoid | 15 | Touch/Target | Same for NPCs; skips actors held by an active quest alias | Custom |
| Summon Ancestral Ghost | 7 | Self | New Dunmer ancestor ghost built from vanilla ghost assets | Native |
| Summon Bonelord | 25 | Self | Wrathman (Dawnguard) | Stand-in |
| Summon Bonewalker | 13 | Self | Boneman (Dawnguard) | Stand-in |
| Summon Clannfear | 22 | Self | Seeker (Dragonborn) | Stand-in |
| Summon Daedroth | 32 | Self | Dremora Markynaz | Stand-in |
| Summon Dremora | 28 | Self | Dremora Kynval, matching Morrowind's mid-tier Dremora | Native |
| Summon Flame Atronach | 23 | Self | Vanilla Flame Atronach; MagicSummonFire for Twin Souls, plus the Elemental Potency rider | Native |
| Summon Frost Atronach | 27 | Self | Vanilla Frost Atronach, with the same keyword and rider | Native |
| Summon Golden Saint | 55 | Self | Dremora Valkynaz | Stand-in |
| Summon Greater Bonewalker | 15 | Self | Mistman (Dawnguard) | Stand-in |
| Summon Hunger | 29 | Self | Lurker (Dragonborn), scaled down | Stand-in |
| Summon Scamp | 12 | Self | Familiar | Stand-in |
| Summon Skeletal Minion | 13 | Self | Skeleton, tagged MagicSummonUndead | Native |
| Summon Storm Atronach | 38 | Self | Vanilla Storm Atronach, with the same keyword and rider | Native |
| Summon Winged Twilight | 52 | Self | Wispmother | Stand-in |
| Turn Undead | 0.2 | Touch/Target | Vanilla Turn Undead archetype with a level cap of M; filed under Restoration, its Skyrim home | Native |

The menu shows a stand-in under its own name (Summon Seeker) and credits the Morrowind original in the item card. Stand-ins live in a JSON file, so a creature pack with original models can swap them back. Summons follow Skyrim's limit (one, two with Twin Souls); Morrowind's one-per-type rule is a setting. The +10 skill bonuses Morrowind built into bound items can ride along as small enchantments (setting).

### Destruction (21)

| Effect | Base | Ranges | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- |
| Damage Attribute | 8 | Self/Touch/Target | Lowers the translated stats (see Attributes & skills) by M per second until restored | Custom |
| Damage Fatigue | 4 | Self/Touch/Target | −M stamina per second, no recovery | Native |
| Damage Health | 8 | Self/Touch/Target | −M health per second, not resisted by elemental resistances | Native |
| Damage Magicka | 8 | Self/Touch/Target | −M magicka per second | Native |
| Disintegrate Armor | 6 | Self/Touch/Target | Skyrim has no durability, so worn pieces lose M points per second from a hidden 100-point condition pool, shield first as in Morrowind. Armor rating falls with condition; a piece at 0 is unequipped until repaired at a workbench | Custom |
| Disintegrate Weapon | 6 | Self/Touch/Target | The same pool for the drawn weapon: damage falls with condition, and at 0 the target is disarmed. Repaired at a grindstone | Custom |
| Drain Attribute | 1 | Self/Touch/Target | Lowers the translated stats by M for the duration | Custom |
| Drain Fatigue | 2 | Self/Touch/Target | −M stamina for the duration, returned when it ends | Native |
| Drain Health | 4 | Self/Touch/Target | −M health for the duration, returned if the target survives | Native |
| Drain Magicka | 4 | Self/Touch/Target | −M magicka for the duration | Native |
| Drain Skill | 1 | Self/Touch/Target | −M to a Skyrim skill for the duration | Native |
| Fire Damage | 5 | Self/Touch/Target | M fire damage per second, resisted by fire resistance; MagicDamageFire for Augmented Flames, plus Intense Flames and Impact riders | Native |
| Frost Damage | 5 | Self/Touch/Target | M frost damage per second; Skyrim's matching stamina damage and slow come along (setting); Augmented Frost and Deep Freeze apply | Native |
| Poison | 9 | Self/Touch/Target | M poison damage per second, resisted by poison resistance | Native |
| Shock Damage | 7 | Self/Touch/Target | M shock damage per second; Skyrim's half-magnitude magicka damage comes along (setting); Augmented Shock and Disintegrate apply | Native |
| Weakness to Common Disease | 2 | Self/Touch/Target | −M% disease resistance | Native |
| Weakness to Fire | 2 | Self/Touch/Target | −M% fire resistance | Native |
| Weakness to Frost | 2 | Self/Touch/Target | −M% frost resistance | Native |
| Weakness to Magicka | 2 | Self/Touch/Target | −M% magic resistance | Native |
| Weakness to Poison | 2 | Self/Touch/Target | −M% poison resistance | Native |
| Weakness to Shock | 2 | Self/Touch/Target | −M% shock resistance | Native |

Every Alteration, Conjuration and Destruction effect keeps its Morrowind school except Turn Undead (Restoration); part 2 re-homes Light, Paralyze and every Mysticism effect. Source for base costs, ranges and flags: [UESP: Spell Effects](https://en.uesp.net/wiki/Morrowind:Spell_Effects) and [OpenMW loadmgef.cpp](https://github.com/OpenMW/openmw/blob/master/components/esm3/loadmgef.cpp).

## Effect catalog: Illusion, Mysticism, Restoration

The other 55 effects split 31 native and 24 custom. Skyrim has no Mysticism, so each Mysticism effect moves to the school Skyrim gave its closest counterpart; Light and Paralyze move from Illusion to Alteration for the same reason. Schools are data, so a Mysticism-skill mod could claim them back.

### Illusion (18)

| Effect | Base | Ranges | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- |
| Blind | 1 | Self/Touch/Target | The subject's physical attacks miss M% of the time (the plugin's miss system, since Skyrim has no hit chance), and it spots sneaking players less well. Morrowind's inverted-Blind bug is not reproduced | Custom |
| Calm Creature | 1 | Touch/Target | Vanilla Calm archetype, creatures up to level M | Native |
| Calm Humanoid | 1 | Touch/Target | Vanilla Calm archetype, NPCs up to level M | Native |
| Chameleon | 1 | Self/Touch/Target | M% stealth bonus on detection rolls that attacking doesn't break; translucency shader scaled by M; 0.2 × M% evasion through the miss system | Custom |
| Charm | 5 | Touch/Target | The target's relationship rank toward the player rises one step per 25 M for the duration, and its prices improve by M/2 % | Custom |
| Demoralize Creature | 1 | Touch/Target | Vanilla Fear archetype, creatures up to level M | Native |
| Demoralize Humanoid | 1 | Touch/Target | Vanilla Fear archetype, NPCs up to level M | Native |
| Frenzy Creature | 1 | Touch/Target | Vanilla Frenzy archetype, creatures up to level M | Native |
| Frenzy Humanoid | 1 | Touch/Target | Vanilla Frenzy archetype, NPCs up to level M | Native |
| Invisibility | 20 | Self/Touch/Target | Vanilla Invisibility; breaks on attacking, casting or activating, as in Morrowind | Native |
| Light | 0.2 | Self/Touch/Target | Candlelight-style light on the subject, radius tier from M; on Target it follows the struck actor. Filed under Alteration | Native |
| Night Eye | 0.2 | Self/Touch/Target | Night Eye image-space effect in three strength tiers by M | Native |
| Paralyze | 40 | Self/Touch/Target | Vanilla Paralysis; filed under Alteration | Native |
| Rally Creature | 0.2 | Touch/Target | Vanilla Courage archetype, creatures up to level M | Native |
| Rally Humanoid | 0.2 | Touch/Target | Vanilla Courage archetype, NPCs up to level M | Native |
| Sanctuary | 1 | Self/Touch/Target | Physical attacks against the subject miss min(M, 75)% of the time (miss system); spells unaffected | Custom |
| Silence | 40 | Self/Touch/Target | The subject can't cast spells; powers and shouts still work, as powers did in Morrowind. NPC casters fall back to weapons | Custom |
| Sound | 3 | Self/Touch/Target | The subject's spells fizzle M% of the time; with the failure module on, M comes off its chance instead | Custom |

### Mysticism (15)

| Effect | Base | Ranges | Skyrim school | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- | --- |
| Absorb Attribute | 2 | Touch/Target | Destruction | Drains the translated stats from the target and fortifies the caster by the same amount for the duration | Custom |
| Absorb Fatigue | 4 | Touch/Target | Destruction | Vanilla Absorb archetype on stamina | Native |
| Absorb Health | 8 | Touch/Target | Destruction | Vanilla Absorb archetype on health | Native |
| Almsivi Intervention | 150 | Self | Conjuration | Teleports to Raven Rock Temple, the last Tribunal temple | Custom |
| Detect Animal | 0.75 | Self | Alteration | Detect Life archetype limited to creatures and animals within M ft | Native |
| Detect Enchantment | 1 | Self | Alteration | Highlights enchanted items on the ground, in containers and worn by actors within M ft | Custom |
| Detect Key | 1 | Self | Alteration | Highlights keys on the ground, in containers and carried by actors within M ft | Custom |
| Dispel | 5 | Self/Touch/Target | Restoration | Each active spell on the subject has an M% chance to end; abilities, diseases, potions and enchantments are untouched | Custom |
| Divine Intervention | 150 | Self | Conjuration | Teleports to the nearest temple of the Divines in the current worldspace: Solitude, Whiterun, Windhelm, Riften or Markarth, and Fort Frostmoth on Solstheim | Custom |
| Mark | 350 | Self | Conjuration | Stores the caster's position; one mark, as in Morrowind (more marks is a setting) | Custom |
| Recall | 350 | Self | Conjuration | Teleports to the mark, bringing followers; blocked wherever a quest has disabled fast travel | Custom |
| Reflect | 10 | Self/Touch/Target | Alteration | M% chance to send an incoming spell back at its caster, beneficial ones included, but never Lock, Open, Calm, Frenzy, Demoralize, Rally, Soultrap or Turn Undead, as in Morrowind | Custom |
| Soultrap | 2 | Touch/Target | Conjuration | Vanilla Soul Trap archetype | Native |
| Spell Absorption | 10 | Self/Touch/Target | Alteration | +M% to the AbsorbChance actor value, the mechanic behind Skyrim's Atronach perk | Native |
| Telekinesis | 1 | Self | Alteration | Activation reach +M ft for the duration: open doors, loot containers and pick up items from afar. Skyrim's own Telekinesis stays a separate effect | Custom |

### Restoration (22)

| Effect | Base | Ranges | Skyrim implementation | Tier |
| --- | --- | --- | --- | --- |
| Cure Blight Disease | 2000 | Self/Touch/Target | Cures Solstheim's ash-borne diseases (Black-Heart Blight, Droops) and anything other mods tag as blight | Custom |
| Cure Common Disease | 300 | Self/Touch/Target | Vanilla Cure Disease archetype | Native |
| Cure Paralyzation | 100 | Self/Touch/Target | Ends active effects tagged MagicParalysis | Custom |
| Cure Poison | 100 | Self/Touch/Target | Ends active poison effects; health already lost stays lost | Custom |
| Fortify Attack | 1 | Self/Touch/Target | +M% weapon damage, and M points off the miss chance from Blind and Sanctuary | Custom |
| Fortify Attribute | 1 | Self/Touch/Target | Raises the translated stats by M for the duration | Custom |
| Fortify Fatigue | 0.5 | Self/Touch/Target | +M stamina | Native |
| Fortify Health | 1 | Self/Touch/Target | +M health | Native |
| Fortify Magicka | 1 | Self/Touch/Target | +M magicka | Native |
| Fortify Skill | 1 | Self/Touch/Target | +M to a Skyrim skill's level | Native |
| Resist Blight Disease | 5 | Self/Touch/Target | +M% disease resistance (Skyrim has a single disease resistance) | Native |
| Resist Common Disease | 2 | Self/Touch/Target | +M% disease resistance | Native |
| Resist Corprus Disease | 5 | Self/Touch/Target | M% chance to shrug off Sanguinare Vampiris, Skyrim's disease that turns you into a monster | Custom |
| Resist Fire | 2 | Self/Touch/Target | +M% fire resistance | Native |
| Resist Frost | 2 | Self/Touch/Target | +M% frost resistance | Native |
| Resist Magicka | 2 | Self/Touch/Target | +M% magic resistance | Native |
| Resist Paralysis | 0.2 | Self/Touch/Target | M% chance to ignore paralysis (Skyrim has no paralysis resistance value) | Custom |
| Resist Poison | 2 | Self/Touch/Target | +M% poison resistance | Native |
| Resist Shock | 2 | Self/Touch/Target | +M% shock resistance | Native |
| Restore Attribute | 1 | Self/Touch/Target | Repairs damage to the translated stats by M per second | Custom |
| Restore Fatigue | 1 | Self/Touch/Target | +M stamina per second | Native |
| Restore Health | 5 | Self/Touch/Target | +M health per second; tagged MagicRestoreHealth so Regeneration applies | Native |

### Beyond Morrowind's list

| Set | What it adds | Default |
| --- | --- | --- |
| Skyrim-only effects | Effects from Skyrim spells you know that Morrowind never had and that work as fire-and-forget: Muffle, Transmute, Detect Life and Detect Dead as timed effects, the reanimation family, Banish and Command Daedra, elemental cloaks, Magelight, and Dragonborn's Ash Spawn summon (Conjure Seeker unlocks Summon Clannfear, shown as Summon Seeker) | On |
| Concentration, runes and walls | Flames- and Healing-style streams, wards, runes and wall spells; they need concentration and target-location variants | v1.x |
| Extended Morrowind | Morrowind effects no obtainable spell carried, or that the engine barred: Restore and Absorb Magicka, Fortify Maximum Magicka (as magicka regeneration), Resist and Weakness to Normal Weapons (versus unenchanted weapons), Summon Centurion Sphere (a Dwarven Sphere), Summon Fabricant (a Dwarven Spider), Call Wolf, Call Bear, Summon Bonewolf, and Damage, Restore and Absorb Skill; taught by rare tomes | Off |

### Effect Discovery

The menu offers exactly the effects on spells the player knows, recomputed each time it opens (a few milliseconds for hundreds of spells):

1. Only spells of type Spell count; abilities, powers, lesser powers, diseases and shouts don't, matching Morrowind's rule.
2. Known vanilla and DLC magic effects map straight to catalog entries through a lookup table (Firebolt's effect is Fire Damage, Oakflesh's is Shield, Candlelight's is Light).
3. Unknown effects from other mods go through JSON rules that match archetype, actor value, resistance, keywords and the hostile flag; a hostile health modifier resisted by fire resistance is Fire Damage. First match wins.
4. Unrecognized fire-and-forget effects can pass through at their own delivery, with magnitude, duration and area editable (a setting, off for scripted effects that ignore magnitude); concentration and target-location effects stay hidden until v1.x.

## Attributes & skills

Skyrim has none of Morrowind's eight attributes, and 18 skills to Morrowind's 27, so the five attribute effects run through a translation layer, while skill effects target Skyrim's skills directly (Morrowind's 27 names are an option).

### Attribute translation

Each point of magnitude on an attribute effect (Fortify, Drain, Damage, Restore, Absorb) moves these Skyrim stats. Values are defaults in JSON, tuned during balance calibration.

| Attribute | What it drove in Morrowind | Per point in Skyrim |
| --- | --- | --- |
| Strength | Carry weight (5 per point), melee damage, fatigue | +3 carry weight, +0.5% melee damage |
| Intelligence | Maximum magicka | +2 maximum magicka |
| Willpower | Casting chance, resisting magic, fatigue | +1% magicka regeneration, +0.25% magic resistance |
| Agility | Hit chance, evasion, block, sneak | +0.5% attack speed, +0.2% evasion (miss system) |
| Speed | Movement speed | +1% movement speed |
| Endurance | Health gains, fatigue | +1 maximum health, +1% stamina regeneration |
| Personality | Disposition, prices | +0.5 Speech, +0.5% better prices |
| Luck | A little of everything | +0.5% pickpocket chance, +0.2% better prices, +0.2% evasion |

- **Compilation:** an attribute effect becomes one visible magic effect for its first stat plus at most one hidden rider for the second; Luck's spread is a single effect read by the plugin's hooks. Percentages use Skyrim's multiplier values (speed, weapon speed, magicka and stamina regeneration).
- **Drain** returns everything when it ends. **Absorb** is a Drain on the target plus a matching Fortify on the caster.
- **Damage** persists in a per-actor ledger saved with the game until a Restore Attribute effect repairs it or the player prays at a shrine, which in Morrowind also restored attributes. Sleeping doesn't heal it, as in Morrowind.
- **Profiles:** Default, Light (half values) or Off (attribute effects disappear from the menu).

### Skills

By default the picker lists Skyrim's 18 skills, and Fortify and Drain Skill change the skill level the Skills menu shows, which is what Morrowind did. A setting routes them to Skyrim's percentage modifiers instead, the way vanilla Fortify enchantments work.

With Morrowind skill names switched on, the picker shows all 27 and maps them like this:

| Morrowind skill | Skyrim target |
| --- | --- |
| Acrobatics | Jump height and softer landings |
| Alchemy | Alchemy |
| Alteration | Alteration |
| Armorer | Smithing |
| Athletics | Running and swimming speed |
| Axe | One-Handed and Two-Handed, half each |
| Block | Block |
| Blunt Weapon | One-Handed and Two-Handed, half each |
| Conjuration | Conjuration |
| Destruction | Destruction |
| Enchant | Enchanting |
| Hand-to-Hand | Unarmed damage |
| Heavy Armor | Heavy Armor |
| Illusion | Illusion |
| Light Armor | Light Armor |
| Long Blade | One-Handed and Two-Handed, half each |
| Marksman | Archery |
| Medium Armor | Light Armor and Heavy Armor, half each |
| Mercantile | Speech |
| Mysticism | Alteration and Conjuration, half each |
| Restoration | Restoration |
| Security | Lockpicking |
| Short Blade | One-Handed |
| Sneak | Sneak, plus Pickpocket at half |
| Spear | Two-Handed |
| Speechcraft | Speech |
| Unarmored | Armor rating while no body armor is worn |

Several Morrowind skills land on the same Skyrim skill, so a spell fortifying Long Blade and Axe stacks on One-Handed; that is intended.

## Cost, price & casting chance

Crafted spells default to Skyrim-balanced costs, so they sit comfortably beside vanilla spells; Morrowind's exact formula is one setting away. An unscaled port can't be the default:

| Vanilla spell rebuilt in the spellmaker | Vanilla cost | Morrowind Classic cost | Classic as % of vanilla |
| --- | --- | --- | --- |
| Paralyze | 450 | 34 | 8% |
| Soul Trap | 107 | 9 | 8% |
| Invisibility | 334 | 31 | 9% |
| Bound Sword | 93 | 12 | 13% |
| Incinerate | 298 | 45 | 15% |
| Conjure Dremora Lord | 358 | 86 | 24% |
| Fireball | 133 | 32 | 24% |
| Fast Healing | 73 | 25 | 34% |
| Close Wounds | 126 | 50 | 40% |
| Firebolt | 41 | 18 | 44% |
| Conjure Flame Atronach | 150 | 70 | 47% |
| Lightning Bolt | 51 | 26 | 51% |
| Heal Other | 80 | 56 | 70% |
| Ebonyflesh | 341 | 610 | 179% |
| Stoneflesh | 194 | 366 | 189% |
| Oakflesh | 103 | 244 | 237% |

Vanilla costs: skill 0, no perks (Fandom, Carl's Guides). Morrowind cost: OpenMW's spellmaking formula, same magnitude, duration and area.

Rebuilt at the same magnitude, duration and area, Paralyze costs 34 magicka under Morrowind's formula against 450 in vanilla, while Oakflesh costs 244 against 103. Morrowind charges linearly for duration and little for control effects, so no single multiplier fixes both ends.

### Cost models

| Model | How the cost is computed | Best for |
| --- | --- | --- |
| Skyrim-balanced (default) | Each effect priced on Skyrim's own curve with Skyrim-scale base costs; Morrowind's ranges, min–max and area still shape the number | Normal play: crafted spells cost what equivalent vanilla spells cost |
| Morrowind Classic | Morrowind's formula exactly, including Target ×1.5 on the running total, times a global multiplier | Purists, who should expect cheap control spells and expensive long buffs |
| Engine autocalc | Skyrim's built-in auto-calculation from the maximum magnitude | Fallback if cost overrides misbehave |

### Skyrim-balanced formula

```latex
c_i = B^{sk}_i \times T_i^{\,1.1} \times \left(1 + \frac{A_i}{16}\right), \qquad T_i = \begin{cases}\bar{M}_i \times \max(1, D_i) & \text{ticking effects}\\ \bar{M}_i \times \frac{\max(D_i, 10)}{10} & \text{held effects}\end{cases}, \qquad \bar{M}_i = \max\left(1, \frac{M_{min}+M_{max}}{2}\right)
```

- B^sk is the effect's Skyrim-scale base cost, read live from the vanilla magic effect where one exists, so overhauls that rebalance vanilla costs carry through. The rest are fitted from Morrowind's base costs.
- The 1.1 exponent and the held-effect duration term are Skyrim's own formula. Ticking effects (damage, restore, absorb, poison, disintegrate) are priced on their total over the duration, because Skyrim's duration term would make ten seconds of damage cost the same as one hit. A one-second duration counts as instant.
- The area factor is fitted so Fire Damage 40 pts in 15 ft lands on Fireball's 133; spike S7 checks it against more area spells.
- Target costs 1.5× only where vanilla has no priced Target version of the effect.
- The spell's cost is the floored sum. Skyrim's skill reduction (× (1 − (skill/400)^0.65), about 0.59 at skill 100), half-cost perks, Fortify school enchantments and the 2.8× dual-cast cost then apply as for any spell.

### Price

```latex
\text{Price} = \max\left(1, \lfloor k \times y \rfloor\right)
```

- y is the cost before its final rounding, as Morrowind computes it; k is 7 in Classic (Morrowind's fSpellMakingValueMult) and 3 in Skyrim-balanced, tuned so rebuilding a vanilla spell costs about what its spell tome does.
- The price is flat by default, as UESP describes Morrowind. The Skyrim haggling setting applies Skyrim's buy-price formula (Speech, Fortify Barter) and then the Modify Buy Prices perk entry point for perks such as Haggling.
- Altars charge the same price as a fee to the altar's owner (the College or Neloth); the Arch-Mage uses the College altar free.

### Rank, perks and experience

- **Rank** (Novice to Master) is the highest rank among the spell's effects. Each effect has a magnitude ladder taken from vanilla spells of that effect: Shield is 40 Novice, 60 Apprentice, 80 Adept, 100 Expert (Oakflesh to Ebonyflesh); Fire Damage is 25 Apprentice, 40 Adept, 60 Expert. Effects without a vanilla ladder use cost thresholds: under 60 Novice, under 130 Apprentice, under 250 Adept, under 500 Expert, then Master.
- **Half-cost perk:** the spell's perk field is set to its school's rank perk (such as DestructionAdept50), so Skyrim's cost-halving perks apply exactly as for vanilla spells.
- **School:** the magic menu files the spell under its costliest effect, Skyrim's rule. Classic mode can credit experience to Morrowind's hardest-effect school instead.
- **Experience** follows Skyrim's casting rule; spike S7 confirms it tracks the cost override, and skill-usage multipliers are tuned if it doesn't.

### Casting chance (optional module)

```latex
\text{Chance} = \left(2S + 15 - D_{mw} - \text{Sound}\right) \times \left(0.75 + 0.5\,\frac{\text{Stamina}}{\text{Stamina}_{max}}\right)
```

- D\_mw is the spell's Morrowind Classic cost, kept as its difficulty whatever the magicka model, so chances stay on Morrowind's scale.
- 15 stands in for Willpower/5 + Luck/10 of an average character (50 and 50); it's a setting.
- S is the skill of the hardest effect's school, Morrowind's rule, using each effect's Skyrim school from the catalog.
- A failed cast spends the magicka, fizzles and grants no experience. The menu shows the chance at half stamina, as Morrowind's preview did.

### Classic worked examples

| Spell | Cost | Price |
| --- | --- | --- |
| Fire Damage 5 to 10 pts for 3 secs in 10 ft on Target | 13 | 91 |
| Fire Damage 10 pts on Touch, then Frost Damage 10 pts on Target | 15 | 107 |
| The same two effects in reverse order | 12 | 89 |
| Fire Damage 10 to 20 pts on Target, then Fortify Health 20 pts for 30 secs on Self | 42 | 297 |
| The same two effects, Fortify Health first | 57 | 405 |
| Levitate 10 pts for 30 secs on Self | 46 | 326 |
| Absorb Health 10 to 20 pts for 5 secs on Touch | 36 | 253 |
| Mark or Recall | 43 | 306 |
| Divine Intervention | 18 | 131 |

Effects without a stated duration last 1 sec. These double as unit-test vectors. In Skyrim-balanced mode, with the fire base cost fitted to Firebolt, a rebuilt Firebolt costs 41 and a rebuilt Fireball 133, matching vanilla; the first example costs 59, and Fire Damage 10 pts for 10 secs on Target costs 188.

## Casting behavior

Every crafted spell is one equippable Skyrim spell. At cast time the SKSE plugin adds what Skyrim lacks: touch range, mixed ranges, min–max rolls and Morrowind-style area.

### Ranges

| Morrowind range | Skyrim delivery | Details |
| --- | --- | --- |
| Self | Self | Applies to the caster on release |
| Touch | Aimed, with a touch projectile | Skyrim's Contact delivery only works for weapons, so touch spells fire an invisible projectile that dies at arm's reach (192 units by default). It can strike doors and containers for Open and Lock |
| Target | Aimed projectile | Projectile, speed, hand art and sounds come from the lead effect. Only the first effect's projectile is drawn, so the compiler lists the costliest Target effect first |

### Mixed-range spells

Morrowind lets one spell mix Self, Touch and Target; a Skyrim spell has a single delivery. The compiler splits the spell:

1. The equipped spell takes the farthest range present (Target, then Touch, then Self) and carries the whole magicka cost.
2. Effects on the other ranges go into linked sub-spells from a shared slot pool, costing nothing themselves.
3. When the equipped spell is released, the plugin casts the linked sub-spells instantly: Self effects on the caster, Touch effects as a touch projectile along the same aim.
4. Failure checks, dual casting and perk scaling are decided once on the equipped spell, and the sub-spells inherit the result.

### Area

- On impact, the plugin applies area effects to every actor within A × 22 game units of the hit point, except the caster. That is Morrowind's own rule (a foot rounds up to 22 units).
- The explosion visuals come in three sizes by area, tinted by school, and deal no damage themselves. Area therefore never depends on how Skyrim's explosion radius interacts with effect area, which is unverified.
- Line of sight to the impact point is required by default; ignoring it is a setting. Area works on Touch as in Morrowind; Self can't have area.

### Magnitude rolls

- One-time effects (fortify, drain, shields, resistances, Charm, Command) roll a whole number between min and max once per target when they land, as Morrowind does.
- Effects that tick for 2 seconds or more (damage, restore, Absorb Health and Fatigue, poison, disintegrate) apply the average each second, which is what Morrowind's per-frame rerolling works out to; a 1-second damage or restore effect is a single hit and rolls once.
- The roll scales the active effect's magnitude before Skyrim's perk multipliers, through a hook on the active effect's perk adjustment, so Augmented Flames and dual casting stack on the rolled value. Resistances apply afterwards, as usual.

### Duration

- A Morrowind duration of 1 is one instant application. From 2 seconds up, ticking effects apply every second and one-time effects hold for the duration.
- Skyrim's duration perks and flags still apply.

### Perks and riders

- Effects carry vanilla keywords (MagicDamageFire, MagicSummonFire, MagicRestoreHealth, MagicArmorSpell and others), so keyword-based perks such as Augmented Flames, Twin Souls and Mage Armor apply unchanged.
- Several vanilla perks work through extra effects embedded in vanilla spells instead: Intense Flames, Deep Freeze, Disintegrate, Impact, Elemental Potency, Mystic Binding and Soul Stealer. The compiler appends the matching vanilla rider effects, hidden, so those perks fire too.
- Riders and attribute bundles count toward Skyrim's practical ceiling of about 15 effects per spell. Overflow moves into a linked sub-spell; if that fails, the menu refuses the combination with a message.

### Stacking, hostility and crime

- Recasting the same spell refreshes it; different custom spells stack, as they did in Morrowind. A spike confirms how Skyrim handles shared magic effects across spells; if buffs from different slots collide, buff effects get per-slot variants.
- The compiler recounts each spell's hostile flag after editing, because the engine caches it. Healing spells don't anger allies, and hostile spells on neutral targets follow Skyrim's assault rules.
- Open on an owned lock is reported like lockpicking.

### NPCs

- Crafted spells are normal spells with correct hostile flags and AI data, so a follower taught one (v1.x) uses it through Skyrim's combat AI.
- Enemy NPCs never receive custom spells, and the casting-failure module exempts NPCs by default.

## Where spells are made

Spells are made at two Altars of Spellmaking, in the College of Winterhold and at Tel Mithryn, or bought from 15 NPC spellmakers. Everything is available from the start; a questline can come later.

### Altars

| Altar | Location | Access | Fee |
| --- | --- | --- | --- |
| College altar | Arcanaeum annex, College of Winterhold | College members | The spell's price, paid to the College; free for the Arch-Mage |
| Telvanni altar | Neloth's upper tower, Tel Mithryn | Anyone | The spell's price, paid to Neloth |

- The plugin places the altars at runtime instead of editing the cells, so interior overhauls don't conflict. If an overhaul occupies the spot, an alternate marker is used.
- Using an altar plays the enchanting-table approach; the menu opens on entering the furniture, and closing it stands the player up.

### NPC spellmakers

| Spellmaker | Where | Refuses when |
| --- | --- | --- |
| Tolfdir | College of Winterhold | The player isn't a College member |
| Faralda | College of Winterhold | The player isn't a College member |
| Drevis Neloren | College of Winterhold | The player isn't a College member |
| Colette Marence | College of Winterhold | The player isn't a College member |
| Phinis Gestor | College of Winterhold | The player isn't a College member |
| Sybille Stentor | Blue Palace, Solitude | The player is wanted in Haafingar |
| Farengar Secret-Fire | Dragonsreach, Whiterun | The player is wanted in Whiterun Hold |
| Wuunferth the Unliving | Palace of the Kings, Windhelm | The player is wanted in Eastmarch |
| Wylandriah | Mistveil Keep, Riften | The player is wanted in the Rift |
| Calcelmo | Understone Keep, Markarth | The player is wanted in the Reach |
| Madena | The White Hall, Dawnstar | The player is wanted in the Pale |
| Falion | His house, Morthal | The player is wanted in Hjaalmarch |
| Neloth | Tel Mithryn, Solstheim | The player hasn't helped him yet (Reluctant Steward); Telvanni don't serve strangers |
| Talvas Fathryon | Tel Mithryn, Solstheim | Same as Neloth |
| Elder Othreloth | Raven Rock Temple, Solstheim | The player hasn't finished his quest, Clean Sweep |

- A new topic, *I'd like to make a spell*, opens the menu in spellmaker mode. It lives in the mod's own quest, so no vanilla dialogue is edited.
- No new voice acting: responses reuse each NPC's existing merchant and trainer lines through shared responses, and truly new lines are subtitled silent lines.
- The gold goes into the spellmaker's merchant chest, or their inventory if they have none, like Morrowind's barter gold.
- Every spellmaker charges the same price, as in Morrowind; per-NPC price modifiers are a setting.

### Later: a questline

No quest ships in v1.0. A future questline can wrap the altars in a story (for example, recovering Third Era spellmaking notes with Urag and Neloth); the altars and spellmakers are built so it can gate them later without changes to the core system.

### How effects are learned

1. **Known spells**, Morrowind's rule, through Effect Discovery.
2. **Morrowind spell tomes**, about 50, named after the originals: Levitate, Mark, Recall, Divine Intervention, Almsivi Intervention, Ondusi's Open Door, Lock, Feather, Burden, Jump, Slowfall, Water Walking, Swift Swim, the elemental shields, Detect Creature, Detect Key, Detect Enchantment, Telekinesis, Dispel, Reflect, Spell Absorption, Sanctuary, Chameleon, Blind, Sound, Silence, Charm, Light, Night-Eye, the Cure, Resist and Weakness families, the attribute and skill families, Disintegrate, Summon Ancestral Ghost, the new bound items, the Command effects, Water Breathing, Poison, Fortify Attack, Summon Skeletal Minion, the Damage, Drain, Absorb, Fortify and Restore families for health, magicka and fatigue, and the stand-in summons no vanilla spell teaches (Daedroth, Golden Saint, Hunger, Winged Twilight).
   - Each tome is an ordinary spell tome: reading it teaches a normal spell, which unlocks its effects, the same mechanism Morrowind used. Every catalog effect has at least one source, a vanilla or DLC spell or a tome; the data files record it and CI fails if any effect has none.
   - Spellmakers sell tomes in their specialty, and the plugin adds tomes to dungeon loot lists at game start without editing records, so loot mods don't conflict.
3. **Sandbox mode** (setting) unlocks every effect at once.

## Spell management

Custom spells behave like any spell in the Magic menu, with Morrowind's delete-on-demand and a few conveniences Morrowind never had.

| Feature | How it works | Version |
| --- | --- | --- |
| Delete a spell | In the Magic menu: Shift+click (Morrowind's gesture) or Delete; on gamepad, click the left stick. Confirmation reads *Are you sure you wish to delete %s?* Powers, racial, standing-stone and quest spells answer *You cannot delete this item from the Magic Menu*. Any non-inherent spell by default, as in Morrowind; custom-only is a setting. The slot is wiped and freed | v1.0 |
| Load as a starting point | Load at an altar or spellmaker copies a custom spell's effects into the editor; the result is a new spell at full price, as Morrowind would charge. Replacing the original (keeping its slot, hotkeys and favorites) and paying only the difference are settings | v1.0 |
| Hotkeys and favorites | Survive saving, loading and restarting, because every custom spell lives in a fixed plugin record | v1.0 |
| Accurate item cards | The Magic menu shows Morrowind-style lines with min–max ranges (*Fire Damage 10 to 20 pts on Target*), which Skyrim's shared effect descriptions can't express; the plugin injects them | v1.0 |
| Capacity | Up to 500 custom spells; mixed-range spells also use 1–2 of 500 shared sub-spell slots, and the MCM shows both pools | v1.0 |
| Prepare for uninstall | An MCM button removes custom spells from the player and followers, dispels their effects, clears marks and ledgers, then says when it's safe to save and remove the mod | v1.0 |
| Teach followers | *Let me teach you a spell* gives a follower a copy, used through normal combat AI. Users of existing spellcrafting mods ask for this and can't have it | v1.x |
| Scribe tomes | At an altar, a roll of paper and an inkwell turn a custom spell into a tome that teaches it | v1.x |
| Share builds | Export the spellbook to JSON and import someone else's; imports still need the effects known and the gold, unless sandbox mode is on | v1.x |

## Technical architecture

One C++ SKSE plugin owns all behavior, and two ESL-flagged plugins hold only static records. Custom spells live in fixed, pre-made spell records that the plugin rewrites at runtime, so saves always point at stable FormIDs.

```text
DATA FILES (read at startup)
  LostArt.esp ............ effects, tomes, altars, dialogue
  LostArt_Slots.esp ...... 500 + 500 blank spell slots
  JSON packs ............. effects, stand-ins, discovery rules
        |
        v
LostArt.dll (CommonLibSSE-NG)
  Services --> Spellmaking menu --> CostEngine --> SpellCompiler
  (altars,      (SWF, item cards)   (cost, price,    (definition to records)
   spellmakers)                      rank)                 | writes
  Effect catalog --(known effects)--> menu                 v
  Cast router (+ 44 custom effects)   Persistence       SlotPool (1,000 fixed slots)
        ^ cast events                   ^ save/reload        | fills slots
        v                               v                    v
SKYRIM ENGINE AND SCRIPTS
  Papyrus (dialogue, MCM) --opens menu--> Services
  Casting (hits, active effects)   Save files (save + SKSE co-save)   Spell records (in the spell list)
```

Only the DLL changes behavior. The menu prices a spell, the compiler writes it into a fixed slot record that the player's spell list already points to, and persistence rebuilds every slot from the co-save whenever a save loads.

### Why fixed slots

| Approach | Used by | What happened | Decision |
| --- | --- | --- | --- |
| Pre-made spell slots rewritten at runtime | [The Last Altar](https://www.nexusmods.com/skyrim/mods/49032) (54 slots), [Simple Obvious Spell-crafting](https://www.nexusmods.com/skyrimspecialedition/mods/48621) (50) | Stable FormIDs, but buff spells vanished after reloading in Simple Obvious Spell-crafting because edits weren't fully re-applied | **Chosen**, with a full re-apply on every load |
| Runtime-created forms through DPF | [Fourth Era Spell-Crafting](https://www.nexusmods.com/skyrimspecialedition/mods/157287) | Unlimited spells; users report spells vanishing with the ESL build and active buffs stacking after reload | Not for v1.0; DPF RE is the fallback if 500 slots prove too few |
| Papyrus Extender's AddMagicEffectToSpell | Prototypes | Saved by the extender, but no cost override, casting type and delivery must match the slot, and setting them rewrites shared magic effects | Milestone 1 prototype only |
| An old 32-bit helper DLL | [Spell Crafting for Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/19697), SE port | Crafted spells cost no magicka on SE, and the bundled DLL crashes | Lesson: set costs natively and ship one maintained DLL |

### Components

1. **EffectCatalog** loads the effect JSON, binds each entry to its magic-effect variants, and validates everything when game data loads.
2. **EffectDiscovery** maps the player's known spells to catalog effects (lookup table, rules, pass-through).
3. **CostEngine** holds the three cost models, price, rank ladders and casting chance as pure functions with unit tests.
4. **SpellCompiler** turns a spell definition into records: primary delivery, effect order, riders and attribute bundles, name, cost override, half-cost perk, flags, hostility recount, sub-spells.
5. **SlotPool** allocates and frees 500 primary slots and 500 shared sub-spell slots.
6. **Persistence** writes the co-save and handles load (re-apply every definition) and revert (blank every slot before another save loads).
7. **CastRouter** runs on release: casts sub-spells, resolves area, rolls magnitudes through the active-effect hook, and runs the failure check.
8. **Effect systems** implement the 44 custom effects: flight, falling, jumping, swimming, elemental retaliation, the miss system, Silence and Sound, Reflect, Dispel, detection scans, telekinetic reach, Open and Lock, teleports, Command, bound armor, condition pools and the attribute ledger.
9. **UI** covers the spellmaking menu, Magic-menu item cards and the delete hotkey.
10. **Services** handle altar furniture events, the NPC service entry point (a Papyrus native called from dialogue), pricing, gold transfer, and vendor and loot injection.
11. **Papyrus API** serves the dialogue scripts and other mods.
12. **Config and logs:** MCM Helper settings, JSON data packs, a spdlog log file and console debug commands.

### Load and save sequence

1. **Game data loaded:** the catalog reads JSON, binds magic effects and validates; vendors and loot lists receive their tomes.
2. **Before a save loads:** revert blanks every slot and clears the registries, so one save's spells can never leak into another.
3. **Save loads:** the player's spell list already points at slot records that exist in the plugin, so nothing is dropped.
4. **Co-save loads:** each definition is recompiled into its slot. A missing effect pack downgrades that effect to an inert placeholder with a warning instead of a crash.
5. **After load:** spells equipped in either hand are re-equipped to rebuild hand art; active effects keep running because they reference stable records.
6. **On save:** definitions, marks, ledgers and condition pools go into the co-save with a schema version for migrations.

### Papyrus API

```papyrus
Scriptname LostArt Hidden

; Opens the spellmaking menu; akProvider is the altar or spellmaker NPC
Function OpenSpellmaking(ObjectReference akProvider) global native
bool Function IsCustomSpell(Spell akSpell) global native
int Function GetCustomSpellCount() global native
int Function GetFreeSlotCount() global native
bool Function DeleteCustomSpell(Spell akSpell) global native
Function SetAltarActive(ObjectReference akAltar, bool abActive) global native

; Mod events: LostArt_SpellCreated(Form akSpell), LostArt_SpellDeleted(Form akSpell)
```

### Threading and safety

- Every record edit runs on the main thread through SKSE's task queue; UI callbacks never touch records directly.
- Hooks are few and resolved through Address Library. Each checks for other plugins hooking the same spot (po3's Tweaks for jumping, flight frameworks for Levitate) and yields to them.
- No Papyrus polling: effects are event-driven, or updated by the plugin's frame hook only while active.

### Prototype path

Milestone 1 is Papyrus only: Papyrus Extender's AddMagicEffectToSpell on 20 pre-made slots, UIExtensions list and text menus, and an effect whitelist whose casting type and delivery match each slot. It has no cost override, min–max or mixed ranges; its job is to validate persistence and the menu flow early.

## Data model & content pipeline

Effects, stand-ins, rank ladders and discovery rules live in JSON. A Mutagen program generates the data-driven records in both ESL plugins from that JSON, and Spriggit keeps the plugins diffable in git.

### Effect definition

One file per effect family; the values below are illustrative.

```json
{
  "id": "mw.fire_damage",
  "name": "$LA_Effect_FireDamage",
  "morrowind": { "index": 14, "school": "Destruction", "baseCost": 5.0 },
  "skyrim": { "school": "Destruction", "baseCost": { "fitFrom": "Firebolt" } },
  "ranges": ["self", "touch", "target"],
  "magnitude": { "unit": "pts", "ticking": true },
  "duration": true,
  "area": true,
  "variants": {
    "self": "LA_FireDamage_Self",
    "touch": "LA_FireDamage_Touch",
    "target": "LA_FireDamage_Target"
  },
  "keywords": ["MagicDamageFire"],
  "riders": ["IntenseFlames", "Impact"],
  "rankLadder": { "Apprentice": 25, "Adept": 40, "Expert": 60, "Master": 100 },
  "discovery": { "archetype": "ValueModifier", "actorValue": "Health", "resist": "FireResist", "hostile": true }
}
```

A stand-in entry names the effect, the Skyrim creature, a level scale and the credit line for the item card:

```json
{ "effect": "mw.summon_scamp", "creature": "Familiar", "levelScale": 1.0, "credit": "$LA_StandIn_Scamp" }
```

### Spell definition

| Field | Type | Notes |
| --- | --- | --- |
| slot | uint16 | Index into the slot pool |
| name | string | Up to 40 characters |
| effects | up to 8 records | Effect id, attribute or skill, range, min, max, duration, area |
| costModel | enum | The model in force at purchase |
| cost | uint32 | Fixed at purchase, applied as the cost override |
| pricePaid | uint32 | Needed for pay-the-difference edits |
| created | float | Game days passed at creation |
| provider | FormID | The altar or spellmaker used |
| flags | bitfield | Replaced, imported, needs-rebalance |

### Co-save records

The plugin registers the co-save id `LART`; every record carries a schema version.

| Record | Contents |
| --- | --- |
| LADF | Spell definitions |
| LAMK | Mark location or locations |
| LALG | Attribute damage ledger per actor |
| LACP | Disintegrate condition pools per actor and item |
| LAVR | Schema version and migration history |

### Record budget

| Plugin | Contents | Records |
| --- | --- | --- |
| LostArt.esp (ESL-flagged) | About 700 magic-effect variants (up to three ranges per effect; 26 attribute and skill targets for those families), keywords, touch projectiles, visual explosions, shaders, lights, tomes and their spells, books, NPCs and stand-ins, altars, dialogue, messages, globals and lists | About 1,350 |
| LostArt\_Slots.esp (ESL-flagged) | 500 primary slots and 500 sub-spell slots | 1,000 |

Both stay under 2,048 records, so they load on SE 1.5.97 without Backported ESL Support. AE's newer 4,096-record ESL header would allow merging them into one plugin later.

### Pipeline

1. The JSON in `data/` is the single source of truth for effects and slots.
2. `tools/Generator` (C#, Mutagen) builds magic effects, keywords, slots, tomes and lists, with FormIDs kept stable by EditorID.
3. Hand-made records (dialogue, altar furniture, new NPCs and stand-in actors; no vanilla record is edited) are authored in the Creation Kit and stored as Spriggit text. The generator writes its records into the same plugin, matched by EditorID, without touching the hand-made ones.
4. CI builds the DLL (CMake, vcpkg, CommonLibSSE-NG), runs unit tests, compiles Papyrus, regenerates the plugins, fails if the Spriggit text changed unexpectedly, and packages a FOMOD installer.

### Repository layout

```text
lost-art/
  skse/              C++ plugin: catalog, compiler, cost, effects, ui, persist
    tests/           Catch2 unit tests and golden cost vectors
  interface/         ActionScript 2 source and the built SWF
  scripts/           Papyrus: dialogue fragments, MCM glue
  data/
    effects/         effect catalog JSON
    standins/        summon and weapon stand-ins
    discovery/       vanilla lookup table and rules
    translations/    LostArt_ENGLISH.txt and other languages
  plugin/            Spriggit text of both plugins
  tools/Generator/   Mutagen plugin generator
  fomod/             installer config
  docs/              player guide, modder guide, formulas
```

## Compatibility

Crafted spells use vanilla keywords, vanilla rider effects and record-free injection, so common overhauls need no patches. The real conflicts are other SKSE plugins hooking the same engine functions, and the plugin detects and yields to those.

| Area | Examples | How it's handled |
| --- | --- | --- |
| Perk overhauls | Vokrii, Ordinator, Adamant | Keyword-based perks apply automatically; perks that rely on rider effects need a small JSON rider pack per overhaul, which the community can extend |
| Magic overhauls and spell packs | Apocalypse, Odin, Triumvirate, Mysticism | Effect Discovery classifies their spells; unrecognized effects pass through at their own range or stay hidden |
| Cost rebalances | Requiem, Mysticism, spell-cost tweaks | Skyrim-balanced mode reads base costs live from vanilla magic effects, so rebalances carry into crafted spells |
| Other spellcrafting mods | Fourth Era Spell-Crafting, Spell Research, Spellshaping | No record conflicts; two spellcrafting systems at once is pointless but safe |
| Jumping, gliding, flight | po3's Tweaks, Skyrim's Paraglider, Flight Framework, Levitation RE-SE-T | Jump uses po3's JumpingBonus fix when present; Levitate and Slowfall yield while another flight or glide controller is active |
| Teleport mods | Translocate, Divine Intervention, Mysticism's Mark and Recall | Independent, no shared data |
| Water walking | Vanilla Waterwalking effect, Sea Stride | Same actor value, no conflict |
| UI | SkyUI (required), SkyHUD, moreHUD, Wheeler, iEquip | Custom spells are ordinary spells; item-card injection is tested against SkyUI's magic menu. Soulsy HUD has crashed alongside another spellcrafting mod, so it's on the test list |
| Interiors | College of Winterhold and Solstheim overhauls | Altars are placed at runtime, with an alternate marker when the spot is occupied |
| Engine fixes | SSE Engine Fixes, SSE Display Tweaks | Recommended, not required |
| VR | SkyrimVR | The DLL builds from the same CommonLib fork; the menu needs a VR layout pass (v1.x) |
| Multiplayer | Skyrim Together Reborn, SkyMP | Not supported: runtime spell edits and co-save data aren't synchronized between players |
| Load order | Any | Both plugins are ESL-flagged and edit no vanilla records; tomes, vendor stock and dialogue are added without touching vanilla forms |

- **Updates:** slot and effect FormIDs never change between versions, because the generator keys them to EditorIDs; the co-save schema version drives migrations.
- **Uninstalling:** run *Prepare for uninstall* first. Removing the mod without it leaves blank spells in the spell list and orphaned co-save data, and the player guide says so plainly.

## Configuration (MCM)

Most departures from Morrowind are settings; the rest are listed in the parity checklist. Defaults favor Skyrim for magicka, gold, summons, skills and perk riders, and Morrowind for the spellmaking rules. The MCM is built with MCM Helper, so settings live in an INI that players can share.

### General

| Option | Default | Values |
| --- | --- | --- |
| Altars | On | On, Off |
| NPC spellmakers | On | On, Off |
| Effect names | Morrowind | Morrowind, Skyrim (Fury, Fear, Courage) |

### Costs

| Option | Default | Values |
| --- | --- | --- |
| Cost model | Skyrim-balanced | Skyrim-balanced, Morrowind Classic, Engine autocalc |
| Target ×1.5 on the running total | On in Classic | On, Off |
| Global cost multiplier | 1.0 | 0.25–4.0 |
| Price multiplier | 3 (7 in Classic) | 1–20 |
| Skyrim haggling | Off | On, Off |
| Altar fee | Full price | Full, Half, Free, One filled soul gem |

### Menu

| Option | Default | Values |
| --- | --- | --- |
| Max effects per spell | 8 | 1–8 |
| Magnitude cap | 100 | 100–500 |
| Duration cap | 1440 s | 60–3600 s |
| Area cap | 50 ft | 0–100 ft |
| Close after creating | On | On, Off |
| Starting range | OpenMW rule | OpenMW rule, First allowed |
| Show cost math | Off | On, Off |

### Casting

| Option | Default | Values |
| --- | --- | --- |
| Casting failure | Off | On, Off |
| Min–max rolls | On | On, Off (always the average) |
| Skyrim elemental riders | On | On, Off |
| Touch reach | 192 units | 128–320 units |
| Area needs line of sight | On | On, Off |
| Summon limit | Skyrim | Skyrim, Morrowind (one per type) |
| Grace Slowfall after Levitate | Off | On, Off |
| Jump height per point | 3% | 1–10% |
| Marks | 1 | 1–10 |
| Casting bonus (stands in for Willpower and Luck) | 15 | 0–30 |
| Casting failure for NPCs | Off | On, Off |
| Experience school in Classic | Costliest effect | Costliest effect, Hardest effect |

### Effects

| Option | Default | Values |
| --- | --- | --- |
| Effect availability | Known spells | Known spells, Sandbox |
| Skyrim-only effects | On | On, Off |
| Extended Morrowind effects | Off | On, Off |
| Pass-through modded effects | On | On, Off |
| Attribute profile | Default | Default, Light, Off |
| Skill picker | Skyrim (18) | Skyrim (18), Morrowind (27) |
| Fortify Skill target | Skill level | Skill level, Percent modifier |
| Morrowind tomes in shops and loot | On | On, Off |
| Bound item skill bonuses | Off | On, Off |

### Services

| Option | Default | Values |
| --- | --- | --- |
| College masters require membership | On | On, Off |
| Refuse wanted players | On | On, Off |
| Per-NPC price modifiers | Off | On, Off |

### Spellbook

| Option | Default | Values |
| --- | --- | --- |
| Deletable spells | Any non-inherent | Custom only, Any non-inherent |
| Load pricing | Full price | Full price, Difference only |
| Replace original on load | Off | On, Off |
| Slots in use | Readout | Spells and sub-spells, 0–500 each |

### Maintenance

| Option | Default | Values |
| --- | --- | --- |
| Rebalance all custom spells | Button | Recomputes costs under the current model |
| Rebuild all slots | Button | Re-applies every definition |
| Prepare for uninstall | Button | See Compatibility |
| Debug logging | Off | Off, Info, Verbose |

## Art, audio & presentation

Everything is built from vanilla and DLC assets or new original work. Nothing is ported from Morrowind or Oblivion: Nexus moderators don't allow assets from other Bethesda games without permission, and Skywind itself builds every asset from scratch.

### Asset rules

- No meshes, textures, sounds or UI art taken from another Bethesda game ([Nexus forum ruling](https://forums.nexusmods.com/topic/9720278-questions-about-content-usage-from-other-bethesda-games/)); recreating a look is fine.
- Morrowind is evoked through shapes, colours and names, never copied files.
- Every new asset ships with its source files (Blender, Substance or GIMP projects) in the repository.

### Altars

| Altar | Design | Built from |
| --- | --- | --- |
| College altar | A waist-high plinth of Winterhold stone with a floating focus stone ringed by slow-orbiting glyphs; the focus glows in the school colour of the spell being built | College architecture kit, vanilla magic-anomaly and soul-gem effects, one new focus mesh |
| Telvanni altar | A grown mushroom-cap lectern with glowing gills and the same floating focus | Tel Mithryn architecture and fungus kit from Dragonborn, the same focus mesh |

- The furniture reuses the enchanting table's entry and idle animations, so camera and hands behave like Skyrim crafting.
- States: dormant (dark), idle (slow glow and hum), in use (glyphs speed up), creating (a flash burst).

### Visual effects

- Creation burst in the school colour; a short puff for touch projectiles; three sizes of school-tinted explosions for area.
- Elemental shields use cloak-style shaders without a damage aura, with sparks on an attacker who gets burned.
- Levitate adds a faint shimmer under the feet; Slowfall sheds drifting motes.
- Detection highlights keys in gold, enchanted items in violet and animals in green, each pulsing at a different rate so colour isn't the only cue.
- Recall and the Interventions flash at departure and arrival; Mark leaves an optional faint rune.

### Audio

- The altar has an ambient hum loop and a glyph whoosh when effects are added.
- The menu uses vanilla enchanting UI sounds; creating plays the vanilla spell-learned sound over a magic flash.
- Failed casts use the vanilla fizzle; Levitate adds a soft wind loop.
- No new voice acting; new text uses subtitled silent lines.

### Icons and text

- Effect icons follow Skyrim's school icons, so the menu reads like the Magic menu.
- Tomes use vanilla book models with new cover textures per school.
- Three original lore books: *Fragments on the Lost Art* (a College scholar), *The Spellmaker's Primer* (Mages Guild, Third Era) and *On Telvanni Inscription* (attributed to a Telvanni master).

## QA & acceptance

Release requires every parity rule, every effect and every save scenario to pass on SE 1.5.97 and current AE, with the formula tests automated in CI.

### Automated tests

- **Cost engine:** the Classic worked examples in the cost section are golden vectors, alongside the chance example (59% in the menu and 73% at full fatigue, with Willpower 40 and Luck 40). Property tests check that effect order never changes a Skyrim-balanced cost, that max never drops below min, and that price is at least 1.
- **Compiler:** every catalog effect compiles on each allowed range, riders are appended, the effect count stays under the engine ceiling, and the hostile flag is right.
- **Data:** JSON schema validation, every variant EditorID resolves, every catalog effect has a source (a vanilla spell or a tome), and every translation key exists in every language file.
- **Plugins:** Spriggit round-trips unchanged, with no errors, no identical-to-master records and no vanilla edits.

### Test cell

A developer-only cell, reached by console, holds dummies from level 1 to 80 (NPCs and creatures), doors and chests at every lock tier plus Requires Key, keys and enchanted items on the floor and in containers, a pool and a river, ledges at 5, 10 and 20 m, and a board that teleports to each Intervention destination.

### Effect tests

Each of the 119 effects is checked at minimum and maximum magnitude, on every allowed range, against the player and an NPC: the expected stat change, duration, area radius, and clean-up when it ends or is dispelled. Custom effects add their own cases: Open never opens a Requires Key lock, Recall is blocked where a quest disabled fast travel, and Levitate ends safely over water, lava and a ledge.

### Save and load matrix

| Scenario | Must hold |
| --- | --- |
| Create spells, save, reload | Names, effects, costs, hotkeys and favorites intact |
| Load a save with different custom spells without restarting | Nothing leaks from the previous save |
| Start a new game after playing | Every slot blank |
| Save with a custom spell in each hand | Correct hand art after loading |
| Save with a custom buff active | The buff keeps its remaining time and doesn't stack on recast, a known failure in existing mods |
| Save while levitating, falling under Slowfall or with a summon out | State restored or ended safely |
| Set a Mark, reload, Recall | Arrives at the mark |
| Reload with attribute damage active | Ledger intact; Restore still repairs it |
| Update the mod between saves | Migrations run; no slot moves |
| Prepare for uninstall, save, remove the mod, load | Clean load, no log errors |
| Add or remove an unrelated mod mid-playthrough | Custom spells unaffected |

Every row runs on Steam SE 1.5.97, Steam AE and GOG AE. In-game rows are written as scripted checks with explicit pass criteria and a log line per result, so any run can be reported back verbatim.

### Compatibility runs

SkyUI, SkyHUD, moreHUD, Wheeler, Soulsy HUD, Ordinator, Vokrii, Adamant, Apocalypse, Mysticism, Odin, Triumvirate, Requiem, po3's Tweaks, Skyrim's Paraglider, Flight Framework and SSE Engine Fixes, each loaded alone with the mod and then together.

### Performance budgets

| Measure | Budget |
| --- | --- |
| Menu open with 200 known effects | Under 150 ms |
| Cost and preview update per slider step | Under 2 ms |
| Extra work per cast | Under 0.2 ms |
| Detection scan while active | Under 1 ms per second |
| Co-save size with 500 spells | Under 64 KB |
| Papyrus polling loops | None |

### Definition of done, per feature

- [ ] Matches its catalog or checklist row
- [ ] Strings localized; works with controller and mouse
- [ ] Unit or scripted test passing
- [ ] The save-matrix rows it touches pass
- [ ] Any departure from Morrowind is an MCM setting or listed in the parity checklist, and explained in the player guide
- [ ] No warnings in the plugin log, and inside the performance budget

## Roadmap

Seven milestones in dependency order; each ends at a gate that must pass before the next begins.

### Milestones

1. **M0 Setup and spikes:** repository, CI, the CommonLibSSE-NG plugin skeleton and the seven spikes below.
2. **M1 Papyrus prototype:** one altar, UIExtensions menus and 20 slots through Papyrus Extender.
3. **M2 Core engine:** catalog, discovery, cost engine with unit tests, compiler, slot pool, persistence and cast router.
4. **M3 Spellmaking menu:** the full SWF menu, item-card injection, the delete hotkey, localization and controller support.
5. **M4 All 119 effects:** the 65 native effects generated from JSON, the 39 other custom effects, then the 10 stand-ins and 5 bound-armor effects.
6. **M5 Content:** altars, the 15 spellmakers and their dialogue, tomes, loot and lore books.
7. **M6 Polish and release:** MCM, balance calibration, the QA matrix, performance, documentation and the release package.

### Gates

- **G1, end of M0:** all seven spikes answered, and the fixed-slot approach survives saving and reloading with a custom buff active.
- **G2, end of M2:** the core save matrix passes with 50 test spells.
- **G3, end of M4:** the parity checklist is complete and at least 95% of effect tests pass.
- **G4, release:** everything in QA passes on SE and AE.

### Technical spikes

| Spike | Question | Fallback |
| --- | --- | --- |
| S1 Area | Does effect area or explosion radius decide who is hit? | Already sidestepped by the plugin's own area resolver; the spike rules out double hits |
| S2 Persistence | Do re-applied slots, equipped hands and active buffs survive saving, reloading and switching saves? | DPF RE for stable runtime FormIDs |
| S3 Menu | Can an AS2 menu with virtualized lists and gamepad focus be built in the time? | A Prisma UI (HTML) menu |
| S4 Levitation | Does a velocity-based flight controller feel smooth, with usable animations? | Invisible-platform levitation, as older mods do |
| S5 Water walking | Does the WaterWalking actor value work for the player and NPCs? | An invisible platform under the actor |
| S6 Stacking | Do buffs from different slots that share a magic effect stack? | A small, budgeted set of variant effects assigned to slots, or runtime forms via DPF RE |
| S7 Cost | Does casting experience track the cost override, and does the (1 + A/16) area term fit vanilla area spells? | Tune skill-usage multipliers; refit the area term |

### Risks

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Save-persistence edge cases (equipped spells, active effects, switching saves) | Medium | High | Spike S2, the revert handler, and the save matrix from M2 on |
| Levitation feels bad | Medium | Medium | Spike S4, the platform fallback, and a hover-only mode as a last resort |
| The Scaleform menu takes longer than planned | Medium | Medium | Start from SkyUI's open-source classes; Prisma UI fallback |
| Balance drifts toward cheap control spells or pricey buffs | High | Medium | Skyrim-balanced default, a calibration script against vanilla costs, MCM multipliers |
| A game update breaks hooks | Low | High | Address Library, few hooks, and a version check with a clear message |
| The record budget overflows | Low (Medium if S6 fails) | Medium | Two ESL plugins now; AE's 4,096-record header later |
| Scope creep | High | Medium | The v1.x list is frozen; the parity checklist defines done |

## Release

Ship on Nexus as a FOMOD installer, with the source on GitHub.

### Packaging

- The FOMOD installs the core files (DLL, both plugins, SWF, scripts, JSON) plus optional translation packs; every behavior choice is a runtime setting, so nobody reinstalls to change one.
- Requirements: SKSE64 matching the game build, Address Library for SKSE Plugins, SkyUI and MCM Helper; SSE Engine Fixes recommended.
- Code under the MIT license on GitHub; original assets with open permissions for patches and translations.

### Documentation

- A Nexus description with features, requirements, the parity summary and known limits.
- The in-game *Spellmaker's Primer*, which doubles as the tutorial.
- A player guide (formulas, settings, uninstalling) and a modder guide (JSON packs, Papyrus API, mod events).
- A changelog kept from the first public build.

### After release

- Compatibility fixes ship as JSON rider packs.
- v1.x, in order: the questline, casting-failure polish, concentration, runes and walls, teaching followers, scribing tomes, sharing builds, VR.

## Appendix

### Morrowind constants

| Setting | Value | Used for |
| --- | --- | --- |
| fEffectCostMult | 0.5 | Effect cost |
| fSpellMakingValueMult | 7 | Spellmaking price |
| fSpellValueMult | 10 | Price of ready-made spells |
| fFatigueBase | 1.25 | Fatigue term at full fatigue |
| fFatigueMult | 0.5 | Fatigue term slope |
| fElementalShieldMult | 0.1 | Elemental shield retaliation |
| Units per foot | 21.33 (area rounds up to 22) | Area radius |

### Skyrim constants

| Setting | Value | Used for |
| --- | --- | --- |
| Skill cost reduction | × (1 − (skill/400)^0.65) | Magicka cost; about 0.59 at skill 100 |
| fMagicDualCastingCostMult | 2.8 | Dual-cast cost |
| fMagicDualCastingEffectivenessBase | 2.2 | Dual-cast magnitude or duration |
| Lock levels | 1, 25, 50, 75, 100; 255 requires a key | Open and Lock |
| Units per foot | About 21.33 (128 units = 6 ft) | Distances |
| ESL record limit | 2,048; 4,096 with the 1.71 header (AE 1.6.1130 and later) | Record budget |

### Glossary

| Term | Meaning |
| --- | --- |
| Address Library | A database that lets one DLL find engine functions on every game build |
| Archetype | The built-in behavior a Skyrim magic effect uses, such as value modifier, summon or paralysis |
| Co-save | The file SKSE writes beside each save for plugin data |
| CommonLibSSE-NG | The C++ library for SKSE plugins that run on SE, AE and VR |
| ESL-flagged plugin | A light plugin that takes no load-order slot |
| Magic effect | Skyrim's record for a single effect; a spell lists several |
| Mutagen, Spriggit | A C# library that writes plugins; a tool that stores plugins as text for git |
| Rider | A hidden vanilla effect appended so a perk that depends on it still applies |
| Scaleform | The Flash-based technology behind Skyrim's menus, scripted in ActionScript 2 |
| Slot | A pre-made spell record the plugin rewrites into a custom spell |
| Spike | A short experiment that answers one technical question before the real build |
| Stand-in | A Skyrim creature or weapon used where Morrowind's doesn't exist |

### Sources

Opened during research on 29 September 2026; some UESP pages were read through its API.

- **Morrowind:** [UESP: Spellmaking](https://en.uesp.net/wiki/Morrowind:Spellmaking) · [UESP: Spell Effects](https://en.uesp.net/wiki/Morrowind:Spell_Effects) · [Fandom: Spellmaking (Morrowind)](<https://elderscrolls.fandom.com/wiki/Spellmaking_(Morrowind)>) · [OpenMW source](https://github.com/OpenMW/openmw) · [MWSE game settings](https://mwse.github.io/MWSE/references/gmst/) · [Morrowind Code Patch](https://www.nexusmods.com/morrowind/mods/19510)
- **Skyrim engine and data:** [CK wiki: Magic Effect](https://ck.uesp.net/wiki/Magic_Effect) · [CK wiki: Spell](https://ck.uesp.net/wiki/Spell) · [CK wiki: Actor Value List](https://ck.uesp.net/wiki/Actor_Value_List) · [CK wiki: SetNthEffectMagnitude](https://ck.uesp.net/wiki/SetNthEffectMagnitude_-_Spell) · [CK wiki: Unit](https://ck.uesp.net/wiki/Unit) · [SKSE64](https://github.com/ianpatt/skse64) · [CommonLibSSE-NG, maintained fork](https://github.com/alandtse/CommonLibVR/tree/ng) · [Papyrus Extender](https://github.com/powerof3/PapyrusExtenderSSE) · [Dynamic Persistent Forms](https://www.nexusmods.com/skyrimspecialedition/mods/116001) · [DPF RE](https://www.nexusmods.com/skyrimspecialedition/mods/183427) · [Backported ESL Support](https://github.com/Nukem9/skyrimse-backported-esl-support) · [Mutagen](https://github.com/Mutagen-Modding/Mutagen) · [Spriggit](https://github.com/Mutagen-Modding/Spriggit) · [Mutagen keyword list](https://raw.githubusercontent.com/Mutagen-Modding/Mutagen.Bethesda.FormKeys/master/Mutagen.Bethesda.FormKeys.SkyrimSE/Skyrim/Keyword.cs) · [Jumping Bonus Fix notes](https://www.nexusmods.com/skyrimspecialedition/articles/3647) · [Nexus forum: magicka cost formula](https://forums.nexusmods.com/topic/7894068-how-to-calculate-magicka-costs-for-new-spells/)
- **Vanilla spell costs:** [Carl's Guides: Destruction](https://www.carlsguides.com/skyrim/magic/destruction.php) · [Restoration](https://www.carlsguides.com/skyrim/magic/restoration.php) · [Alteration](https://www.carlsguides.com/skyrim/magic/alteration.php) · [Conjuration](https://www.carlsguides.com/skyrim/magic/conjuration.php) · [Illusion](https://www.carlsguides.com/skyrim/magic/illusion.php)
- **UI frameworks:** [Prisma UI](https://www.nexusmods.com/skyrimspecialedition/mods/148718) · [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) · [UIExtensions](https://www.nexusmods.com/skyrimspecialedition/mods/17561) · [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000)
- **Prior art:** [Fourth Era Spell-Crafting](https://www.nexusmods.com/skyrimspecialedition/mods/157287) · [Spell Crafting for Skyrim (SE)](https://www.nexusmods.com/skyrimspecialedition/mods/19697) · [Spellmaking in Skyrim: The Last Altar](https://www.nexusmods.com/skyrim/mods/49032) · [Simple Obvious Spell-crafting](https://www.nexusmods.com/skyrimspecialedition/mods/48621) · [GiftOfSpellCraft](https://www.nexusmods.com/skyrimspecialedition/mods/184672) · [Levitation RE-SE-T](https://www.nexusmods.com/skyrimspecialedition/mods/72873) · [Flight Framework](https://www.nexusmods.com/skyrimspecialedition/mods/178943) · [Translocate](https://www.nexusmods.com/skyrimspecialedition/mods/11467) · [Divine Intervention](https://www.nexusmods.com/skyrimspecialedition/mods/10235) · [Unlock Spell SSE](https://www.nexusmods.com/skyrimspecialedition/mods/32021) · [Detect Levers and Keys](https://www.nexusmods.com/skyrimspecialedition/mods/77938) · [Mysticism](https://www.nexusmods.com/skyrimspecialedition/mods/27839)
- **Skywind and assets:** [Skywind volunteer page](https://tesrskywind.com/volunteer/) · [Skywind FAQ](https://tesrskywind.com/faq/) · [GameFront: Skywind 2026 update](https://www.gamefront.com/news/skywinds-latest-update-shows-off-spellcrafting-underwater-combat-and-a-nearly-complete-map) · [Nexus forum: assets from other Bethesda games](https://forums.nexusmods.com/topic/9720278-questions-about-content-usage-from-other-bethesda-games/)
- **Skyrim NPCs and places:** [UESP: Trainers](https://en.uesp.net/wiki/Skyrim:Trainers) · [UESP: Court Wizard](https://en.uesp.net/wiki/Skyrim:Court_Wizard) · [Fandom: Tel Mithryn](https://elderscrolls.fandom.com/wiki/Tel_Mithryn)
