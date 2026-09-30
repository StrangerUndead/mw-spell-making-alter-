using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using ArchetypeType = Mutagen.Bethesda.Skyrim.MagicEffectArchetype.TypeEnum;

namespace LostArt.Generator.Build;

/// <summary>One MGEF variant to generate: effect x range (x attribute/skill target).</summary>
public sealed record VariantSpec(EffectDef Effect, string Range, string? TargetName, int Sub, bool SpecialTarget, string EditorId);

/// <summary>Keywords, the touch projectile, area visuals and all MGEF variants of LostArt.esp.</summary>
public sealed class EffectBuilder(BuildContext ctx)
{
    public const string KwCustom = "LA_KW_Custom";
    public const string KwRider = "LA_KW_Rider";
    public const string KwTouch = "LA_KW_Touch";
    public const string KwBlight = "LA_KW_Blight";
    public const string TouchProjectile = "LA_TouchProjectile";
    public static readonly string[] AreaSizes = { "Small", "Medium", "Large" };

    private Log Log => ctx.Log;

    // ------------------------------------------------------------------------------------
    // Shared records
    // ------------------------------------------------------------------------------------

    public void BuildShared()
    {
        foreach (var kw in new[] { KwBlight, KwCustom, KwRider, KwTouch })
            ctx.C(kw, fk => new Keyword(fk, BuildContext.Release));

        // Touch: Skyrim's Contact delivery only works for weapons, so Touch variants are Aimed
        // and fire this short-range invisible missile (OUTLINE "Ranges": 192 units by default).
        ctx.C(TouchProjectile, fk => new Projectile(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            Type = Projectile.TypeEnum.Missile,
            Flags = 0,
            Gravity = 0f,
            Speed = 6000f,
            Range = 192f,
            ImpactForce = 0f,
            FadeDuration = 0f,
            Lifetime = 0f,
            CollisionRadius = 10f,
            TracerChance = 0f,
            ConeSpread = 0f,
            RelaunchInterval = 0f,
            MuzzleFlashDuration = 0f,
            ExplosionAltTriggerProximity = 0f,
            ExplosionAltTriggerTimer = 0f,
            SoundLevel = 2, // Silent (0 Loud, 1 Normal, 2 Silent, 3 VeryLoud)
            CollisionLayer = new FormLink<ICollisionLayerGetter>(ctx.Vanilla_("CollisionLayer", "L_SPELL")),
        });
        ctx.Verify("PROJ LA_TouchProjectile: no model (invisible) - confirm an Aimed missile without MODL flies and hits in-game; " +
                   "speed 6000 / range 192 / collision radius 10 / CollisionLayer L_SPELL; the DLL may override range from iTouchReach");

        // Area visuals: explosions that deal no damage, one per school x size (OUTLINE "Area").
        foreach (var school in Mappings.Schools)
        {
            var art = AreaArt(school);
            for (var i = 0; i < AreaSizes.Length; i++)
            {
                var size = AreaSizes[i];
                var radius = new[] { 128f, 320f, 640f }[i];
                ctx.C($"LA_AreaFX_{school}_{size}", fk => new Explosion(fk, BuildContext.Release)
                {
                    ObjectBounds = new ObjectBounds(),
                    Model = new Model { File = art.Model },
                    ImpactDataSet = new FormLink<IImpactDataSetGetter>(ctx.Vanilla_("ImpactDataSet", art.ImpactSet)),
                    Sound1 = new FormLink<ISoundDescriptorGetter>(ctx.Vanilla_("SoundDescriptor", art.Sound)),
                    Force = 0f,
                    Damage = 0f,
                    Radius = radius,
                    ISRadius = 0f,
                    VerticalOffsetMult = 0f,
                    Flags = Explosion.Flag.IgnoreLosCheck | Explosion.Flag.NoControllerVibration,
                    SoundLevel = SoundLevel.Normal,
                });
            }
        }
        ctx.Verify("EXPL LA_AreaFX_*: model paths are vanilla mesh paths typed by hand (not read from game data) - verify each exists in Skyrim - Meshes0.bsa; " +
                   "tint/scale per size needs an art or CK pass (the three sizes differ only by Radius); ImpactDataSet/Sound1 FormKeys are verified against Mutagen FormKeys");
    }

    private sealed record AreaArtSpec(string Model, string ImpactSet, string Sound);

    private static AreaArtSpec AreaArt(string school) => school switch
    {
        "Alteration" => new(@"Magic\AlterationMassExplosion.nif", "MAGLightSpellImpactSet", "MAGGenericProjectileImpactSD"),
        "Conjuration" => new(@"Magic\ConjureImpactExplosion.nif", "MAGConjure01ImpactSet", "MAGConjureImpactSD"),
        "Destruction" => new(@"Magic\FireBallExplosion.nif", "MAGFirebolt01ImpactSet", "MAGDestructionFireballExplosion"),
        "Illusion" => new(@"Magic\IllusionMassiveLightExplosion.nif", "MAGIllusionImpactSet", "MAGIllusionCharmHitSD"),
        "Restoration" => new(@"Magic\TurnUndeadMassExplosion.nif", "MAGTurnUnlImpactSet", "MAGRestorationBaneOfTheDeadExplosion2D"),
        _ => throw new GeneratorException($"no area art for school {school}"),
    };

    // ------------------------------------------------------------------------------------
    // Variant planning (also used by validation and tests)
    // ------------------------------------------------------------------------------------

    public static IEnumerable<(string? Name, int Sub, bool Special)> Targets(EffectDef e, DataSet data)
    {
        switch (e.Target)
        {
            case "attribute":
                for (var i = 0; i < Mappings.Attributes.Length; i++) yield return (Mappings.Attributes[i], i, false);
                break;
            case "skill":
                for (var i = 0; i < Mappings.SkyrimSkills.Length; i++) yield return (Mappings.SkyrimSkills[i], i, false);
                foreach (var (name, idx) in SpecialMorrowindSkills(data)) yield return (name, 100 + idx, true);
                break;
            default:
                yield return (null, -1, false);
                break;
        }
    }

    /// <summary>
    /// Morrowind skills whose data/content/skills.json mapping includes a target that is not a
    /// Skyrim skill (Acrobatics, Athletics, Hand-to-Hand, Unarmored, ...): they get their own
    /// custom variant named with the Morrowind skill.
    /// </summary>
    public static List<(string Name, int Index)> SpecialMorrowindSkills(DataSet data)
    {
        var result = new List<(string, int)>();
        var entries = data.ContentList("skills", "skills", "morrowind", "mappings");
        var skyrim = new HashSet<string>(Mappings.SkyrimSkills, StringComparer.OrdinalIgnoreCase);
        foreach (var o in entries)
        {
            var name = o.Str("morrowind", "mwSkill", "skill", "name", "id");
            if (name is null) continue;
            var pascal = Naming.ToPascal(name.StartsWith("$") ? name[(name.LastIndexOf('_') + 1)..] : name);
            var idx = Array.FindIndex(Mappings.MorrowindSkills, s => s.Equals(pascal, StringComparison.OrdinalIgnoreCase));
            var subIdx = (int?)o.Num("index", "mwIndex", "sub");
            if (subIdx is >= 100) subIdx -= 100;
            if (idx < 0 && subIdx is >= 0 and < 27) idx = subIdx.Value;
            if (idx < 0) continue;
            if (o.Bool("special") == true || TargetNames(o).Any(t => !skyrim.Contains(t)))
                result.Add((Mappings.MorrowindSkills[idx], idx));
        }
        return result.Distinct().OrderBy(r => r.Item2).ToList();
    }

    private static IEnumerable<string> TargetNames(System.Text.Json.Nodes.JsonObject o)
    {
        var targets = o.Arr("targets", "skyrim", "mapping", "map");
        if (targets is null)
        {
            var single = o.Str("skyrim", "target", "mapping");
            if (single is not null) yield return Naming.ToPascal(single);
            yield break;
        }
        foreach (var t in targets)
        {
            if (t is System.Text.Json.Nodes.JsonObject to)
            {
                var n = to.Str("skill", "target", "av", "actorValue", "kind", "special", "name");
                if (n is not null) yield return Naming.ToPascal(n);
            }
            else if (t is System.Text.Json.Nodes.JsonValue tv && tv.TryGetValue<string>(out var s)) yield return Naming.ToPascal(s);
        }
    }

    public static string VariantEditorId(EffectDef e, string range, string? targetName) =>
        targetName is null ? $"LA_{e.Pascal}_{Naming.RangeName(range)}" : $"LA_{e.Pascal}_{targetName}_{Naming.RangeName(range)}";

    public static List<VariantSpec> PlanVariants(DataSet data)
    {
        var list = new List<VariantSpec>();
        foreach (var e in data.Effects)
            foreach (var range in e.Ranges)
                foreach (var (name, sub, special) in Targets(e, data))
                    list.Add(new VariantSpec(e, range, name, sub, special, VariantEditorId(e, range, name)));
        return list;
    }

    // ------------------------------------------------------------------------------------
    // MGEF variants
    // ------------------------------------------------------------------------------------

    public void BuildVariants(Func<EffectDef, FormKey?> associationFor)
    {
        foreach (var v in PlanVariants(ctx.Data))
        {
            var mgef = BuildVariant(v, associationFor);
            if (!ctx.Variants.TryGetValue(v.Effect.Id, out var byRange))
                ctx.Variants[v.Effect.Id] = byRange = new SortedDictionary<string, SortedDictionary<int, string>>(StringComparer.Ordinal);
            if (!byRange.TryGetValue(v.Range, out var bySub)) byRange[v.Range] = bySub = new SortedDictionary<int, string>();
            bySub[v.Sub] = mgef.EditorID!;
        }
    }

    private MagicEffect BuildVariant(VariantSpec v, Func<EffectDef, FormKey?> associationFor)
    {
        var e = v.Effect;
        var ctxName = $"{e.SourceFile}:{e.Id}";
        var custom = e.Tier == "custom" || v.SpecialTarget || e.Target == "attribute";
        var archetypeType = custom ? ArchetypeType.Script : ParseArchetypeFor(e, ctxName);

        // Primary actor value: the skill for skill families, else the data's actorValue.
        var av = ActorValue.None;
        if (!custom)
        {
            if (e.Target == "skill" && v.TargetName is not null) av = Mappings.ParseAv(v.TargetName, Log, ctxName);
            else av = Mappings.ParseAv(e.ActorValue, Log, ctxName);
        }

        var archetype = MakeArchetype(archetypeType, e, associationFor, ctxName);
        archetype.ActorValue = av;

        var flags = ComputeFlags(e, v, archetypeType, custom, ctxName);
        var delivery = v.Range switch
        {
            "self" => TargetType.Self,
            _ => TargetType.Aimed,
        };

        var mgef = ctx.C(v.EditorId, fk => new MagicEffect(fk, BuildContext.Release));
        mgef.Name = DisplayName(e, v);
        mgef.Description = "";
        mgef.Flags = flags;
        mgef.BaseCost = (float)e.BaseCost;
        mgef.MagicSkill = Mappings.SchoolAv(e.School, Log, ctxName);
        mgef.ResistValue = custom ? ActorValue.None : Mappings.ParseAv(e.Resist, Log, ctxName);
        mgef.Archetype = archetype;
        mgef.CastType = CastType.FireAndForget;
        mgef.TargetType = delivery;
        mgef.SecondActorValue = custom ? ActorValue.None : Mappings.ParseAv(e.SecondAV, Log, ctxName);
        mgef.SecondActorValueWeight = mgef.SecondActorValue == ActorValue.None ? 0f : (float)(e.SecondAVWeight ?? 1.0);
        mgef.CastingSoundLevel = SoundLevel.Normal;
        mgef.SkillUsageMultiplier = (float)(e.Raw.Obj("skyrim").Num("skillUsageMult", "skillUsageMultiplier") ?? 0.0);
        mgef.MinimumSkillLevel = 0;
        mgef.SpellmakingArea = 0;
        mgef.SpellmakingCastingTime = 0.5f;
        mgef.TaperWeight = 0f;
        mgef.TaperCurve = 0f;
        mgef.TaperDuration = 0f;
        mgef.DualCastScale = 1f;
        mgef.ScriptEffectAIScore = 0f;
        mgef.ScriptEffectAIDelayTime = 0f;

        if (v.Range == "touch") mgef.Projectile.SetTo(ctx.ContentLink(TouchProjectile));
        else if (v.Range == "target") mgef.Projectile.SetTo(TargetProjectile(e, ctxName));

        var impact = ImpactSetFor(e);
        if (impact is not null) mgef.ImpactData.SetTo(ctx.Vanilla_("ImpactDataSet", impact));

        var kws = KeywordsFor(e, v, custom, ctxName).ToList();
        if (kws.Count > 0) mgef.Keywords = Links.Keywords(kws);

        return mgef;
    }

    private ArchetypeType ParseArchetypeFor(EffectDef e, string ctxName)
    {
        if (!string.IsNullOrWhiteSpace(e.Archetype)) return Mappings.ParseArchetype(e.Archetype, Log, ctxName);
        if (e.Pascal.StartsWith("Summon", StringComparison.Ordinal)) return ArchetypeType.SummonCreature;
        if (e.Pascal.StartsWith("Bound", StringComparison.Ordinal)) return ArchetypeType.Bound;
        Log.Warn($"{ctxName}: {e.Tier} effect has no skyrim.archetype; using Script");
        return ArchetypeType.Script;
    }

    private AMagicEffectArchetype MakeArchetype(ArchetypeType t, EffectDef e, Func<EffectDef, FormKey?> associationFor, string ctxName)
    {
        FormKey? Assoc()
        {
            var fk = associationFor(e);
            if (fk is null) Log.Error($"{ctxName}: {t} archetype needs an association (skyrim.association or a stand-in entry)");
            return fk;
        }
        switch (t)
        {
            case ArchetypeType.SummonCreature:
            {
                var a = new MagicEffectSummonCreatureArchetype();
                if (Assoc() is { } fk) a.Association.SetTo(fk);
                return a;
            }
            case ArchetypeType.Bound:
            {
                var a = new MagicEffectBoundArchetype();
                if (Assoc() is { } fk) a.Association.SetTo(fk);
                return a;
            }
            case ArchetypeType.Light:
            {
                var a = new MagicEffectLightArchetype();
                if (associationFor(e) is { } fk) a.Association.SetTo(fk);
                else ctx.Verify($"MGEF {e.Pascal}: Light archetype without a LIGH association - set skyrim.association or pick the Candlelight light in CK");
                return a;
            }
            case ArchetypeType.PeakValueModifier:
            {
                var a = new MagicEffectPeakValueModArchetype();
                if (associationFor(e) is { } fk) a.Association.SetTo(fk);
                return a;
            }
            case ArchetypeType.Cloak:
            {
                var a = new MagicEffectCloakArchetype();
                if (Assoc() is { } fk) a.Association.SetTo(fk);
                return a;
            }
            default:
                return new MagicEffectArchetype(t);
        }
    }

    private MagicEffect.Flag ComputeFlags(EffectDef e, VariantSpec v, ArchetypeType t, bool custom, string ctxName)
    {
        MagicEffect.Flag flags = 0;
        foreach (var f in e.Flags)
        {
            if (Mappings.TryParseFlag(f, out var parsed)) flags |= parsed;
            else Log.Error($"{ctxName}: unknown MGEF flag '{f}'");
        }
        if (e.Hostile)
        {
            flags |= MagicEffect.Flag.Hostile;
            if (!e.FlagsSpecified) flags |= MagicEffect.Flag.Detrimental;
        }
        // Recover: held value changes are undone when the effect ends; ticking ones are not.
        if (!custom && Mappings.IsValueArchetype(t) && e.HasDuration && e.HasMagnitude && !e.Ticking) flags |= MagicEffect.Flag.Recover;
        if (e.Ticking) flags &= ~MagicEffect.Flag.Recover;
        if (!e.HasMagnitude) flags |= MagicEffect.Flag.NoMagnitude;
        if (!e.HasDuration) flags |= MagicEffect.Flag.NoDuration;
        if (!e.HasArea || v.Range == "self") flags |= MagicEffect.Flag.NoArea;
        if (e.HasMagnitude) flags |= MagicEffect.Flag.PowerAffectsMagnitude;
        else if (e.HasDuration) flags |= MagicEffect.Flag.PowerAffectsDuration;
        return flags;
    }

    private FormKey TargetProjectile(EffectDef e, string ctxName)
    {
        if (!string.IsNullOrWhiteSpace(e.Projectile))
        {
            var fk = ctx.Resolve(e.Projectile, $"{ctxName} skyrim.projectile", "Projectile");
            if (fk is not null) return fk.Value;
        }
        var name = DefaultProjectile(e);
        return ctx.Vanilla_("Projectile", name);
    }

    /// <summary>School default projectile for Target variants without skyrim.projectile.</summary>
    public static string DefaultProjectile(EffectDef e)
    {
        var kws = new HashSet<string>(e.Keywords, StringComparer.OrdinalIgnoreCase);
        if (kws.Contains("MagicDamageFrost")) return "FrostIcicleProjectile01";
        if (kws.Contains("MagicDamageShock")) return "ShockBoltAim";
        if (kws.Contains("MagicDamageFire")) return "FireboltProjectile01";
        return e.School switch
        {
            "Alteration" => e.Hostile ? "ParalyzeProjectile" : "AlterPosProjectile",
            "Conjuration" => "ReanimateProjectile",
            "Destruction" => "FireboltProjectile01",
            "Illusion" => e.Hostile ? "IllusionNeg01Projectile" : "Illusion01Projectile",
            "Restoration" => e.Hostile ? "TurnUndeadProjectile" : "HealFakeProjectile",
            _ => "FireboltProjectile01",
        };
    }

    private static string? ImpactSetFor(EffectDef e)
    {
        var kws = new HashSet<string>(e.Keywords, StringComparer.OrdinalIgnoreCase);
        if (kws.Contains("MagicDamageFrost")) return "MAGFrostBolt01ImpactSet";
        if (kws.Contains("MagicDamageShock")) return "MAGShock01ImpactSet";
        if (kws.Contains("MagicDamageFire")) return "MAGFirebolt01ImpactSet";
        if (!e.Ranges.Any(r => r != "self")) return null;
        return e.School switch
        {
            "Alteration" => "MAGLightSpellImpactSet",
            "Conjuration" => "MAGConjure01ImpactSet",
            "Destruction" => e.Hostile ? "MAGAbsorbRedImpactSet" : null,
            "Illusion" => e.Hostile ? "MAGIllusionNegImpactSet" : "MAGIllusionImpactSet",
            "Restoration" => "MAGTurnUnlImpactSet",
            _ => null,
        };
    }

    private IEnumerable<FormKey> KeywordsFor(EffectDef e, VariantSpec v, bool custom, string ctxName)
    {
        var seen = new HashSet<FormKey>();
        foreach (var k in e.Keywords)
        {
            var fk = ctx.Resolve(k, $"{ctxName} keywords", "Keyword");
            if (fk is not null && seen.Add(fk.Value)) yield return fk.Value;
        }
        if (custom)
        {
            var fk = ctx.ContentLink(KwCustom);
            if (seen.Add(fk)) yield return fk;
        }
        if (v.Range == "touch")
        {
            var fk = ctx.ContentLink(KwTouch);
            if (seen.Add(fk)) yield return fk;
        }
    }

    private string DisplayName(EffectDef e, VariantSpec v)
    {
        var baseName = ctx.Tr.Resolve(e.NameKey, SplitPascal(e.Pascal));
        if (v.TargetName is null) return baseName;
        var targetName = TargetDisplayName(v);
        foreach (var word in new[] { "Attribute", "Skill" })
        {
            var idx = baseName.IndexOf(word, StringComparison.Ordinal);
            if (idx >= 0) return baseName[..idx] + targetName + baseName[(idx + word.Length)..];
        }
        return $"{baseName}: {targetName}";
    }

    private string TargetDisplayName(VariantSpec v)
    {
        if (v.Effect.Target == "attribute") return ctx.Tr.Resolve($"$LA_Attr_{v.TargetName}", SplitPascal(v.TargetName!));
        if (v.SpecialTarget) return ctx.Tr.Resolve($"$LA_MwSkill_{v.TargetName}", SplitPascal(v.TargetName!));
        return ctx.Tr.Resolve($"$LA_Skill_{v.TargetName}", SplitPascal(v.TargetName!));
    }

    public static string SplitPascal(string s)
    {
        var sb = new System.Text.StringBuilder();
        for (var i = 0; i < s.Length; i++)
        {
            if (i > 0 && char.IsUpper(s[i]) && !char.IsUpper(s[i - 1])) sb.Append(' ');
            sb.Append(s[i]);
        }
        return sb.ToString();
    }
}
