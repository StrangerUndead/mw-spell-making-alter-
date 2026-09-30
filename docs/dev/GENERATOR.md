# Plugin generator (tools/Generator)

`tools/Generator` is a C# console app (Mutagen) that builds both ESL-flagged plugins from the
JSON in `data/`. It is the only way records get into `LostArt.esp` and `LostArt_Slots.esp`;
`plugin/` holds their Spriggit YAML so every change is diffable. No vanilla record is edited
and no cell is touched: altars and spellmakers are wired up at runtime by the DLL.

## Commands

```sh
# generate (writes build/plugins/*.esp, data/generated/formmap.json and variants.json)
dotnet run --project tools/Generator -- --data data --out build/plugins [--formmap data/generated/formmap.json]

# serialize to plugin/LostArt and plugin/LostArt_Slots + round-trip check
tools/Generator/spriggit.sh build/plugins plugin
tools/Generator/spriggit.sh --check-only build/plugins plugin      # check committed YAML only

# tests (xunit)
dotnet test tools/Generator/tests

# keep records authored in the Creation Kit (see "Hand-made records")
spriggit deserialize -i plugin/LostArt -o /tmp/base/LostArt.esp
spriggit deserialize -i plugin/LostArt_Slots -o /tmp/base/LostArt_Slots.esp
dotnet run --project tools/Generator -- --data data --out build/plugins --base /tmp/base
```

Other options: `--prune` (retire formmap entries whose records are gone), `--no-formmap-write`
(dry run), `--generated <dir>` (where `variants.json` goes), `--quiet`,
`compare <a.esp> <b.esp>` (the round-trip comparer). The run exits non-zero and writes no
plugin when validation finds an error.

Toolchain: .NET SDK 9 or newer (Mutagen.Bethesda.Skyrim 0.54.4 targets net9.0/net10.0, so the
net8.0 SDK cannot build it; the project targets net9.0). Spriggit.CLI 0.41.0 is a net10.0 tool:
`dotnet tool install --global Spriggit.CLI --version 0.41.0` and a .NET 10 runtime (set
`DOTNET_ROOT` if dotnet is not installed system-wide; `spriggit.sh` does this for you). Vanilla
FormKeys come from the NuGet package Mutagen.Bethesda.FormKeys.SkyrimSE 3.4.0, so no game files
are needed.

## Outputs

| File | What |
| --- | --- |
| `build/plugins/LostArt.esp` | content plugin (ESL, header 1.7, form version 44, masters Skyrim/Dawnguard/Dragonborn as referenced) |
| `build/plugins/LostArt_Slots.esp` | 500 + 500 spell slots (ESL, master Skyrim.esm) |
| `data/generated/formmap.json` | EditorID -> local FormID per plugin (CONTRACTS §2), read by the DLL |
| `data/generated/formmap.retired.json` | ids of removed records (only after `--prune`); never reused |
| `data/generated/variants.json` | effect id -> range -> sub -> MGEF EditorID; `associations` (effect -> generated actor/bound item); `linkedSpells` (mixed-range tome spell -> sub-spells); `lootInjection` (generated LVLI -> vanilla list) |
| `plugin/LostArt/`, `plugin/LostArt_Slots/` | Spriggit YAML (Spriggit.Yaml.Skyrim 0.41.0) |

## FormID stability

- Every record is keyed by its EditorID in `formmap.json`. An existing id is never moved.
- A new EditorID gets the next id after the highest id ever used in that plugin (live or
  retired); only when 0xFFF is reached does it fall back to the lowest never-used gap.
- EditorIDs no longer generated stay in the map (warning) until `--prune` moves them to
  `formmap.retired.json`; retired ids are never handed out again (the same EditorID coming back
  gets its old id).
- On a fresh map the slots come first and are contiguous: `LA_Slot_000..499` = 0x800..0x9F3,
  `LA_Sub_000..499` = 0x9F4..0xBE7.
- Output is deterministic: records inside each group are sorted by EditorID the way Spriggit
  names its files (case-insensitive), so a Spriggit deserialize on Windows reproduces the
  plugin byte for byte.

## What is generated

### LostArt_Slots.esp (1,002 records)

| Record | EditorID | Notes |
| --- | --- | --- |
| SPEL x500 | `LA_Slot_000`..`499` | Spell, Fire and Forget, Self, cost override (ManualCostCalc) with cost 0, Either Hand, one effect `LA_BlankEffect` 0/0/0, keyword `LA_KW_CustomSpell`, name "Unwritten Spell" |
| SPEL x500 | `LA_Sub_000`..`499` | same, the shared sub-spell pool |
| MGEF | `LA_BlankEffect` | Script archetype, FF/Self, HideInUI, NoMagnitude/NoArea/NoDuration, Painless, NoHitEffect, cost 0 |
| KYWD | `LA_KW_CustomSpell` | |

### LostArt.esp (953 records with the current catalog)

| Record | EditorID | Source / rules |
| --- | --- | --- |
| MGEF x705 | `LA_<Pascal>_<Self\|Touch\|Target>`, `LA_<Pascal>_<Target>_<Range>` | one per effect x allowed range; attribute families x 8 attributes; skill families x 18 Skyrim skills + one custom variant per Morrowind skill whose `skills.json` targets include a non-skill (Athletics, Unarmored, Acrobatics, HandToHand) |
| KYWD | `LA_KW_Custom`, `LA_KW_Rider`, `LA_KW_Touch`, `LA_KW_Blight` | |
| PROJ | `LA_TouchProjectile` | invisible missile, range 192, speed 6000, L_SPELL |
| EXPL x15 | `LA_AreaFX_<School>_<Small\|Medium\|Large>` | damage 0, force 0, radius 128/320/640, vanilla impact set + sound per school |
| GLOB | `LA_ServiceRefusal` (0), `LA_AltarsEnabled` (1), `LA_SpellmakersEnabled` (1) | short |
| NPC_ x13 | `LA_StandIn_<Effect>` (`actor` in stand-ins.json) | templated on the vanilla creature |
| WEAP | `LA_Bound_Mace`, `LA_Bound_Spear` | new bound weapons (see below) |
| ARMO x5 | `LA_Bound_Boots/Cuirass/Gloves/Helm/Shield` | bound armor the DLL equips |
| SPEL x76 / BOOK x75 | `LA_TomeSpell_<Pascal>` / `LA_Tome_<Pascal>` | tomes.json (`<Pascal>` from the `$LA_Tome_` title key) |
| BOOK x3 | `LA_Book_FragmentsOnTheLostArt`, `LA_Book_SpellmakersPrimer`, `LA_Book_OnTelvanniInscription` | lore books |
| LVLI x28 | `LA_TomeLoot`, `LA_TomeLoot_<vanilla list>`, `LA_VendorTomes_<School>` | injected at runtime by the DLL |
| FURN | `LA_AltarCollege`, `LA_AltarTelvanni` | placed at runtime by the DLL |
| QUST | `LA_ServicesQuest` | start game enabled, run once, dialogue only |
| DLBR / DIAL | `LA_Branch_MakeSpell` / `LA_Topic_MakeSpell` | top-level player topic "I'd like to make a spell." |
| INFO x17 | `LA_Info_MakeSpell_Serve`, `LA_Info_MakeSpell_Refuse`, `LA_Info_MakeSpell_Refuse_<Spellmaker>` | see Dialogue |
| QUST | `LA_MCMQuest` | MCM Helper quest (see below) |

### Magic effect variants

- Casting type is always Fire and Forget. Self -> delivery Self; Touch -> Aimed with
  `LA_TouchProjectile` and keyword `LA_KW_Touch`; Target -> Aimed with `skyrim.projectile`, or a
  default: Firebolt / Ice Spike / Lightning Bolt projectile by damage keyword, otherwise per
  school (Alteration Paralyze/AlterPos, Conjuration Reanimate, Illusion IllusionNeg01/Illusion01,
  Restoration TurnUndead/HealFake, hostile/benign).
- Tier `custom`, attribute families and the special Morrowind-skill variants: Script archetype
  with keyword `LA_KW_Custom`; no primary AV. Native and stand-in: `skyrim.archetype` with
  `actorValue` (for skill families the skill), `secondAV`, `resist`.
- Associations: SummonCreature -> the generated stand-in actor or `skyrim.creature`; BoundWeapon
  -> `skyrim.weapon` when it is already a vanilla bound weapon (Bound Sword/Battleaxe/Bow,
  Dragonborn's `DLC2BoundWeaponDagger`), otherwise a new `LA_Bound_*` weapon modelled on it.
  The schema has no field for cloak spells and lights, so the generator falls back to a small
  table (warning printed): Flame/Frost/Lightning Cloak -> `FlameCloakDmg`/`FrostCloakDmg`/
  `LightningCloakDmg`, Light -> `MagicLightLightSpell01`, Magelight -> `LightSpellLightStatic`,
  Conjure Ash Spawn -> `DLC2SummonAshSpawn01`, and Summon Ancestral Ghost gets a new actor
  `LA_StandIn_SummonAncestralGhost` templated on `LvlBanditGhostMelee1HMale`.
- Flags: the data's `skyrim.flags`; `Hostile` when `hostile` (plus `Detrimental` only when the
  data lists no flags); `Recover` for held value modifiers (ValueModifier, PeakValueModifier,
  DualValueModifier, Absorb with duration and non-ticking magnitude), never for ticking ones;
  NoMagnitude / NoDuration / NoArea from the catalog (NoArea also on every Self variant);
  PowerAffectsMagnitude if the effect has magnitude, else PowerAffectsDuration.
- Base cost = `skyrim.baseCost`; magic skill = the Skyrim school; name = the English
  translation (`$LA_Effect_*`; target families substitute the attribute/skill name, e.g.
  "Fortify Strength", "Drain Sneak"). Keywords are vanilla keywords by EditorID (resolved through
  the FormKeys package) or `LA_KW_*`. ImpactData on Touch/Target from the element or school.

### Stand-ins and bound items

- `LA_StandIn_*` NPCs: `Template` = the vanilla creature, all template flags except Base Data (so
  the record keeps its own name, e.g. "Seeker" from `$LA_EffectSk_SummonClannfear`), Summonable.
  `levelScale` is stored as the PC level multiplier.
- New bound weapons copy the vanilla Daedric weapon: `Template` (CNAM) = that weapon, Daedric
  mesh path, animation/speed/reach/stagger of the type, damage x0.65 (vanilla Bound Sword vs
  Daedric Sword), weight/value 0, Bound Weapon + Can't Drop, WeapType + WeapMaterialDaedric +
  MagicDisallowEnchanting + VendorNoSale.
- Bound armor: vanilla Daedric ArmorAddon (worn look), `TemplateArmor` = the Daedric piece,
  Daedric armor rating, weight/value 0, heavy armor keywords + MagicDisallowEnchanting +
  VendorNoSale. The Bound Boots/Cuirass/... effects are custom (Script); the DLL equips the item.

### Tomes and leveled lists

- One BOOK (teaches spell) + one SPEL per tome. The spell uses the tome's `skyrimCost` as cost
  override, the school's rank perk as half-cost perk, and the midpoint of min..max as magnitude
  (Skyrim spells have one magnitude); no-magnitude value effects get 1.
- A spell has one delivery, so a mixed-range tome (currently `skill_leech`) becomes a primary
  spell (farthest range, carries the cost) plus `LA_TomeSpell_<Pascal>_<Range>` sub-spells at
  cost 0, listed in `variants.json` `linkedSpells` for the DLL to cast on release.
- `LA_TomeLoot`: all `loot: true` tomes, level by rank (1/5/10/20/30).
  `LA_TomeLoot_<vanilla list EditorID>`: the tomes each vanilla list in `lootLists` should get;
  `variants.json` `lootInjection` maps each to its vanilla list. `LA_VendorTomes_<School>`: tomes
  whose `vendors` include that (Morrowind) school, Use All; the DLL adds them to the merchant
  chests of spellmakers with that specialty.

### Dialogue

`LA_Topic_MakeSpell` sits in `LA_ServicesQuest` behind a top-level player branch. No INFO has a
script fragment: the DLL reacts to `TESTopicInfoEvent` for `LA_Info_MakeSpell_Serve` and opens
the menu; before the topic list is built it sets `LA_ServiceRefusal` (CONTRACTS §8 codes) for
the current speaker.

| INFO | Conditions (Subject) | Line |
| --- | --- | --- |
| `LA_Info_MakeSpell_Serve` (Goodbye) | GetIsID = spellmaker 1 OR ... OR spellmaker 15, AND GetGlobalValue LA_SpellmakersEnabled == 1, AND LA_ServiceRefusal == 0 | "Very well. Tell me what you have in mind." |
| `LA_Info_MakeSpell_Refuse_<Spellmaker>` x15 | GetIsID = that spellmaker, AND LA_SpellmakersEnabled == 1, AND 0 < LA_ServiceRefusal < 4 | the spellmaker's `refusalLine` |
| `LA_Info_MakeSpell_Refuse` | the OR chain, AND LA_SpellmakersEnabled == 1, AND LA_ServiceRefusal >= 4 | generic refusal (reasons 4 vampire, 5 relationship) |

The conditions are mutually exclusive, so INFO order does not matter. Reason 6 (service
disabled) is expressed by `LA_SpellmakersEnabled = 0`, which hides the topic.

`LA_MCMQuest`: start game enabled (run once off), VMAD script `LostArt_MCM` (no properties),
reference alias `PlayerAlias` (forced reference PlayerRef `Skyrim.esm|0x000014`) with script
`SKI_PlayerLoadGameAlias`. The script names survive the Spriggit round trip.

## Validation (every run)

- ESL: every local FormID in 0x800-0xFFF, at most 2,048 records per plugin, ESL flag, not
  localized, header 1.7, form version 44, masters in load order; re-read from disk after writing.
- No overrides: every record belongs to its own plugin; the two plugins do not reference each
  other; every link to a generated record resolves; every vanilla link exists in
  Mutagen.Bethesda.FormKeys.SkyrimSE (plus the hard-coded Player/PlayerRef) and comes from an
  allowed master.
- Every effect x range (x target) of the catalog compiled (count checked against the plan).
- Casting type/delivery: every spell's effects share its casting type and delivery; every
  generated MGEF is Fire and Forget; Aimed effects have a projectile; Touch uses
  `LA_TouchProjectile`.
- Data sanity: ranges, base cost > 0, sources present, translation keys present.
- Record counts per plugin and type are printed.

## Spriggit round trip

`spriggit.sh` serializes each plugin with `-u` (error on unknown fields) into a fresh folder,
replaces `plugin/<name>`, deserializes it again and compares with the generated plugin.
Spriggit deserializes records in directory-enumeration order; on Linux the script stages the
YAML on tmpfs in alphabetical order (what NTFS gives on Windows), and the result is
byte-identical for both plugins. Without tmpfs the comparer still proves equivalence
(identical header, groups and record bytes; only in-group order may differ).

## Hand-made records (`--base`)

OUTLINE step 3 keeps Creation Kit work in the same plugins. With `--base <dir>`, the previous
plugins (deserialized from `plugin/`, then edited in CK) are read first: their FormIDs are
reserved in the formmap, generated EditorIDs are rebuilt from data, and every other top-level
record is copied over unchanged. Nested records (INFOs added to our topic, placed references)
are not carried over and produce a warning. Vanilla overrides in the base are errors.

## Tests (`tools/Generator/tests`, xunit)

Byte-identical regeneration with the same formmap; existing ids never move and new ids take
the next free one; retired ids are not reused; ESL range/header/form version; the 1,000 slots;
variant count equals the catalog-derived expectation (fixture and, when present, the real
`data/`); range/flag/archetype rules; bound items, mixed-range tome, INFO set and MCM quest;
hand-made records survive regeneration; the plugin comparer. Fixture data lives in
`tools/Generator/tests/fixtures/data`.

## What still needs the Creation Kit or an in-game check

Nothing is placed by the generator: the DLL places the altars at runtime (altars.json), adds
the dialogue-free service via the INFO event, and injects tomes into loot and vendor chests, so
no CELL, NAVM or vanilla list is edited. The generator prints this list on every run:

- MGEF variants: no casting/hand art, hit shader/art, sounds or light - copy from
  `skyrim.vanillaEffect` in the DLL at data load, or an art pass in CK. Flags are derived
  (Recover, PowerAffects*, NoArea) - spot-check against the vanilla effects.
- Summon and bound variants use Self delivery - confirm the summon appears beside the caster.
- Generator fallback associations (cloak spells, Light/Magelight lights, Ash Spawn, Ancestral
  Ghost template) - confirm each matches the vanilla effect it is modelled on.
- `LA_TouchProjectile`: no model; confirm an invisible Aimed missile flies and hits doors and
  containers; range 192 (the DLL may override from iTouchReach).
- `LA_AreaFX_*`: explosion mesh paths are typed by hand - verify they exist; tint and per-size
  scale need an art pass.
- `LA_StandIn_*`: Race is a DefaultRace placeholder (Traits come from the template); open once in
  CK; `levelScale` is ignored while Stats are templated (Lurker 0.6, Death Hound 0.8).
- `LA_StandIn_SummonAncestralGhost`: pick the final ghost template and look.
- `LA_Bound_*` weapons: mesh paths typed by hand, damage balance, no swing/draw sounds (copy from
  the Daedric weapon), consider the bound-weapon shader.
- `LA_Bound_*` armor: ground models typed by hand; Daedric armor ratings (balance); consider
  NonPlayable.
- Tome and lore book models typed by hand; lore book text is a placeholder until
  `$LA_Book_<Name>_Text` keys (or `docs/lore/<EditorID>.txt`) exist.
- Altars: enchanting-table mesh and one front marker, BenchType None; confirm the enchanting
  approach/idle animation plays, then swap in the College and Telvanni altar meshes.
- Dialogue: silent lines without voice files; optionally point ResponseData at each NPC's shared
  merchant/trainer lines in CK; open the quest once so CK builds its dialogue view.
- `LA_TomeSpell_SkillLeech`: the DLL must cast `LA_TomeSpell_SkillLeech_Self` on release
  (`linkedSpells`).
