# Data notes

Notes on the JSON under `data/`: the exact file shapes the C++ loader reads, where the numbers came
from, how the Skyrim-balanced base costs were fitted, where the data departs from `docs/OUTLINE.md`,
and which values still need checking in the game or the Creation Kit.

Tools:

| Command | What it does |
| --- | --- |
| `python tools/validate_data.py` | Schema-validates every file, checks counts, sources, tomes, translations, form keys and the cost check numbers. Exits 1 on any failure |
| `python tools/validate_data.py --write-translations` | Regenerates `data/translations/LostArt_ENGLISH.txt` (UTF-16 LE, BOM, CRLF) from `LostArt_ENGLISH.src.tsv` |
| `python tools/calibrate.py [--write]` | Prints every base-cost fit, the scale K and the check numbers. `--write` stores the fits in `data/effects/*.json` |
| `python tools/check_formkeys.py [REPO] [--list]` | Checks that every `Plugin.esm\|0xXXXXXX` reference and keyword EditorID resolves in Mutagen.Bethesda.FormKeys. REPO defaults to `$LA_FORMKEYS`, then `/opt/deps/formkeys` |
| `python -m pytest tools/tests -q` | Runs the validator plus mutation tests that confirm it catches breakage |

## 1. File shapes (the loader contract)

All files are UTF-8 JSON. Loaders must ignore fields they don't know. Vanilla references are
written `"Skyrim.esm|0x012FD0"` (plugin plus a 6-digit local FormID). Every reference was resolved
by EditorID against Mutagen.Bethesda.FormKeys and is re-checked by `tools/check_formkeys.py`. Fields
named `*Label`, `label` or `labels` hold the EditorID of the neighbouring reference and are checked too.

### 1.1 `data/effects/*.json`: array of EffectDef (CONTRACTS §3)

There is one file per family: `alteration`, `conjuration`, `destruction`, `illusion`, `mysticism` and
`restoration` hold the 119 `mw` effects, grouped by Morrowind school. `extended.json` holds the 13 `mwx`
effects and `skyrim_only.json` the 12 `sk` effects. Fields follow CONTRACTS §3. Additions and clarifications:

| Field | Meaning |
| --- | --- |
| `morrowind` | `null` for `sk` effects. `index` is the OpenMW `ESM::MagicEffect` index (0–142) |
| `nameSkyrim` | `$LA_EffectSk_<Pascal>`, present only where the Skyrim naming option differs: Frenzy, Demoralize and Rally become Fury, Fear and Courage, and each stand-in shows its creature's name |
| `skyrim.baseCost` | B^sk, the Skyrim-balanced base cost at full double precision (§3) |
| `skyrim.fitFrom` | `{"kind":"vanilla","spell":ref,"label","magnitude","duration","area","cost"}` or `{"kind":"morrowind","tome":id,"label","magnitude":n or [min,max],"duration","area"}`. Every effect has one |
| `skyrim.creature` / `weapon` / `armor` | Vanilla NPC, WEAP or ARMO that a native summon, bound weapon or bound-armor effect is built from (stand-ins are in `stand-ins.json`) |
| `skyrim.flags` | MGEF flags for the generated variants. `Recover` marks Drain and Fortify value modifiers, which return the value when they end |
| `magnitude.unit` | Morrowind's display unit (OpenMW `getMagnitudeDisplayType`). `none` when the effect has no magnitude |
| `magnitude.ticking` | True for Damage X, Restore X, Absorb Health, Absorb Fatigue, Absorb Magicka, Poison, Disintegrate, and the elemental damage effects |
| `rankLadder` | Magnitude thresholds by rank. The rule is in §1.9 |
| `hostile`, `reflectable` | From Morrowind's hard-coded flags (`Harmful` 0x10 and `Unreflectable` 0x10000) |
| `sources` | Vanilla SPEL refs and `"tome:<id>"` entries pointing at `content/tomes.json`. There is at least one per effect |

Engine rules implied by the fields:
- `area` is true exactly when the effect can be cast on Touch or Target, which is Morrowind's rule. Self-only effects never have area.
- An effect without magnitude counts as 1 to 1 in cost. An effect without duration counts as 1 s in Classic and as instant in Skyrim-balanced.

### 1.2 `data/discovery/vanilla.json`

```json
{"map": [{"effect": "Skyrim.esm|0x012F03", "id": "mw.fire_damage", "label": "FireDamageFFAimed", "example": "Firebolt"}],
 "unmapped": [{"effect": "...", "label": "ClairvoyanceEffect", "example": "Clairvoyance", "reason": "..."}]}
```

- One entry per (MGEF, catalog id) pair. An MGEF that unlocks two catalog effects appears twice: vanilla Calm, Fear, Fury and Courage each unlock both the Humanoid and the Creature variant.
- `example` is informational only.
- `unmapped` lists effects of player-learnable spells that deliberately map to nothing: runes, wards, Sun Damage, Clairvoyance, Arvak and others. The pass-through setting may still show them.
- Concentration spells (Flames, Frostbite, Sparks, Healing, Healing Hands, Telekinesis, Detect Life and Dead, Equilibrium, Vampiric Drain) do teach their catalog effect. Morrowind's rule is "effects of spells you know", so a new character knows Fire Damage and Restore Health from the starting spells.

### 1.3 `data/discovery/rules.json`

```json
{"rules": [{"id": "mw.fire_damage", "alsoIds": [], "match": {"archetype": "ValueModifier", "actorValue": "Health",
  "resist": "FireResist", "hostile": true, "keywordsAny": [], "keywordsAll": [], "keywordsNone": [],
  "delivery": ["Aimed","Self","Touch","TargetActor","TargetLocation"], "castingType": "FireAndForget",
  "actorValueGroup": "Skill", "flagsAll": ["Recover"], "flagsNone": []}}]}
```

- Rules are ordered and the first match wins. Every `match` field is optional, and all present fields must match. String comparisons are case-insensitive.
- Archetypes use CK names without spaces. Actor values use `RE::ActorValue` names such as `ResistMagic`, `ResistDisease`, `ElectricResist`, `DamageResist` and `MovementNoiseMult`.
- Extensions beyond the coordinator's shape:
  - `alsoIds`: the rule unlocks these ids as well. It is used for Humanoid/Creature pairs.
  - `actorValueGroup`: `"Skill"` matches any of the 18 skill actor values.
  - `flagsAll` / `flagsNone`: MGEF flags that must be set or clear. `Recover` separates Drain and Fortify from Damage and Restore.
  - `keywordsNone`: keywords that must be absent.
- Specific rules (elemental resist, keywords) come before generic ones (Health plus hostile gives Damage Health).

### 1.4 `data/content/riders.json`

```json
{"riders": [{"id": "IntenseFlames", "effect": ref, "perk": ref|null, "elemental": false, "magnitudeScale": 1.0,
  "verify": false, "variants": {"aimed": ref, "aimedArea": ref, "selfArea": ref, "concentration": ref},
  "labels": ["..."], "dualCastOnly": false, "replacesPrimary": false, "notes": "..."}]}
```

- `effect` is the default rider MGEF. `variants` holds delivery-specific (or element- or weapon-specific) alternatives.
- `replacesPrimary: true` (Elemental Potency, Mystic Binding) means vanilla swaps the main effect for a perk-conditioned variant instead of adding a rider.
- `elemental: true` marks the Skyrim elemental companions `FrostStaminaSlow` and `ShockMagicka`, which are governed by `bElementalRiders`.
- Riders listed by effects: Fire (`IntenseFlames`, `Impact`), Frost (`FrostStaminaSlow`, `DeepFreeze`, `Impact`), Shock (`ShockMagicka`, `Disintegrate`, `Impact`), Flame, Frost and Storm Atronach (`ElementalPotency`), and every bound weapon (`MysticBinding`, `SoulStealer`).

### 1.5 `data/content/spellmakers.json`

```json
{"spellmakers": [{"id": "farengar", "npc": ref, "npcLabel": "FarengarSecretFire", "name": "Farengar Secret-Fire",
   "location": "Dragonsreach, Whiterun", "locationRef": ref, "refusal": "wanted:Whiterun", "crimeFaction": ref,
   "merchantChest": ref|null, "priceMult": 1.0, "specialties": ["Destruction","Mysticism"], "refusalLine": "$LA_Refuse_FarengarSecretFire"}],
 "universalRules": ["vampireStage4","relationshipBelowAcquaintance","serviceDisabled"],
 "collegeFaction": ref, "holds": {"Whiterun": {"crimeFaction": ref}}, "refusalReasons": {"0": "serves", ...}}
```

- `refusal` rule kinds:
  - `"college"`: the player must be in `CollegeofWinterholdFaction` (reason 1). Toggled by `bCollegeMembership`.
  - `"wanted:<Hold>"`: refuse when the player has a bounty with `crimeFaction` (reason 2). Holds: Haafingar, Whiterun, Eastmarch, Rift, Reach, Pale, Hjaalmarch. Toggled by `bRefuseWanted`.
  - `"quest:<Plugin|0xID>:<stage>"`: refuse until the quest has reached that stage or completed (reason 3). Neloth and Talvas use `DLC2TT1` (Reluctant Steward) stage 500. Elder Othreloth uses `DLC2RRFavor03` (Clean Sweep) stage 200.
  - `"anyone"`: never refuses.
- The DLL applies `universalRules` to every spellmaker (reasons 4, 5 and 6 of CONTRACTS §8).
- `specialties` are Morrowind school names, Mysticism included. They decide which tomes a spellmaker sells (`tomes[].vendors`).
- `merchantChest` is the vendor's base CONT record. At runtime, use the merchant container reference of the NPC's vendor faction, or the NPC's inventory when it is `null` (Elder Othreloth).

### 1.6 `data/content/altars.json`

```json
{"altars": [{"id": "college", "furniture": "LA_AltarCollege", "nameKey": "$LA_Altar_College", "cell": ref, "cellLabel": "...",
  "position": [x,y,z], "rotation": [rx,ry,rz], "alternate": {"position": [...], "rotation": [...]},
  "access": "college"|"anyone", "owner": ref (faction or NPC), "ownerLabel": "...", "freeFor": "archmage"|null,
  "freeForFaction": ref, "loreBook": "LA_Book_...", "verify": true, "notes": "..."}]}
```

Positions and rotations are placeholders (all zero) and must be set in the Creation Kit (`verify: true`).

### 1.7 `data/standins/stand-ins.json`

```json
{"standins": [{"effect": "mw.summon_scamp", "kind": "creature", "creature": ref, "creatureLabel": "EncSummonFamiliar",
  "nameKey": "$LA_EffectSk_SummonScamp", "levelScale": 1.0, "credit": "$LA_StandIn_Scamp", "actor": "LA_StandIn_SummonScamp"}]}
```

- Bound Spear is `kind: "weapon"`. It has `"creature": null`, `"weapon": <DaedricGreatsword>` and `"item": "LA_Bound_Spear"`.
- The file holds the 10 Morrowind stand-ins and 3 Extended ones (Centurion Sphere, Fabricant, Bonewolf).

### 1.8 `data/content/tomes.json`

```json
{"tomes": [{"id": "levitate", "title": "$LA_Tome_Levitate", "spellName": "$LA_TomeSpell_Levitate", "school": "Alteration",
  "mwSchool": "Alteration", "effects": [{"id": "mw.levitate", "sub": -1, "range": "self", "min": 10, "max": 10, "duration": 30, "area": 0}],
  "value": 595, "rank": "Adept", "vendors": ["Alteration"], "loot": true, "lootLists": [ref], "extended": false,
  "original": true, "valuesFrom": ["..."], "skyrimCost": 199}]}
```

- `school` is the Skyrim school of the costliest effect. `vendors` lists the Morrowind schools whose spellmakers stock the tome.
- `lootLists` are the vanilla leveled lists the plugin injects into at game start, chosen by school and rank.
- `value` is the Skyrim-balanced price (3 × cost), rounded to 5, with a minimum of 25.
- Effects without magnitude or duration store 1.
- `sub`: attributes use 0–7 in Morrowind order and Skyrim skills use 0–17 (CONTRACTS §2).

### 1.9 `data/content/ranks.json`

`{"costThresholds": [60,130,250,500], "rankNames": [...], "ladderRule": "...", "perks": {"Alteration": {"Novice": ref, ...}}, "perkLabels": {...}}`

- An effect's rank is the highest rank whose `rankLadder` threshold is at or below the effect's **max** magnitude. Magnitude counts as 1 for effects without one, so `{"Expert": 1}` pins an effect such as Invisibility to Expert.
- Effects without a ladder use `costThresholds` on their Skyrim-balanced cost: below 60 Novice, below 130 Apprentice, below 250 Adept, below 500 Expert, and Master above that.
- The spell's rank is the highest effect rank. The half-cost perk is `perks[school][rank]` (for example `DestructionAdept50`).

### 1.10 `data/content/attributes.json`

```json
{"attributes": [{"index": 0, "name": "Strength", "nameKey": "$LA_Attr_Strength",
   "stats": [{"stat": "CarryWeight", "perPoint": 3}, {"stat": "MeleeDamagePercent", "perPoint": 0.5}]}],
 "effects": ["mw.damage_attribute", ...], "profiles": {"Default": 1.0, "Light": 0.5, "Off": 0.0}, "singleEffectAttributes": ["Luck"]}
```

- Stat vocabulary: CarryWeight, MeleeDamagePercent, Magicka, MagickaRatePercent, MagicResist, AttackSpeedPercent, EvasionPercent, SpeedMult, Health, StaminaRatePercent, Speech, PricePercent, PickpocketPercent.
- The Light profile is computed in code as half of Default. `profiles` is informational.
- `effects` lists the attribute-target catalog effects, and the validator checks it matches the catalog.

### 1.11 `data/content/skills.json`

```json
{"skyrimSkills": [{"index": 0, "name": "OneHanded", "nameKey": "$LA_Skill_OneHanded", "actorValue": "OneHanded"}],
 "morrowindSkills": [{"index": 0, "sub": 100, "name": "Block", "nameKey": "$LA_MwSkill_Block", "targets": [{"skill": "Block", "weight": 1.0}]}],
 "specialTargets": ["Jump","RunSwimSpeed","UnarmedDamage","Unarmored"], "effects": ["mw.drain_skill", "mw.fortify_skill", "mwx.damage_skill", ...]}
```

### 1.12 Translations

- `data/translations/LostArt_ENGLISH.src.tsv` is the UTF-8 source: `key<TAB>value`, `#` comments.
- `LostArt_ENGLISH.txt` is generated from it and must not be hand-edited.
- Key families:
  - `$LA_Effect_*`, `$LA_EffectSk_*`, `$LA_EffectFmt_*` (value contains `{0}` for the attribute or skill), `$LA_Card_*`.
  - `$LA_Attr_*`, `$LA_Skill_*`, `$LA_MwSkill_*`.
  - `$LA_Msg_<GMST>` for the 8 Morrowind messages, verbatim, and `$LA_Msg_<Name>` for the 5 messages beyond Morrowind: SpellbookFull, AltarMembersOnly, SpellmakerMembersOnly, DuplicateName, TooComplex.
  - `$LA_Fmt_*`, `$LA_Unit_*` (`percent` is `%`, appended directly), `$LA_Range_*`, `$LA_Rank_*`, `$LA_School_*`.
  - `$LA_UI_*`, including cost-model names, control hints and `$LA_UI_Counter` = `{0}/{1}`.
  - `$LA_MCM_*`: page names, every CONTRACTS §5 setting and its option values.
  - `$LA_StandIn_*`, `$LA_Tome_*`, `$LA_TomeSpell_*`, `$LA_Book_*`, `$LA_Refuse_*`, `$LA_Topic_MakeSpell`, `$LA_Altar_*`.

## 2. Sources

- **Morrowind effect indices, ranges and flags:** OpenMW `components/esm3/loadmgef.cpp`, fetched 2026-09-30. It gives the `HardcodedFlags` table (ranges, NoDuration, NoMagnitude, Harmful, Unreflectable, target attribute or skill) and `getMagnitudeDisplayType` (units).
- **Morrowind base costs and schools:** UESP `Morrowind:Spell_Effects`, read through the API. All 132 `mw` and `mwx` base costs and schools match both UESP and the outline, and every outline range matches OpenMW's flags. **No outline value had to be overridden.**
- **Original Morrowind spell values for tomes:** UESP `Morrowind:<School>_Spells` (rendered HTML).
- **Vanilla Skyrim spell costs, magnitudes and durations:** the outline's cost table plus UESP `Skyrim:<School>_Spells` (skill-0 base cost). They agree on every spell in the outline table.
- **FormIDs:** Mutagen.Bethesda.FormKeys (`/opt/deps/formkeys`, commit 650e147). References are looked up by EditorID; the UESP IDs for Dremora ranks and quest EditorIDs were only used to find the EditorID.

## 3. Skyrim-balanced base costs

Formula (outline): `c = B × T^1.1 × (1 + A/16)`, where `T = M̄ × max(1, D)` for ticking effects and
`M̄ × max(D, 10)/10` for held effects, with `M̄ = max(1, (Mmin + Mmax)/2)`. Target costs × 1.5 unless
`vanillaTargetPriced` is set. The spell cost is `floor(Σc + 1e-9)`.

1. **Vanilla fit** (`fitFrom.kind = "vanilla"`, 32 Morrowind effects and all 12 `sk` effects):
   - `B = vanillaCost / (T^1.1 × (1 + A/16))`, using the vanilla spell's magnitude, duration and area at skill 0 with no perks. Magnitude-less effects use M̄ = 1, and instant spells use D = 0.
   - Fire is fitted to Firebolt: B = 41/25^1.1 = 1.1886386. That reproduces Firebolt 41, Fireball 133, the worked example 59 and "10 pts for 10 secs" 188.
   - `vanillaTargetPriced` is true where the fitting vanilla spell is aimed: Fire, Frost, Shock, Restore Health (Heal Other), Paralyze, Soul Trap, Calm, Fear, Fury, Courage, Turn Undead, and in `sk` Reanimate, Banish, Command and Magelight.
2. **Morrowind fit** (`kind = "morrowind"`, 87 Morrowind effects and all 13 `mwx` effects), for effects with no vanilla analogue:
   - `K = median over the vanilla fits of vanillaCost / ClassicSelfCost(same M, D, A)`, where ClassicSelfCost is Morrowind's C_i without the Target ×1.5. **K = 4.273211** (32 fits).
   - Then `B = K × ClassicSelfCost(ref) / (T_ref^1.1 × (1 + A_ref/16))`, where `ref` is the Morrowind spell of the tome named in `fitFrom.tome`.
   - In words: rebuilding the effect's original Morrowind spell costs K times its Classic cost, which is where a typical vanilla spell sits against its Classic rebuild. Target ×1.5 applies in both models for these effects, so the fit uses self costs on both sides.
   - Examples: Levitate 10 pts 30 s rebuilds at 199 magicka, Mark and Recall at 186, Divine and Almsivi Intervention at 80, and Cure Blight Disease at 1068.
3. `tools/calibrate.py` prints every fit. The validator fails if a stored `baseCost` drifts from the fit, or if any of the 4 Skyrim-balanced or 10 Classic golden numbers change.

Known quirks of the fit, left as data to tune in balance calibration:
- Rally is fitted from Courage (level cap 100 for 60 s costs 39), so B is only 0.034.
- Summon Dremora is fitted from Conjure Dremora Lord (358), although the creature is the mid-tier Kynval.
- Ticking restore effects fitted from Morrowind spells come out cheap per point, because Morrowind charges linearly for duration.

## 4. Decisions and departures

- **Counts match the outline exactly.** Morrowind effects: 119 = 65 native + 44 custom + 10 stand-in. By Morrowind school: Alteration 14, Conjuration 29, Destruction 21, Illusion 18, Mysticism 15, Restoration 22. Extended: 13. Skyrim-only: 12.
- **Skyrim-only set (12 effects):**
  - Muffle, Transmute, Detect Life, Detect Dead, Reanimate, Banish Daedra, Command Daedra, Flame Cloak, Frost Cloak, Lightning Cloak, Magelight, Conjure Ash Spawn.
  - Reanimate is one effect for the whole reanimation family, with M as the level cap.
  - Detect Life and Detect Dead are fitted from their per-second concentration cost, read as the cost of a 10 s timed effect.
- **Units** follow Morrowind (OpenMW display types):
  - Calm, Frenzy, Demoralize, Rally and Turn Undead show `pts`, although Skyrim reads M as a level cap. Command shows `level`.
  - Spell Absorption, Sanctuary and Sound show `pts`.
  - Fortify Maximum Magicka (`mwx`) is shown as `percent` because the Extended set implements it as magicka regeneration; Morrowind displayed "×INT".
- **Hostile and reflectable** come from Morrowind's flags. Calm, Frenzy, Demoralize, Rally, Charm and Command are not Harmful in Morrowind. The compiler may still need to treat Frenzy on NPCs as assault under Skyrim's crime rules.
- **Area** is available on Touch and Target for every effect, which is Morrowind's rule. The outline doesn't list area per effect.
- **Tomes:** 66 Morrowind tomes plus 9 rare Extended tomes, against the outline's "about 50".
  - Most tomes are one original Morrowind spell with its original values.
  - Six are groupings (`original: false`, originals in `valuesFrom`): Bound Armor, Armor and Weapon Eater, Dire Weakness, Vital Gifts, Great Elemental Resistance and Healer's Wards.
  - The Extended tomes have invented titles and values (`valuesFrom: []`), except Dwemer Animunculi.
  - Tomes also cover effects that already have a vanilla source, where the outline lists them: Light, Water Breathing, Telekinesis.
- **Rank ladders** that are not in the outline:
  - Frost and Shock reuse Fire's 25/40/60/100.
  - Restore Health is 50/100/200 (Fast Healing, Close Wounds, Grand Healing).
  - Calm, Frenzy, Demoralize and Rally use levels 9/14/20/25 (Calm and Fear 9, Frenzy 14, Pacify and Rout 20, Harmony, Hysteria and Mayhem 25).
  - Turn Undead is 6/8/16/30.
  - Magnitude-less effects are pinned to their vanilla spell's rank: Invisibility and Paralyze Expert, Water Breathing Adept, Soul Trap Apprentice, and each bound weapon or summon at its vanilla level.
- **Stand-in level scales:** Hunger (Lurker) 0.6 and Bonewolf (Death Hound, Extended) 0.8. The outline only says "scaled down".
- **Telekinesis:** Skyrim's Telekinesis spell teaches `mw.telekinesis` (the reach effect). This is a judgement call, and the tome teaches it too.
- **Unmapped perk riders:** perk-conditioned riders in vanilla spells, such as Respite's restore stamina, are not mapped by discovery, because they only fire with the perk.

## 5. Low confidence: verify in xEdit, the CK or the game

1. `riders.json` entries marked `verify: true`:
   - ElementalPotency and MysticBinding may be perk-conditioned replacements rather than riders.
   - The Mystic Binding dagger variant is `DLC2BoundSwordMysticFFSelf`.
   - Soul Stealer may live on the bound weapons' enchantment.
   - FrostStaminaSlow and ShockMagicka: if the vanilla frost and shock MGEFs are DualValueModifiers, the stamina or magicka part lives in the primary effect instead.
2. `skyrim.vanillaEffect` "modelled on" choices use potion or enchantment MGEFs for several effects, for example `AlchResistFire`, Water Walking's Dragonborn `AlchWaterWalking`, `EnchAbsorbHealthFFContact` and `RaceBretonAbsorbSpellChance`.
3. Night Eye as a ValueModifier on the `NightEye` actor value.
4. Muffle's actor value `MovementNoiseMult`.
5. Summon keywords: MagicSummonUndead on the Soul Cairn stand-ins, Ancestral Ghost and Skeletal Minion.
6. Altars:
   - Placeholder positions for both altars.
   - `DLC2TelMithryn` taken as Neloth's tower interior.
7. Elder Othreloth's refusal stage: Clean Sweep `DLC2RRFavor03` stage 200. UESP lists stages 10, 20, 30, 200 and 250 without completion text.
8. `merchantChest` holds base CONT records, not placed references.
9. Invented ladders:
   - Night Eye 34/67 and Light 25/50 (strength tiers).
   - Fortify Health, Magicka and Fatigue 25/50/100 (Courage, Rally and Call to Arms).
10. Discovery `example` labels are informational; some were left out where the spell pairing was uncertain.
