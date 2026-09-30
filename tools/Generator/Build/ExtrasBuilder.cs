using System.Drawing;
using LostArt.Generator.Core;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace LostArt.Generator.Build;

/// <summary>
/// Support records the DLL's effect systems use: the Morrowind summon-limit perk, the follow
/// package for Command Creature/Humanoid, and the detection shaders for Detect Key/Enchantment.
/// </summary>
public sealed class ExtrasBuilder(BuildContext ctx)
{
    public const string PerkSummonLimit = "LA_Perk_SummonLimit";
    public const string PackageCommandFollow = "LA_Package_CommandFollow";
    public const string ShaderDetectKey = "LA_Shader_DetectKey";
    public const string ShaderDetectEnchantment = "LA_Shader_DetectEnchantment";

    public void Build()
    {
        BuildSummonLimitPerk();
        BuildCommandFollowPackage();
        // OUTLINE "Visual effects": keys gold, enchanted items violet, each pulsing at its own rate
        // (vanilla Detect Life's animals are green), so colour is not the only cue.
        BuildDetectShader(ShaderDetectKey, Color.FromArgb(0, 255, 196, 48), pulseHz: 1.25f);
        BuildDetectShader(ShaderDetectEnchantment, Color.FromArgb(0, 176, 96, 255), pulseHz: 0.6f);
    }

    /// <summary>Hidden perk: +1 to the commanded (summoned) actor limit; the DLL adds/removes it.</summary>
    private void BuildSummonLimitPerk()
    {
        ctx.C(PerkSummonLimit, fk => new Perk(fk, BuildContext.Release)
        {
            Name = ctx.Tr.Resolve("$LA_Perk_SummonLimit", "Morrowind Summoning"),
            Description = "",
            Trait = false,
            Level = 0,
            NumRanks = 1,
            Playable = false,
            Hidden = true,
            Effects = new ExtendedList<APerkEffect>
            {
                new PerkEntryPointModifyValue
                {
                    EntryPoint = APerkEntryPointEffect.EntryType.ModCommandedActorLimit,
                    Modification = PerkEntryPointModifyValue.ModificationType.Add,
                    Value = 1f,
                    Rank = 0,
                    Priority = 0,
                    PerkConditionTabCount = 1, // "Perk Owner", as on vanilla Twin Souls
                },
            },
        });
        ctx.Verify("PERK LA_Perk_SummonLimit: entry point Mod Commanded Actor Limit, Add Value 1, one condition tab (Perk Owner) like vanilla TwinSouls - open in CK once to confirm the entry point shows as expected");
    }

    /// <summary>
    /// Follow-the-player package for AddPackageOverride on commanded actors. It is built on the
    /// vanilla Follow package template (Skyrim.esm "Follow") and carries the template's Follow
    /// procedure with the target set to PlayerRef; no conditions.
    /// </summary>
    private void BuildCommandFollowPackage()
    {
        var playerRef = new FormKey(VanillaIndex.Skyrim, 0x000014);
        var pack = ctx.C(PackageCommandFollow, fk => new Package(fk, BuildContext.Release)
        {
            Flags = Package.Flag.AllowSwimming,
            Type = Package.Types.Package,
            InterruptOverride = Package.Interrupt.None,
            PreferredSpeed = Package.Speed.Run,
            InterruptFlags = Package.InterruptFlag.HellosToPlayer | Package.InterruptFlag.RandomConversations | Package.InterruptFlag.ObserveCombatBehavior
                             | Package.InterruptFlag.GreetCorpseBehavior | Package.InterruptFlag.ReactionToPlayerActions | Package.InterruptFlag.FriendlyFireComments
                             | Package.InterruptFlag.AggroRadiusBehavior | Package.InterruptFlag.AllowIdleChatter | Package.InterruptFlag.WorldInteractions,
            ScheduleMonth = -1,
            ScheduleDayOfWeek = Package.DayOfWeek.Any,
            ScheduleDate = 0,
            ScheduleHour = -1,
            ScheduleMinute = -1,
            ScheduleDurationInMinutes = 0,
            IdleAnimations = new PackageIdles { Type = PackageIdles.Types.Random, TimerSetting = 0f },
            PackageTemplate = new FormLink<IPackageGetter>(ctx.Vanilla_("Package", "Follow")),
            ProcedureTree = new ExtendedList<PackageBranch>
            {
                new PackageBranch
                {
                    BranchType = "Procedure",
                    ProcedureType = "Follow",
                    DataInputIndices = new ExtendedList<byte> { 0, 1, 2 },
                },
            },
        });
        pack.Data[0] = new PackageDataTarget
        {
            Name = "Target",
            Type = PackageDataTarget.Types.SingleRef,
            Flags = APackageData.Flag.Public,
            Target = new PackageTargetSpecificReference { Reference = new FormLink<IPlacedGetter>(playerRef), CountOrDistance = 0 },
        };
        pack.Data[1] = new PackageDataFloat { Name = "MinRadius", Data = 150f, Flags = APackageData.Flag.Public };
        pack.Data[2] = new PackageDataFloat { Name = "MaxRadius", Data = 300f, Flags = APackageData.Flag.Public };
        ctx.Verify("PACK LA_Package_CommandFollow: written by hand on the vanilla Follow template (Skyrim.esm 0x019B2C) with inputs 0 Target = PlayerRef, 1 MinRadius 150, 2 MaxRadius 300 - " +
                   "the input indices/names were not read from the template (no game data): open it in CK, confirm the Follow template's public data shows Target = Player, re-save if CK re-indexes; " +
                   "vanilla FollowPlayer (Skyrim.esm 0x0750BE) is the fallback");
    }

    private void BuildDetectShader(string edid, Color color, float pulseHz)
    {
        ctx.C(edid, fk => new EffectShader(fk, BuildContext.Release)
        {
            // Rim glow only (edge effect): needs no texture, reads through walls like Detect Life.
            Flags = EffectShader.Flag.NoParticleShader,
            MembraneSourceBlendMode = EffectShader.BlendMode.SourceAlpha,
            MembraneBlendOperation = EffectShader.BlendOperation.Add,
            MembraneZTest = EffectShader.ZTest.AlwaysShow,
            MembraneDestBlendMode = EffectShader.BlendMode.One,
            ParticleSourceBlendMode = EffectShader.BlendMode.SourceAlpha,
            ParticleBlendOperation = EffectShader.BlendOperation.Add,
            ParticleZTest = EffectShader.ZTest.Normal,
            ParticleDestBlendMode = EffectShader.BlendMode.One,
            FillColorKey1 = color,
            FillColorKey2 = color,
            FillColorKey3 = color,
            FillColorKey1Scale = 1f,
            FillColorKey2Scale = 1f,
            FillColorKey3Scale = 1f,
            FillColorKey1Time = 0f,
            FillColorKey2Time = 0.5f,
            FillColorKey3Time = 1f,
            FillAlphaFadeInTime = 0.4f,
            FillFullAlphaTime = 0f,
            FillFadeOutTime = 0.4f,
            FillPersistentAlphaRatio = 0.35f,
            FillFullAlphaRatio = 0.6f,
            FillAlphaPulseAmplitude = 0.25f,
            FillAlphaPulseFrequency = pulseHz,
            FillTextureScaleU = 1f,
            FillTextureScaleV = 1f,
            EdgeEffectFallOff = 1.5f,
            EdgeEffectColor = color,
            EdgeEffectAlphaFadeInTime = 0.4f,
            EdgeEffectFullAlphaTime = 0f,
            EdgeEffectAlphaFadeOutTime = 0.4f,
            EdgeEffectPersistentAlphaRatio = 1f,
            EdgeEffectFullAlphaRatio = 1f,
            EdgeEffectAlphaPulseAmplitude = 0.5f,
            EdgeEffectAlphaPulseFrequency = pulseHz,
            EdgeWidth = 0f,
            EdgeColor = color,
            ColorScale = 1f,
            ColorKey1 = color,
            ColorKey2 = color,
            ColorKey3 = color,
            ColorKey1Alpha = 1f,
            ColorKey2Alpha = 1f,
            ColorKey3Alpha = 1f,
            ColorKey2Time = 0.5f,
            ColorKey3Time = 1f,
            ParticleScaleKey1 = 1f,
            ParticleScaleKey2 = 1f,
            ParticleScaleKey2Time = 1f,
            TextureCountU = 1,
            TextureCountV = 1,
            AddonModelsScaleStart = 1f,
            AddonModelsScaleEnd = 1f,
        });
        ctx.Verify($"EFSH {edid}: modelled on vanilla LifeDetected (0x000146) by hand - edge-glow only, no fill texture, ZTest Always Show, pulse {pulseHz} Hz; compare side by side with Detect Life in-game and tune alpha/falloff");
    }
}
