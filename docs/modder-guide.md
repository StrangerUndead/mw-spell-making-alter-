# Lost Art of Spellmaking — Modder Guide

The mod has three data-driven extension points, a Papyrus API and two mod events. Nothing here
requires editing the mod's own files. You drop your files beside ours:

| Folder | What goes there |
| --- | --- |
| `Data/SKSE/Plugins/LostArt/` | JSON data, read once at game start (kDataLoaded) |
| `Data/Interface/Translations/` | Translation files (`LostArt_<LANGUAGE>.txt`) |
| `Data/Scripts/` | Papyrus |

The exact file shapes are documented in [data-notes.md](data-notes.md). The JSON Schemas live in
`data/schema/` in the source repository.

## 1. Teaching the spellmaker about your spells (discovery packs)

The spellmaker offers the effects of spells the player knows. It maps each magic effect to one of
our catalog effects in two ways:
- **Lookup table:** an exact MGEF → effect id.
- **Rules:** these match the archetype, actor value, resist, keywords, flags and delivery. The first match wins.

To map your mod's effects, add files named `discovery/vanilla_<yourpack>.json` or
`discovery/rules_<yourpack>.json`. They load after ours, in file-name order.

```json
{ "map": [ { "effect": "MyMagic.esp|0x000812", "id": "mw.fire_damage", "label": "MyFireballEffect" } ] }
```

```json
{ "rules": [ { "id": "mw.frost_damage",
               "match": { "archetype": "ValueModifier", "actorValue": "Health",
                          "keywordsAny": ["MyMagic_FrostKeyword"], "hostile": true } } ] }
```

Lookup entries add to ours: one MGEF may unlock several ids. Rules are appended after ours, so
they catch what our rules don't.

Effects that match nothing and are fire-and-forget can still **pass through** at their own delivery
when *Pass-through modded effects* is on (the default). The player can edit their magnitude,
duration and area, and they price from their own base cost. Concentration and target-location
effects stay hidden until v1.x.

## 2. New effects (one-file add-ons)

Add `effects/<yourpack>.json`: an array of effect definitions in the catalog format (data-notes
§1.1, CONTRACTS §3). Point `variantForms` at **your own** magic effects, one per range. You don't
need generated `LA_` records:

```json
[{
  "id": "mw.ice_storm", "name": "$MyPack_IceStorm", "set": "mw", "tier": "native",
  "morrowind": { "school": "Destruction", "baseCost": 6 },
  "skyrim": { "school": "Destruction", "baseCost": 1.4, "vanillaTargetPriced": true },
  "ranges": ["touch", "target"],
  "magnitude": { "has": true, "unit": "pts", "ticking": true },
  "duration": true, "area": true, "hostile": true,
  "variantForms": { "touch": "IceStorm.esp|0x000801", "target": "IceStorm.esp|0x000802" },
  "sources": ["IceStorm.esp|0x000810"]
}]
```

Rules for the MGEFs you point at:
- **Casting type:** Fire and Forget.
- **Delivery:** Self for `self`, Aimed for `touch` and `target`. For Touch, reuse our `LA_TouchProjectile` or your own short-range projectile.
- **Magnitude:** the compiler writes the maximum magnitude into the effect item and rolls between min and max at cast time.
- **Area:** the plugin resolves area itself, so leave the effect item's area at 0.
- **Name:** ship the name key in `Interface/Translations/LostArt_<YourPack>_ENGLISH.txt`, UTF-16 LE with a BOM. The DLL builds the effect lines itself and reads every `LostArt_*_<LANGUAGE>.txt` after its own file; if the key is missing, the key itself shows.
- **Rules still apply:** the Morrowind rules (8 effects, duplicates, range cycling, caps) apply to your effect automatically.

A spell or tome in `sources` teaches the effect. Players learn it by knowing a spell that uses one
of your MGEFs, plus a discovery lookup entry (section 1) that maps that MGEF to your id.

Effects with an attribute or skill target (the picker families) need generated variants per
target, and aren't supported from add-ons in v1.0.

## 3. Rider packs (perk overhauls)

Keyword perks (Augmented Flames, Twin Souls, Mage Armor, Regeneration) work automatically,
because our effects carry the vanilla keywords.

Other perks work through hidden effects embedded in vanilla spells: Intense Flames, Deep Freeze,
Disintegrate and Impact. We append those as riders (`content/riders.json`). An overhaul whose perks
use their own embedded effects can ship a rider list. Each rider names the MGEF to append, with
optional per-delivery variants:

```json
{ "riders": [ { "id": "MyPerk_Chill", "effect": "MyPerks.esp|0x000900",
                "variants": { "aimed": "MyPerks.esp|0x000901", "aimedArea": "MyPerks.esp|0x000902" },
                "elemental": false, "magnitudeScale": 1.0 } ] }
```

For now, rider packs replace `content/riders.json`. Merge your entries into a copy of ours, and
reference the rider ids from the effects that should carry them. Every spell is capped at
15 effects, riders included; overflow moves into a linked sub-spell.

## 4. Stand-ins

`standins/stand-ins.json` maps each Morrowind summon to the Skyrim creature that stands in for it.
A creature pack with original models can point these entries at its own actors:

```json
{ "standins": [ { "effect": "mw.summon_scamp", "creature": "ScampPack.esp|0x000801",
                  "nameKey": "$ScampPack_Scamp", "levelScale": 1.0, "credit": "$LA_StandIn_Scamp" } ] }
```

## 5. Papyrus API

Script `LostArt` (all functions are global and native):

```papyrus
Function OpenSpellmaking(ObjectReference akProvider)   ; altar or spellmaker; None = sandbox
bool     IsCustomSpell(Spell akSpell)
int      GetCustomSpellCount()
int      GetFreeSlotCount()
int      GetFreeSubSlotCount()
bool     DeleteCustomSpell(Spell akSpell)
Function SetAltarActive(ObjectReference akAltar, bool abActive)
int      GetRefusalReason(Actor akSpellmaker)          ; 0 serves, 1 not a member, 2 wanted,
                                                       ; 3 quest, 4 vampire, 5 relationship, 6 disabled
Function ReloadSettings()
int      RebalanceAll()                                ; spells changed, < 0 on failure
int      RebuildAllSlots()
bool     PrepareForUninstall()
string   GetVersion()
int      RunTests(string asSuite)                      ; failures; < 0 unknown suite
```

Mod events (register with `RegisterForModEvent`):

- `LostArt_SpellCreated`, sent with `Form akSpell`
- `LostArt_SpellDeleted`, sent with `Form akSpell`
- `LostArt_SettingsChanged`: string argument is the MCM id (for example `iCostModel:Costs`)

A future questline can gate the altars with `SetAltarActive` and the spellmakers with the global
`LA_SpellmakersEnabled`. No core change is needed.

## 6. Records you can rely on

Every record in `LostArt.esp` and `LostArt_Slots.esp` has a stable FormID, keyed to its EditorID.
The mapping ships as `SKSE/Plugins/LostArt/formmap.json`. Custom spells always live in the
spell records `LA_Slot_000`–`LA_Slot_499` and `LA_Sub_000`–`LA_Sub_499`, and carry the
keyword `LA_KW_CustomSpell`.

Don't patch slot records. The DLL rewrites them on every load.

## 7. Building from source

See [dev/BUILDING.md](dev/BUILDING.md):

| What | How |
| --- | --- |
| Core tests on Linux | `cmake --preset linux-tests && cmake --build --preset linux-tests && ctest --preset linux-tests` |
| The DLL | preset `windows-msvc`, or `linux-clangcl` to cross-compile |
| The plugins | `dotnet run --project tools/Generator -- --data data --out build/plugins` |
| The SWF | `interface/build.sh` |
| The scripts | `scripts/build.sh` |
| The release | `python3 tools/package.py` |
