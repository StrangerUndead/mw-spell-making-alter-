using LostArt.Generator.Core;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator.Build;

/// <summary>Post-build checks. Errors go to the log (the run fails); the summary is printed.</summary>
public sealed class Validator(BuildContext ctx)
{
    public const int EslRecordLimit = 2048;
    private static readonly HashSet<ModKey> AllowedMasters = new() { VanillaIndex.Skyrim, VanillaIndex.Update, VanillaIndex.Dawnguard, VanillaIndex.Dragonborn };

    /// <summary>Engine-hardcoded vanilla forms that Mutagen's FormKeys package does not list.</summary>
    private static readonly HashSet<FormKey> HardcodedVanilla = new()
    {
        new FormKey(VanillaIndex.Skyrim, 0x000007), // Player (NPC_)
        new FormKey(VanillaIndex.Skyrim, 0x000014), // PlayerRef (ACHR)
    };

    private Log Log => ctx.Log;

    public void Run()
    {
        var locals = new Dictionary<FormKey, IMajorRecordGetter>();
        foreach (var mod in new[] { ctx.Content, ctx.Slots })
            foreach (var r in AllRecords(mod))
                if (!locals.TryAdd(r.FormKey, r)) Log.Error($"duplicate FormKey {r.FormKey}");

        foreach (var mod in new[] { ctx.Content, ctx.Slots })
        {
            var name = mod.ModKey.FileName.String;
            var records = AllRecords(mod).ToList();
            if (records.Count > EslRecordLimit) Log.Error($"{name}: {records.Count} records exceeds the ESL limit of {EslRecordLimit}");
            var edids = new HashSet<string>(StringComparer.Ordinal);
            foreach (var r in records)
            {
                if (r.FormKey.ModKey != mod.ModKey) Log.Error($"{name}: {r.EditorID} is an override of {r.FormKey} (vanilla records must never be edited)");
                if (r.FormKey.ID < FormMap.MinId || r.FormKey.ID > FormMap.MaxId) Log.Error($"{name}: {r.EditorID} {r.FormKey.ID:X6} outside ESL range 0x800-0xFFF");
                if (string.IsNullOrEmpty(r.EditorID) || !r.EditorID.StartsWith("LA_", StringComparison.Ordinal)) Log.Error($"{name}: record {r.FormKey} has EditorID '{r.EditorID}' (must start with LA_)");
                else if (!edids.Add(r.EditorID)) Log.Error($"{name}: duplicate EditorID {r.EditorID}");
                else if (ctx.FormMap.Entries(name).TryGetValue(r.EditorID, out var mapped) && mapped != r.FormKey.ID)
                    Log.Error($"{name}: {r.EditorID} is {r.FormKey.ID:X6} but formmap says {mapped:X6}");

                foreach (var link in r.EnumerateFormLinks())
                {
                    if (link.IsNull) continue;
                    var fk = link.FormKey;
                    if (fk.ModKey == BuildContext.ContentKey || fk.ModKey == BuildContext.SlotsKey)
                    {
                        if (fk.ModKey != mod.ModKey) Log.Error($"{name}: {r.EditorID} references the other generated plugin ({fk})");
                        else if (!locals.ContainsKey(fk))
                        {
                            var edid = ctx.FormMap.Entries(fk.ModKey.FileName).FirstOrDefault(kv => kv.Value == fk.ID).Key;
                            Log.Error($"{name}: {r.EditorID} references missing record {edid ?? fk.ToString()}");
                        }
                    }
                    else
                    {
                        if (!AllowedMasters.Contains(fk.ModKey)) Log.Error($"{name}: {r.EditorID} references {fk} from a plugin that is not an allowed master");
                        else if (!ctx.Vanilla.Exists(fk) && !HardcodedVanilla.Contains(fk)) Log.Error($"{name}: {r.EditorID} references {fk}, not a known vanilla FormKey");
                    }
                }
            }
        }

        // Every planned variant compiled.
        var planned = EffectBuilder.PlanVariants(ctx.Data);
        var mgefByEdid = ctx.Content.MagicEffects.ToDictionary(m => m.EditorID!, m => m, StringComparer.Ordinal);
        var missing = planned.Where(v => !mgefByEdid.ContainsKey(v.EditorId)).ToList();
        foreach (var v in missing) Log.Error($"variant {v.EditorId} for {v.Effect.Id} was not generated");

        // Casting type / delivery consistency on every spell.
        var allMgef = ctx.Content.MagicEffects.Concat(ctx.Slots.MagicEffects).ToDictionary(m => m.FormKey);
        foreach (var spell in ctx.Content.Spells.Concat(ctx.Slots.Spells))
        {
            if (spell.Effects.Count == 0) Log.Error($"{spell.EditorID}: spell has no effects");
            foreach (var eff in spell.Effects)
            {
                if (!allMgef.TryGetValue(eff.BaseEffect.FormKey, out var m)) continue; // missing link reported above
                if (m.CastType != spell.CastType) Log.Error($"{spell.EditorID}: effect {m.EditorID} casting type {m.CastType} != spell {spell.CastType}");
                if (m.TargetType != spell.TargetType) Log.Error($"{spell.EditorID}: effect {m.EditorID} delivery {m.TargetType} != spell {spell.TargetType}");
            }
        }
        foreach (var m in ctx.Content.MagicEffects)
        {
            if (m.CastType != CastType.FireAndForget) Log.Error($"{m.EditorID}: casting type {m.CastType}, expected FireAndForget");
            if (m.TargetType == TargetType.Aimed && m.Projectile.IsNull && m.Archetype is not MagicEffectSummonCreatureArchetype)
                Log.Error($"{m.EditorID}: Aimed effect without projectile");
            var isTouch = m.EditorID!.EndsWith("_Touch", StringComparison.Ordinal);
            if (isTouch && m.Projectile.FormKey != ctx.ContentLink(EffectBuilder.TouchProjectile)) Log.Error($"{m.EditorID}: Touch variant must use LA_TouchProjectile");
        }

        // Data sanity that the plugin depends on.
        foreach (var e in ctx.Data.Effects)
        {
            if (e.Ranges.Count == 0) Log.Error($"{e.Id}: no ranges");
            foreach (var r in e.Ranges) if (r is not ("self" or "touch" or "target")) Log.Error($"{e.Id}: unknown range '{r}'");
            if (e.BaseCost <= 0) Log.Warn($"{e.Id}: skyrim.baseCost is {e.BaseCost}");
            if (e.Sources.Count == 0) Log.Warn($"{e.Id}: no sources (CONTRACTS: every effect needs >= 1 spell or tome)");
            if (!ctx.Tr.Has(e.NameKey) && ctx.Tr.Count > 0) Log.Warn($"{e.Id}: translation key {e.NameKey} missing; English fallback used");
        }
        if (ctx.Content.MagicEffects.Count(m => m.EditorID!.StartsWith("LA_", StringComparison.Ordinal)) != planned.Count)
            Log.Error($"MGEF count {ctx.Content.MagicEffects.Count} != planned variants {planned.Count}");
    }

    public static IEnumerable<IMajorRecordGetter> AllRecords(ISkyrimModGetter mod) => mod.EnumerateMajorRecords();

    public static SortedDictionary<string, int> CountByType(ISkyrimModGetter mod)
    {
        var d = new SortedDictionary<string, int>(StringComparer.Ordinal);
        foreach (var r in mod.EnumerateMajorRecords())
        {
            var t = r switch
            {
                IMagicEffectGetter => "MGEF", ISpellGetter => "SPEL", IKeywordGetter => "KYWD", IProjectileGetter => "PROJ",
                IExplosionGetter => "EXPL", IBookGetter => "BOOK", IGlobalGetter => "GLOB", INpcGetter => "NPC_", IWeaponGetter => "WEAP",
                IArmorGetter => "ARMO", IFurnitureGetter => "FURN", IQuestGetter => "QUST", IDialogBranchGetter => "DLBR",
                IDialogTopicGetter => "DIAL", IDialogResponsesGetter => "INFO", ILeveledItemGetter => "LVLI", IFormListGetter => "FLST",
                IMessageGetter => "MESG", IPerkGetter => "PERK", IPackageGetter => "PACK", IEffectShaderGetter => "EFSH", _ => r.GetType().Name,
            };
            d[t] = d.TryGetValue(t, out var c) ? c + 1 : 1;
        }
        return d;
    }
}
