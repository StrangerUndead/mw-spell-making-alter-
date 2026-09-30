using LostArt.Generator.Core;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator.Build;

/// <summary>Name tables and string -> Mutagen enum conversions.</summary>
public static class Mappings
{
    public static readonly string[] Schools = { "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration" };

    /// <summary>Morrowind attribute order (CONTRACTS §2).</summary>
    public static readonly string[] Attributes = { "Strength", "Intelligence", "Willpower", "Agility", "Speed", "Endurance", "Personality", "Luck" };

    /// <summary>Skyrim skills in RE::ActorValue order (sub 0-17).</summary>
    public static readonly string[] SkyrimSkills =
    {
        "OneHanded", "TwoHanded", "Archery", "Block", "Smithing", "HeavyArmor", "LightArmor", "Pickpocket", "Lockpicking",
        "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting",
    };

    /// <summary>Morrowind skills (sub 100+i).</summary>
    public static readonly string[] MorrowindSkills =
    {
        "Block", "Armorer", "MediumArmor", "HeavyArmor", "BluntWeapon", "LongBlade", "Axe", "Spear", "Athletics", "Enchant",
        "Destruction", "Alteration", "Illusion", "Conjuration", "Mysticism", "Restoration", "Alchemy", "Unarmored", "Security",
        "Sneak", "Acrobatics", "LightArmor", "ShortBlade", "Marksman", "Mercantile", "Speechcraft", "HandToHand",
    };

    private static readonly Dictionary<string, ActorValue> AvAliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["FireResist"] = ActorValue.ResistFire, ["ResistFire"] = ActorValue.ResistFire,
        ["FrostResist"] = ActorValue.ResistFrost, ["ResistFrost"] = ActorValue.ResistFrost,
        ["ElectricResist"] = ActorValue.ResistShock, ["ShockResist"] = ActorValue.ResistShock, ["ResistShock"] = ActorValue.ResistShock,
        ["MagicResist"] = ActorValue.ResistMagic, ["ResistMagic"] = ActorValue.ResistMagic,
        ["DiseaseResist"] = ActorValue.ResistDisease, ["ResistDisease"] = ActorValue.ResistDisease,
        ["PoisonResist"] = ActorValue.PoisonResist, ["ResistPoison"] = ActorValue.PoisonResist,
        ["Fatigue"] = ActorValue.Stamina, ["Marksman"] = ActorValue.Archery, ["Speechcraft"] = ActorValue.Speech,
        ["Lockpicking"] = ActorValue.Lockpicking, ["Pickpocket"] = ActorValue.Pickpocket,
        ["Speed"] = ActorValue.SpeedMult, ["SpeedMult"] = ActorValue.SpeedMult,
        ["ArmorRating"] = ActorValue.DamageResist, ["Armor"] = ActorValue.DamageResist,
        ["HealthRate"] = ActorValue.HealRate, ["MagickaRateMult"] = ActorValue.MagickaRateMult,
        ["WeaponSpeedMult"] = ActorValue.WeaponSpeedMult, ["AttackDamageMult"] = ActorValue.AttackDamageMult,
        ["Paralysis"] = ActorValue.Paralysis, ["Invisibility"] = ActorValue.Invisibility, ["NightEye"] = ActorValue.NightEye,
        ["Blindness"] = ActorValue.Blindness, ["Telekinesis"] = ActorValue.Telekinesis, ["JumpingBonus"] = ActorValue.JumpingBonus,
        ["None"] = ActorValue.None, [""] = ActorValue.None,
    };

    public static ActorValue ParseAv(string? name, Log log, string context)
    {
        if (string.IsNullOrWhiteSpace(name)) return ActorValue.None;
        var key = name.Replace(" ", "");
        if (AvAliases.TryGetValue(key, out var av)) return av;
        if (Enum.TryParse<ActorValue>(key, true, out av)) return av;
        log.Error($"{context}: unknown actor value '{name}'");
        return ActorValue.None;
    }

    public static ActorValue SchoolAv(string school, Log log, string context) => school switch
    {
        "Alteration" => ActorValue.Alteration,
        "Conjuration" => ActorValue.Conjuration,
        "Destruction" => ActorValue.Destruction,
        "Illusion" => ActorValue.Illusion,
        "Restoration" => ActorValue.Restoration,
        _ => Fail(log, $"{context}: '{school}' is not a Skyrim school (Mysticism effects must be re-homed in skyrim.school)"),
    };

    private static ActorValue Fail(Log log, string msg) { log.Error(msg); return ActorValue.None; }

    private static readonly Dictionary<string, MagicEffectArchetype.TypeEnum> ArchetypeAliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Fear"] = MagicEffectArchetype.TypeEnum.Demoralize,
        ["Courage"] = MagicEffectArchetype.TypeEnum.Rally,
        ["Summon"] = MagicEffectArchetype.TypeEnum.SummonCreature,
        ["BoundWeapon"] = MagicEffectArchetype.TypeEnum.Bound,
        ["PeakValueMod"] = MagicEffectArchetype.TypeEnum.PeakValueModifier,
        ["ValueMod"] = MagicEffectArchetype.TypeEnum.ValueModifier,
        ["Paralyze"] = MagicEffectArchetype.TypeEnum.Paralysis,
        ["CureDisease"] = MagicEffectArchetype.TypeEnum.CureDisease,
        ["Soultrap"] = MagicEffectArchetype.TypeEnum.SoulTrap,
    };

    public static MagicEffectArchetype.TypeEnum ParseArchetype(string? name, Log log, string context)
    {
        if (string.IsNullOrWhiteSpace(name)) return MagicEffectArchetype.TypeEnum.Script;
        var key = name.Replace(" ", "").Replace("-", "");
        if (ArchetypeAliases.TryGetValue(key, out var t)) return t;
        if (Enum.TryParse<MagicEffectArchetype.TypeEnum>(key, true, out t)) return t;
        log.Error($"{context}: unknown archetype '{name}'");
        return MagicEffectArchetype.TypeEnum.Script;
    }

    public static bool TryParseFlag(string name, out MagicEffect.Flag flag)
    {
        var key = name.Replace(" ", "").Replace("-", "");
        if (key.Equals("HideInUi", StringComparison.OrdinalIgnoreCase)) key = "HideInUI";
        return Enum.TryParse(key, true, out flag);
    }

    /// <summary>Archetypes whose value is a held actor-value change (Recover applies when not ticking).</summary>
    public static bool IsValueArchetype(MagicEffectArchetype.TypeEnum t) =>
        t is MagicEffectArchetype.TypeEnum.ValueModifier or MagicEffectArchetype.TypeEnum.PeakValueModifier
            or MagicEffectArchetype.TypeEnum.DualValueModifier or MagicEffectArchetype.TypeEnum.Absorb;
}
