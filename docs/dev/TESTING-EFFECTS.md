# Testing the custom effects and the cast router

This page covers the in-game suite `effects` (code: `skse/plugin/src/Effects/EffectTests.cpp` and
the `casting.*` cases in `skse/plugin/src/Casting/CastRouter.cpp`) and the checks a person has to
make by eye. The log format is in `docs/dev/CONTRACTS.md` section 9.

## Running the scripted suite

```
la test effects                                  (console)
devbench call LostArt RunTests '["effects"]'     (DevBench)
```

Then read `Documents/My Games/Skyrim Special Edition/SKSE/LostArt_Tests.log`.

- Run it in a quiet exterior, not in combat. Wear at least one piece of armor and hold a weapon in the
  right hand, or the two Disintegrate cases fail with a `precondition:` message.
- The suite places a dummy NPC 3000 units below you (the vanilla prisoner `CWPrisonerImperialA`,
  `Skyrim.esm|0x10D4B2`) with its AI off, and deletes it in the last case (`zz_cleanup.dummy`).
- Every case starts the effect's handler, checks the stat or state, ends the effect and checks the
  value is back. The handlers are the same code the engine's effect events reach, so what passes
  here is what a cast does after the effect lands. How the effect lands (projectile, area,
  magnitude roll) is covered by the manual checks below.
- A second run must pass as well: nothing may be left behind by the first.

### What the scripted cases check

| Case | Pass criterion |
| --- | --- |
| `fire_shield`, `frost_shield`, `lightning_shield` `.player/.npc.min/.max` | Fire/Frost/Shock resistance rises by exactly M (1 and 100) while active and returns to its old value after |
| `blind.detection.*` | Blindness actor value +M, restored |
| `chameleon.sneak.*` | Sneak +M/2, restored (the translucency is a manual check) |
| `jump.player.max`, `jump.npc.min` | JumpingBonus +M x `fJumpPerPoint`, restored |
| `jump.fall_reduction` | Jump 10 shortens the fall height by 10 ft (213 units) |
| `burden.npc.min/max`, `burden.player.native_only` | NPC speed -M/2 %, capped at -75; the player's speed is untouched (the native carry-weight change applies) |
| `fortify_attribute.strength.*` | Carry weight +3 x M (x0.5 on the Light profile, 0 on Off), restored |
| `drain_attribute.intelligence.*` | Maximum magicka -2 x M, restored |
| `fortify_attribute.speed.*`, `fortify_attribute.endurance.*` | SpeedMult +M, Health +M, restored |
| `absorb_attribute.willpower.*`, `absorb_attribute.caster_gain` | Target loses 0.25 x M magic resistance, the caster gains the absorbed Strength (+30 carry weight at M 10); both restored |
| `damage_restore_attribute.*` | Damage Attribute (instant) lowers carry weight by 3 x M and writes M into the ledger; Restore Attribute repairs it back to the start value |
| `fortify_skill.athletics.*`, `drain_skill.athletics.*` | SpeedMult +M / -M |
| `fortify_skill.hand_to_hand.*` | UnarmedDamage +M |
| `fortify_skill.acrobatics.player.max` | JumpingBonus +M x `fJumpPerPoint` |
| `absorb_skill.onehanded.npc.*` | One-Handed -M on the target, restored |
| `damage_restore_skill.player` | One-Handed -5 (Damage Skill, instant), then back (Restore Skill) |
| `swift_swim.on_land` | No speed change out of water |
| `sanctuary.min`, `sanctuary.max_capped` | Miss chance against you +1 at M 1, +75 at M 100 (cap) |
| `blind.miss.min/max` | Attacker's miss chance +M |
| `chameleon.evasion.max` | Miss chance against you +20 at M 100 (0.2 x M) |
| `fortify_attack.offsets_blind` | Blind 30 with Fortify Attack 10 gives a 20 % miss chance |
| `silence.*`, `sound.*`, `reflect.*`, `resist_paralysis.*`, `resist_corprus.*` | The query (IsSilenced, SoundMagnitude, magnitude sums) is set while active and cleared after |
| `levitate.state.*`, `slowfall.state.*` | Levitating / slowfalling and fall damage cancelled while active; cleared after |
| `telekinesis.reach` | `iActivatePickLength` grows by 20 ft (426 units) at M 20 |
| `mark.stores_position` | Mark stores your position (your own marks are put back afterwards) |
| `interventions.destinations` | All 7 destination markers resolve (5 cities + Fort Frostmoth + Raven Rock) |
| `bound_armor.records` | The 5 LA_Bound_* armor records are loaded |
| `command_humanoid.level_cap` | The dummy is commanded only when M is at least its level |
| `command_creature.rejects_humanoid` | Command Creature does nothing to an NPC |
| `charm.steps` | Charm 50 raises the relationship by 0 to 2 steps (one per 25 M, capped at Ally) |
| `disintegrate_armor.player` | The first worn piece (shield first) loses 10 condition, and the workbench repair path restores 100 |
| `disintegrate_weapon.player` | The right-hand weapon loses 25 condition, and the grindstone repair path restores 100 |
| `casting.touch_reach` | LA_TouchProjectile's range equals `iTouchReach` |
| `casting.chance_formula`, `casting.chance_off` | The menu chance uses the module formula at half stamina; -1 when the module is off |
| `casting.area_radius` | 10 ft = 220 units |

## Manual checks

These need a person watching, or real casts. Make each one with a crafted spell at minimum and at
maximum magnitude, on yourself and on an NPC, and write the result into the release checklist.

### Cast router

1. **Mixed ranges.** Make "Fire Damage 10 on Target + Fortify Health 20 for 30 s on Self + Frost
   Damage 10 on Touch". Cast it once: the fireball flies, your health rises, and the frost hits a
   target at arm's reach only. Dual cast it: all three are stronger (dual-cast effectiveness).
2. **Magnitude rolls.** "Fire Damage 1 to 100 on Target", 10 casts at a dummy: damage varies between
   casts; with `bMinMaxRolls=0` it is always 50. With Augmented Flames the rolled value is raised by
   the perk. A "Fortify Health 10 to 50 for 60 s" buff holds one value for its duration.
3. **Area.** "Fire Damage 20 in 20 ft on Target" at a group of three dummies: all within 440 units
   burn, the directly hit one only once (watch its health), you are never hit. Put a wall between a
   dummy and the impact point: it is spared with `bAreaLOS=1` and hit with `bAreaLOS=0`. The
   explosion visual matches the school and size (10 ft Small, 25 ft Medium, 50 ft Large).
4. **Touch reach.** A Touch spell at a dummy 3 m away does nothing; at 1.5 m it lands. Change
   `iTouchReach` in the MCM: the reach follows within 5 s.
5. **Casting failure** (`bCastingFailure=1`). The menu shows the chance; a low-skill character
   fails some casts: magicka is spent, "You failed casting the spell." shows, the fail sound plays,
   no hand art fires and the school skill gains no experience. NPCs never fail unless `bNPCFailure=1`.
6. **Sound and Silence.** Sound 50 on you: about half your casts fizzle (module off); with the
   module on, your chance drops by 50 instead. Silenced: spells can't be charged, powers and shouts
   still work. A silenced NPC mage switches to melee.
7. **Summon limit Morrowind** (`iSummonLimit=1`). Two different crafted summons coexist; casting the
   same summon again replaces the old one. (Needs `LA_Perk_SummonLimit` in LostArt.esp to lift
   Skyrim's total of one.)
8. **Linked tome spell.** Cast Skill Leech (`LA_TomeSpell_SkillLeech`) at an NPC: its linked
   `_Self` spell lands on you at release.

### Effects that need eyes

| Effect | What to check |
| --- | --- |
| Levitate | Jump rises, sneak descends, WASD moves at a speed that grows with M; hover is stable (no jitter); normal falling resumes when it ends; no fall damage while levitating; ends safely over water, lava and a 20 m ledge (with `bGraceSlowfall=1` the fall after it is soft). NPCs float up about 1.5 m and hover. With Skyrim's Paraglider or Flight Framework active, gliding wins (Lost Art logs that it yields) |
| Slowfall | Falling speed visibly slower (M 100: half speed), no fall damage from 20 m |
| Jump | Jump height grows by 3 % per point (without po3's Tweaks through `fJumpHeightMin`; with it, through JumpingBonus); falls count M ft shorter |
| Swift Swim | Faster only while swimming; normal on land |
| Elemental shields | Shell shader visible for the whole duration; a melee attacker takes fire/frost/shock damage per hit (0.1 x M, less for resistant or skilled attackers); blocked hits and spells don't retaliate |
| Chameleon | Translucency grows with M; NPCs notice you later when sneaking, also after you attack |
| Blind | A blinded bandit misses about M % of swings (no hit sound, no damage) |
| Sanctuary | Attacks against you miss about min(M, 75) %; spells still hit |
| Fortify Attack | Weapon damage +M %; offsets Blind and Sanctuary |
| Charm | The NPC's dialogue warms up (friend/confidant lines); buy prices drop by M/2 % with that merchant; everything back after the duration |
| Command Creature / Humanoid | A wolf / bandit up to level M follows and fights for you, then returns to its old behavior (hostile again) when the effect ends; quest NPCs are never commanded |
| Lock / Open | Open 50 unlocks an Average lock but not Hard, never a Requires Key door; opening an owned door in view of its owner is reported like lockpicking (bounty). Lock raises an open door to the tier at or below M |
| Bound armor | Weightless Daedric piece appears equipped at cast and disappears at the end; the piece you wore before is put back on |
| Detect Key / Enchantment | Keys glow gold, enchanted items violet, on the ground, in containers (the container glows) and on actors, within M ft; the glow stops when the effect ends |
| Telekinesis | Doors, containers and items can be activated from M ft further away |
| Mark / Recall | Recall returns you (and your followers) to the mark, also from another worldspace; blocked in Helgen before fast travel is enabled and in Sovngarde |
| Divine Intervention | From Whiterun's plains you arrive in the Temple of Kynareth; from Solstheim at Fort Frostmoth; from an interior, the temple nearest to where you entered it |
| Almsivi Intervention | Arrives at the Raven Rock temple |
| Dispel | Each active spell ends with M % chance; abilities, diseases and potions stay |
| Cure Blight / Paralyzation / Poison | Black-Heart Blight and Droops cured; paralysis ends at once; poison damage over time stops (health lost stays lost) |
| Resist Paralysis 100 | Paralysis spells and poisons never take hold |
| Resist Corprus 100 | Vampire hits never give Sanguinare Vampiris |
| Damage / Restore Attribute | Damage persists through saves and sleep; Restore repairs it; praying at a shrine repairs it |
| Disintegrate Armor / Weapon | Worn pieces (shield first) and the weapon lose condition; armor protects less and weapons hit softer as condition falls; at 0 the piece comes off and can't be re-equipped until you use a workbench (armor) or grindstone (weapons) |
| Burden on NPCs | A burdened NPC walks visibly slower (M 100: half speed) |
| Light on Target | The light stays on the struck actor and moves with it (engine behavior, no plugin code) |

### Save and load (effects rows of the QA matrix)

- Save while levitating, while falling under Slowfall and with a summon out; reload each: the
  effect continues (or ends safely), no stuck gravity, no double buff.
- Reload with attribute damage active: the ledger is intact and Restore still repairs it.
- Save with a custom buff active (Fortify Attribute, shield): after reload the buff keeps its time
  and the stat is not doubled; when it ends the stat returns exactly.
- Load another save without restarting while levitating: gravity and jump height are normal.

## Performance

`effects` logs nothing per frame. To measure, set `iLogLevel=2` and cast a spell with eight
effects: the cast adds no measurable frame time. The detection scan is capped at 0.8 ms per
second; with many full containers in range it may cover only part of the radius per scan.
