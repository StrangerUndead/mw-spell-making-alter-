using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace LostArt.Generator.Build;

/// <summary>LostArt_Slots.esp: 500 primary + 500 sub-spell slots and their placeholder effect.</summary>
public sealed class SlotsBuilder(BuildContext ctx)
{
    public const int SlotCount = 500;
    public const string BlankEffect = "LA_BlankEffect";
    public const string KwCustomSpell = "LA_KW_CustomSpell";

    public static string SlotId(int i) => $"LA_Slot_{i:000}";
    public static string SubId(int i) => $"LA_Sub_{i:000}";

    public void Build()
    {
        var p = BuildContext.SlotsKey;
        // Allocate slot ids first so a fresh formmap gives contiguous ranges (0x800.. primary, then sub).
        for (var i = 0; i < SlotCount; i++) ctx.Key(p, SlotId(i));
        for (var i = 0; i < SlotCount; i++) ctx.Key(p, SubId(i));

        var kw = ctx.Create(p, KwCustomSpell, fk => new Keyword(fk, BuildContext.Release));

        var blank = ctx.Create(p, BlankEffect, fk => new MagicEffect(fk, BuildContext.Release)
        {
            Name = ctx.Tr.Resolve("$LA_BlankEffect", "Unwritten Spell"),
            Description = "",
            Flags = MagicEffect.Flag.HideInUI | MagicEffect.Flag.NoMagnitude | MagicEffect.Flag.NoArea | MagicEffect.Flag.NoDuration
                    | MagicEffect.Flag.Painless | MagicEffect.Flag.NoHitEffect,
            BaseCost = 0f,
            MagicSkill = ActorValue.None,
            ResistValue = ActorValue.None,
            Archetype = new MagicEffectArchetype(MagicEffectArchetype.TypeEnum.Script) { ActorValue = ActorValue.None },
            CastType = CastType.FireAndForget,
            TargetType = TargetType.Self,
            SecondActorValue = ActorValue.None,
            CastingSoundLevel = SoundLevel.Silent,
            SpellmakingCastingTime = 0.5f,
            DualCastScale = 1f,
        });

        var name = ctx.Tr.Resolve("$LA_Slot_Empty", "Unwritten Spell");
        for (var i = 0; i < SlotCount; i++) MakeSlot(p, SlotId(i), name, blank.FormKey, kw.FormKey);
        for (var i = 0; i < SlotCount; i++) MakeSlot(p, SubId(i), name, blank.FormKey, kw.FormKey);
    }

    private void MakeSlot(ModKey p, string editorId, string name, FormKey blank, FormKey kw)
    {
        ctx.Create(p, editorId, fk => new Spell(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            Name = name,
            Keywords = Links.Keywords(new[] { kw }),
            EquipmentType = new FormLinkNullable<IEquipTypeGetter>(ctx.Vanilla_("EquipType", "EitherHand")),
            Description = "",
            BaseCost = 0,
            Flags = SpellDataFlag.ManualCostCalc,
            Type = SpellType.Spell,
            ChargeTime = 0.5f,
            CastType = CastType.FireAndForget,
            TargetType = TargetType.Self,
            CastDuration = 0f,
            Range = 0f,
            Effects = new ExtendedList<Effect>
            {
                new Effect
                {
                    BaseEffect = new FormLinkNullable<IMagicEffectGetter>(blank),
                    Data = new EffectData { Magnitude = 0f, Area = 0, Duration = 0 },
                },
            },
        });
    }
}
