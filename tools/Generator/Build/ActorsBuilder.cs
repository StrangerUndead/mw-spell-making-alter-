using System.Text.Json.Nodes;
using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;
using Mutagen.Bethesda.Plugins.Records;

namespace LostArt.Generator.Build;

/// <summary>Stand-in actors (NPC_), bound weapons (WEAP) and bound armor (ARMO).</summary>
public sealed class ActorsBuilder(BuildContext ctx)
{
    /// <summary>effect id -> generated association (stand-in NPC or bound item).</summary>
    private readonly Dictionary<string, FormKey> _associations = new(StringComparer.Ordinal);

    private Log Log => ctx.Log;

    // ---- bound items: vanilla Daedric look, stats scaled like vanilla bound weapons ---------------
    private sealed record BoundWeaponSpec(string Item, string Model, string Template, WeaponAnimationType Anim, ushort Damage, float Speed,
        float Reach, float Stagger, ushort Crit, Skill Skill, string EquipType, string TypeKeyword, string Impact, string BlockImpact, string BlockMaterial);

    private sealed record BoundArmorSpec(string Item, string Addon, string Template, string MaleModel, BipedObjectFlag Slots, float Rating, string TypeKeyword);

    // Damage = Daedric damage x ~0.65, the ratio of vanilla Bound Sword (9) to Daedric Sword (14).
    private static readonly Dictionary<string, BoundWeaponSpec> BoundWeapons = new(StringComparer.Ordinal)
    {
        ["BoundDagger"] = new("Dagger", @"Weapons\Daedric\DaedricDagger.nif", "DaedricDagger", WeaponAnimationType.OneHandDagger, 7, 1.3f, 0.7f, 0f, 3,
            Skill.OneHanded, "EitherHand", "WeapTypeDagger", "WPNzBlade1HandImpactSet", "WPNBlockBlade1HandImpactSet", "MaterialBlockBlade1Hand"),
        ["BoundMace"] = new("Mace", @"Weapons\Daedric\DaedricMace.nif", "DaedricMace", WeaponAnimationType.OneHandMace, 11, 0.8f, 1f, 0.75f, 5,
            Skill.OneHanded, "EitherHand", "WeapTypeMace", "WPNzBluntImpactSet", "WPNBlockBlunt1HandImpactSet", "MaterialBlockBlunt"),
        // Skyrim has no spears: a bound Daedric greatsword stands in (OUTLINE, Conjuration table).
        ["BoundSpear"] = new("Spear", @"Weapons\Daedric\DaedricGreatsword.nif", "DaedricGreatsword", WeaponAnimationType.TwoHandSword, 16, 0.75f, 1.3f, 1.1f, 8,
            Skill.TwoHanded, "BothHands", "WeapTypeGreatsword", "WPNzBlade2HandImpactSet", "WPNBlockBlade2HandImpactSet", "MaterialBlockBlade2Hand"),
    };

    // Vanilla bound weapons for the native effects that Skyrim already has.
    private static readonly Dictionary<string, string> VanillaBound = new(StringComparer.Ordinal)
    {
        ["BoundBattleAxe"] = "BoundWeaponBattleaxe",
        ["BoundLongsword"] = "BoundWeaponSword",
        ["BoundLongbow"] = "BoundWeaponBow",
    };

    private static readonly Dictionary<string, BoundArmorSpec> BoundArmor = new(StringComparer.Ordinal)
    {
        ["BoundBoots"] = new("Boots", "DaedricBootsAA", "ArmorDaedricBoots", @"Armor\Daedric\DaedricBootsGND.nif", BipedObjectFlag.Feet, 18f, "ArmorBoots"),
        ["BoundCuirass"] = new("Cuirass", "DaedricCuirassAA", "ArmorDaedricCuirass", @"Armor\Daedric\DaedricCuirassGND.nif", BipedObjectFlag.Body, 49f, "ArmorCuirass"),
        ["BoundGloves"] = new("Gloves", "DaedricGlovesAA", "ArmorDaedricGauntlets", @"Armor\Daedric\DaedricGauntletsGND.nif", BipedObjectFlag.Hands, 18f, "ArmorGauntlets"),
        ["BoundHelm"] = new("Helm", "DaedricHelmetAA", "ArmorDaedricHelmet", @"Armor\Daedric\DaedricHelmetGND.nif", BipedObjectFlag.Head | BipedObjectFlag.Hair, 23f, "ArmorHelmet"),
        ["BoundShield"] = new("Shield", "DaedricShieldAA", "ArmorDaedricShield", @"Armor\Daedric\DaedricShieldGND.nif", BipedObjectFlag.Shield, 36f, "ArmorShield"),
    };

    /// <summary>
    /// Associations the effect schema has no field for (cloak spell, light) or that the catalog
    /// leaves empty; each use is reported as a warning so the data can take them over.
    /// </summary>
    private static readonly Dictionary<string, (string Type, string EditorId)> FallbackAssociations = new(StringComparer.Ordinal)
    {
        ["sk.conjure_ash_spawn"] = ("Npc", "DLC2SummonAshSpawn01"),
        ["sk.flame_cloak"] = ("Spell", "FlameCloakDmg"),
        ["sk.frost_cloak"] = ("Spell", "FrostCloakDmg"),
        ["sk.lightning_cloak"] = ("Spell", "LightningCloakDmg"),
        ["mw.light"] = ("Light", "MagicLightLightSpell01"),
        ["sk.magelight"] = ("Light", "LightSpellLightStatic"),
    };

    /// <summary>New actors for native summons without a vanilla creature (OUTLINE: "New Dunmer ancestor ghost built from vanilla ghost assets").</summary>
    private static readonly Dictionary<string, (string EditorId, string TemplateType, string Template)> FallbackActors = new(StringComparer.Ordinal)
    {
        ["mw.summon_ancestral_ghost"] = ("LA_StandIn_SummonAncestralGhost", "Npc", "LvlBanditGhostMelee1HMale"),
    };

    public void Build()
    {
        BuildStandIns();
        BuildBoundItems();
    }

    public FormKey? AssociationFor(EffectDef e)
    {
        if (_associations.TryGetValue(e.Id, out var fk)) return fk;
        if (!string.IsNullOrWhiteSpace(e.Association))
            return ctx.Resolve(e.Association, $"{e.Id} skyrim.creature/weapon/armor", "Npc", "LeveledNpc", "Weapon", "Armor", "Light", "Keyword", "Spell");
        if (FallbackAssociations.TryGetValue(e.Id, out var fb))
        {
            ctx.Log.Warn($"{e.Id}: no association in data; using built-in fallback {fb.Type} {fb.EditorId}");
            ctx.Verify($"MGEF {e.Pascal}: association {fb.Type} {fb.EditorId} is a generator fallback (no schema field) - confirm it matches the vanilla effect {e.VanillaEffect}");
            return ctx.Vanilla_(fb.Type, fb.EditorId);
        }
        if (VanillaBound.TryGetValue(e.Pascal, out var vb)) return ctx.Vanilla_("Weapon", vb);
        return null;
    }

    /// <summary>Map of effect id -> generated association EditorID (for variants.json).</summary>
    public IReadOnlyDictionary<string, string> GeneratedAssociations =>
        _associations.ToDictionary(kv => kv.Key, kv => ctx.Records.Values.First(r => r.FormKey == kv.Value).EditorID!);

    // ------------------------------------------------------------------------------------
    // Stand-in actors
    // ------------------------------------------------------------------------------------

    public static string StandInEditorId(JsonObject entry, EffectDef? e)
    {
        var explicitId = entry.Str("actor", "editorId", "editorID", "edid");
        if (explicitId is not null && explicitId.StartsWith("LA_StandIn_")) return explicitId;
        var p = e?.Pascal ?? Naming.SnakeToPascal((entry.Str("effect") ?? "unknown").Split('.').Last());
        return "LA_StandIn_" + p;
    }

    private void BuildStandIns()
    {
        foreach (var entry in ctx.Data.StandIns.OrderBy(s => s.Str("effect") ?? "", StringComparer.Ordinal))
        {
            var effectId = entry.Str("effect", "effectId", "id");
            if (effectId is null) { Log.Error("standins: entry without 'effect'"); continue; }
            ctx.Data.EffectsById.TryGetValue(effectId, out var e);
            if (e is null) { Log.Warn($"standins: {effectId} is not in the effect catalog; skipped"); continue; }
            var context = $"standins:{effectId}";
            var kind = entry.Str("kind") ?? (entry.Str("creature") is not null ? "creature" : "weapon");
            if (kind != "creature") continue; // weapon stand-ins (Bound Spear) are built with the bound items

            var template = ctx.Resolve(entry.Str("creature", "npc", "template"), $"{context} creature", "Npc", "LeveledNpc");
            if (template is null) continue;
            var nameKey = entry.Str("nameKey", "name");
            var npc = MakeActor(StandInEditorId(entry, e), e, template.Value, nameKey, entry.Str("race"), entry.Num("levelScale", "levelMult") ?? 1.0, entry.Num("scale", "height"), context);
            _associations[e.Id] = npc.FormKey;
        }

        foreach (var (effectId, fb) in FallbackActors)
        {
            if (!ctx.Data.EffectsById.TryGetValue(effectId, out var e) || _associations.ContainsKey(effectId) || !string.IsNullOrWhiteSpace(e.Association)) continue;
            ctx.Log.Warn($"{effectId}: no skyrim.creature; generating {fb.EditorId} templated on {fb.Template}");
            ctx.Verify($"NPC_ {fb.EditorId}: new actor templated on vanilla {fb.Template} (generator fallback) - pick/retune the ghost template and add the ghost look in CK");
            var npc = MakeActor(fb.EditorId, e, ctx.Vanilla_(fb.TemplateType, fb.Template), e.NameKey, null, 1.0, null, effectId);
            _associations[e.Id] = npc.FormKey;
        }

        if (_associations.Count > 0)
            ctx.Verify("NPC_ LA_StandIn_*: templated on the vanilla creature with every template flag except Base Data (so the stand-in keeps its own name); " +
                       "Race is DefaultRace as a placeholder (Traits come from the template) - open once in CK to confirm no template warnings; " +
                       "levelScale is stored as PC level mult but ignored while Stats are templated - scale in the DLL or untick Stats in CK (Lurker 0.6, Death Hound 0.8)");
    }

    private Npc MakeActor(string edid, EffectDef e, FormKey template, string? nameKey, string? raceRef, double levelScale, double? height, string context)
    {
        var name = ctx.Tr.Resolve(nameKey, "");
        if (name.Length == 0) name = ctx.Tr.Get(e.NameKey) ?? EffectBuilder.SplitPascal(e.Pascal);
        if (name.StartsWith("Summon ", StringComparison.Ordinal)) name = name["Summon ".Length..];
        var race = raceRef is null ? ctx.Vanilla_("Race", "DefaultRace") : ctx.Resolve(raceRef, $"{context} race", "Race") ?? ctx.Vanilla_("Race", "DefaultRace");
        return ctx.C(edid, fk => new Npc(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            Name = name,
            Race = new FormLink<IRaceGetter>(race),
            Template = new FormLinkNullable<INpcSpawnGetter>(template),
            Height = (float)(height ?? 1.0),
            Weight = 50f,
            Configuration = new NpcConfiguration
            {
                Flags = NpcConfiguration.Flag.Summonable | NpcConfiguration.Flag.AutoCalcStats,
                // Everything but Base Data comes from the vanilla creature, so the stand-in keeps
                // its own name while model, stats, AI, spells and inventory follow the template.
                TemplateFlags = NpcConfiguration.TemplateFlag.Traits | NpcConfiguration.TemplateFlag.Stats | NpcConfiguration.TemplateFlag.Factions
                                | NpcConfiguration.TemplateFlag.SpellList | NpcConfiguration.TemplateFlag.AIData | NpcConfiguration.TemplateFlag.AIPackages
                                | NpcConfiguration.TemplateFlag.ModelAnimation | NpcConfiguration.TemplateFlag.Inventory | NpcConfiguration.TemplateFlag.Script
                                | NpcConfiguration.TemplateFlag.DefPackList | NpcConfiguration.TemplateFlag.AttackData | NpcConfiguration.TemplateFlag.Keywords,
                Level = new PcLevelMult { LevelMult = (float)levelScale },
                CalcMinLevel = 1,
                CalcMaxLevel = 0,
                SpeedMultiplier = 100,
                DispositionBase = 35,
            },
            AIData = new AIData { Aggression = Aggression.Unaggressive, Confidence = Confidence.Average, EnergyLevel = 50, Responsibility = Responsibility.NoCrime, Mood = Mood.Neutral, Assistance = Assistance.HelpsAllies },
            PlayerSkills = new PlayerSkills(),
        });
    }

    // ------------------------------------------------------------------------------------
    // Bound items
    // ------------------------------------------------------------------------------------

    private void BuildBoundItems()
    {
        var byPascal = ctx.Data.Effects.GroupBy(e => e.Pascal).ToDictionary(g => g.Key, g => g.First(), StringComparer.Ordinal);
        var anyWeapon = false;
        foreach (var (pascal, spec) in BoundWeapons)
        {
            if (!byPascal.TryGetValue(pascal, out var e)) continue;
            // skyrim.weapon is either a vanilla bound weapon (used directly, e.g. Dragonborn's
            // DLC2BoundWeaponDagger) or the Daedric weapon the new bound item is modelled on.
            var template = string.IsNullOrWhiteSpace(e.Association) ? ctx.Vanilla_("Weapon", spec.Template) : ctx.Resolve(e.Association, $"{e.Id} skyrim.weapon", "Weapon");
            if (template is null) continue;
            if (ctx.Vanilla.Describe(template.Value) is { } d && d.EditorId.Contains("Bound", StringComparison.OrdinalIgnoreCase)) continue;
            anyWeapon = true;
            var over = ctx.Data.StandIns.FirstOrDefault(s => s.Str("effect") == e.Id);
            var model = over?.Str("model") ?? spec.Model;
            var edid = over?.Str("item") is { } item && item.StartsWith("LA_Bound_") ? item : $"LA_Bound_{spec.Item}";
            var name = ctx.Tr.Resolve(over?.Str("nameKey"), ctx.Tr.Resolve(e.NameKey, "Bound " + spec.Item));
            var w = ctx.C(edid, fk => new Weapon(fk, BuildContext.Release)
            {
                ObjectBounds = new ObjectBounds(),
                Name = name,
                Model = new Model { File = model },
                EquipmentType = new FormLinkNullable<IEquipTypeGetter>(ctx.Vanilla_("EquipType", spec.EquipType)),
                BlockBashImpact = new FormLinkNullable<IImpactDataSetGetter>(ctx.Vanilla_("ImpactDataSet", spec.BlockImpact)),
                AlternateBlockMaterial = new FormLinkNullable<IMaterialTypeGetter>(ctx.Vanilla_("MaterialType", spec.BlockMaterial)),
                Keywords = Links.Keywords(new[]
                {
                    ctx.Vanilla_("Keyword", spec.TypeKeyword), ctx.Vanilla_("Keyword", "WeapMaterialDaedric"),
                    ctx.Vanilla_("Keyword", "MagicDisallowEnchanting"), ctx.Vanilla_("Keyword", "VendorNoSale"),
                }),
                Description = "",
                ImpactDataSet = new FormLinkNullable<IImpactDataSetGetter>(ctx.Vanilla_("ImpactDataSet", spec.Impact)),
                BasicStats = new WeaponBasicStats { Value = 0, Weight = 0f, Damage = spec.Damage },
                Data = new WeaponData
                {
                    AnimationType = spec.Anim,
                    Speed = spec.Speed,
                    Reach = spec.Reach,
                    Flags = WeaponData.Flag.BoundWeapon | WeaponData.Flag.CantDrop,
                    SightFOV = 0f,
                    BaseVATStoHitChance = 0,
                    AttackAnimation = WeaponData.AttackAnimationType.Default,
                    NumProjectiles = 1,
                    EmbeddedWeaponAV = 0,
                    RangeMin = 0f,
                    RangeMax = 0f,
                    OnHit = WeaponData.OnHitType.NoFormulaBehavior,
                    AnimationAttackMult = 1f,
                    Unknown2 = 1061997773, // 0.8f, the value vanilla melee weapons carry
                    Skill = spec.Skill,
                    Resist = ActorValue.None,
                    Stagger = spec.Stagger,
                },
                Critical = new CriticalData { Damage = spec.Crit, PercentMult = 1f, Flags = 0 },
                DetectionSoundLevel = SoundLevel.Normal,
                Template = new FormLinkNullable<IWeaponGetter>(template.Value),
            });
            _associations[e.Id] = w.FormKey;
        }
        if (anyWeapon)
            ctx.Verify("WEAP LA_Bound_Dagger/Mace/Spear: Daedric mesh paths typed by hand (verify they exist and consider the bound-weapon shader/.nif used by BoundWeaponSword); " +
                       "damage scaled to ~0.65x Daedric like vanilla Bound Sword; Template (CNAM) points at the vanilla Daedric weapon; no swing/draw sounds set - copy from the Daedric weapon in CK");

        var anyArmor = false;
        foreach (var (pascal, spec) in BoundArmor)
        {
            if (!byPascal.TryGetValue(pascal, out var e)) continue;
            anyArmor = true;
            var template = string.IsNullOrWhiteSpace(e.Association) ? ctx.Vanilla_("Armor", spec.Template) : ctx.Resolve(e.Association, $"{e.Id} skyrim.armor", "Armor") ?? ctx.Vanilla_("Armor", spec.Template);
            var name = ctx.Tr.Resolve(e.NameKey, "Bound " + spec.Item);
            var isShield = spec.Slots.HasFlag(BipedObjectFlag.Shield);
            var a = ctx.C($"LA_Bound_{spec.Item}", fk => new Armor(fk, BuildContext.Release)
            {
                ObjectBounds = new ObjectBounds(),
                Name = name,
                WorldModel = new GenderedItem<ArmorModel?>(new ArmorModel { Model = new Model { File = spec.MaleModel } }, null),
                BodyTemplate = new BodyTemplate { FirstPersonFlags = spec.Slots, ArmorType = ArmorType.HeavyArmor, Flags = 0 },
                EquipmentType = isShield ? new FormLinkNullable<IEquipTypeGetter>(ctx.Vanilla_("EquipType", "Shield")) : new FormLinkNullable<IEquipTypeGetter>(),
                BashImpactDataSet = isShield ? new FormLinkNullable<IImpactDataSetGetter>(ctx.Vanilla_("ImpactDataSet", "WPNBashShieldHeavyImpactSet")) : new FormLinkNullable<IImpactDataSetGetter>(),
                Keywords = Links.Keywords(new[]
                {
                    ctx.Vanilla_("Keyword", spec.TypeKeyword), ctx.Vanilla_("Keyword", "ArmorHeavy"), ctx.Vanilla_("Keyword", "ArmorMaterialDaedric"),
                    ctx.Vanilla_("Keyword", "MagicDisallowEnchanting"), ctx.Vanilla_("Keyword", "VendorNoSale"),
                }),
                Description = "",
                Armature = new ExtendedList<IFormLinkGetter<IArmorAddonGetter>> { new FormLink<IArmorAddonGetter>(ctx.Vanilla_("ArmorAddon", spec.Addon)) },
                Value = 0,
                Weight = 0f,
                ArmorRating = spec.Rating,
                TemplateArmor = new FormLinkNullable<IArmorGetter>(template),
                MajorFlags = isShield ? Armor.MajorFlag.Shield : 0,
            });
            _associations[e.Id] = a.FormKey;
        }
        if (anyArmor)
            ctx.Verify("ARMO LA_Bound_*: worn look comes from the vanilla Daedric ArmorAddons (verified FormKeys); ground models (…GND.nif) are typed by hand - verify; " +
                       "armor rating = Daedric (Morrowind bound armor was Daedric) - balance check; weight 0; consider the NonPlayable flag so it cannot be dropped");
    }
}
