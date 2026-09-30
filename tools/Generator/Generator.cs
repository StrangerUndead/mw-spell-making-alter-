using System.Text.Json.Nodes;
using LostArt.Generator.Build;
using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator;

public sealed class GeneratorOptions
{
    public string DataDir { get; set; } = "data";
    public string OutDir { get; set; } = Path.Combine("build", "plugins");
    public string? FormMapPath { get; set; }
    public string? GeneratedDir { get; set; }
    public bool Prune { get; set; }
    public bool WriteFormMap { get; set; } = true;
    public bool Quiet { get; set; }

    public string ResolvedFormMap => FormMapPath ?? Path.Combine(DataDir, "generated", "formmap.json");
    public string ResolvedGeneratedDir => GeneratedDir ?? Path.GetDirectoryName(Path.GetFullPath(ResolvedFormMap))!;
}

public sealed class GeneratorResult
{
    public required BuildContext Context { get; init; }
    public required Log Log { get; init; }
    public required string ContentPath { get; init; }
    public required string SlotsPath { get; init; }
    public bool Success => Log.Errors.Count == 0;
}

public static class PluginGenerator
{
    public static GeneratorResult Run(GeneratorOptions opt)
    {
        var log = new Log { Quiet = opt.Quiet };
        var data = DataSet.Load(opt.DataDir, log);
        log.Info($"data: {data.Effects.Count} effects, {data.StandIns.Count} stand-ins, content files [{string.Join(", ", data.Content.Keys.OrderBy(k => k))}], {data.Translations.Count} translation keys");

        var formMap = FormMap.Load(opt.ResolvedFormMap);
        var ctx = new BuildContext(data, formMap, log);

        // Slots first: their ids are the most important to keep contiguous and stable.
        new SlotsBuilder(ctx).Build();

        var effects = new EffectBuilder(ctx);
        var items = new ItemsBuilder(ctx);
        var actors = new ActorsBuilder(ctx);
        effects.BuildShared();
        items.BuildGlobals();
        actors.Build();
        effects.BuildVariants(actors.AssociationFor);
        items.BuildTomes();
        items.BuildLoreBooks();
        items.BuildLeveledLists();
        items.BuildAltars();
        var dialogue = new DialogueBuilder(ctx);
        dialogue.Build();
        dialogue.BuildMcmQuest();

        ctx.Commit();
        new Validator(ctx).Run();

        foreach (var plugin in new[] { BuildContext.SlotsName, BuildContext.ContentName })
        {
            var stale = formMap.Stale(plugin);
            if (stale.Count > 0)
            {
                if (opt.Prune) log.Info($"formmap: retired {formMap.Prune(plugin)} stale ids in {plugin}");
                else log.Warn($"formmap: {stale.Count} EditorIDs in {plugin} were not generated (kept; run with --prune to retire): {string.Join(", ", stale.Take(10))}{(stale.Count > 10 ? ", ..." : "")}");
            }
            var added = formMap.NewlyAllocated(plugin);
            if (added.Count > 0) log.Info($"formmap: {added.Count} new ids in {plugin}");
        }

        Directory.CreateDirectory(opt.OutDir);
        var contentPath = Path.Combine(opt.OutDir, BuildContext.ContentName);
        var slotsPath = Path.Combine(opt.OutDir, BuildContext.SlotsName);
        var result = new GeneratorResult { Context = ctx, Log = log, ContentPath = contentPath, SlotsPath = slotsPath };
        if (log.Errors.Count > 0) return result;

        Write(ctx.Slots, slotsPath);
        Write(ctx.Content, contentPath);
        VerifyWritten(slotsPath, log);
        VerifyWritten(contentPath, log);

        if (opt.WriteFormMap)
        {
            formMap.Save(new[] { BuildContext.ContentName, BuildContext.SlotsName });
            WriteVariants(ctx, Path.Combine(opt.ResolvedGeneratedDir, "variants.json"));
        }
        return result;
    }

    public static void Write(SkyrimMod mod, string path)
    {
        var maxId = mod.EnumerateMajorRecords().Select(r => r.FormKey.ID).DefaultIfEmpty(FormMap.MinId - 1).Max();
        mod.ModHeader.Stats.NextFormID = maxId + 1;
        mod.WriteToBinary(path, new BinaryWriteParameters
        {
            ModKey = ModKeyOption.ThrowIfMisaligned,
            MastersListContent = MastersListContentOption.Iterate,
            MastersListOrdering = new MastersListOrderingByLoadOrder(VanillaIndex.MasterOrder),
            RecordCount = RecordCountOption.Iterate,
            NextFormID = NextFormIDOption.NoCheck,
            FormIDUniqueness = FormIDUniquenessOption.Iterate,
            FormIDCompaction = FormIDCompactionOption.Iterate,
            OverriddenFormsOption = OverriddenFormsOption.Iterate,
        });
    }

    /// <summary>Reads the written plugin back and checks header version, ESL flag, masters and ids.</summary>
    private static void VerifyWritten(string path, Log log)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(path, BuildContext.Release);
        var h = mod.ModHeader;
        if (Math.Abs(h.Stats.Version - 1.7f) > 0.0001f) log.Error($"{path}: header version {h.Stats.Version}, expected 1.7");
        if (!h.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small)) log.Error($"{path}: ESL flag not set");
        if (h.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Localized)) log.Error($"{path}: plugin must not be localized");
        var masters = h.MasterReferences.Select(m => m.Master.FileName.String).ToList();
        var order = VanillaIndex.MasterOrder.Select(m => m.FileName.String).ToList();
        if (!masters.SequenceEqual(masters.OrderBy(m => order.IndexOf(m)))) log.Error($"{path}: masters out of order: {string.Join(", ", masters)}");
        var count = mod.EnumerateMajorRecords().Count();
        if (count > Validator.EslRecordLimit) log.Error($"{path}: {count} records");
        foreach (var r in mod.EnumerateMajorRecords())
            if (r.FormKey.ID is < FormMap.MinId or > FormMap.MaxId) log.Error($"{path}: {r.EditorID} has FormID {r.FormKey.ID:X6}");
        // Record header form version (44 for SSE) on a sample record.
        var sample = mod.EnumerateMajorRecords().FirstOrDefault();
        if (sample is IMajorRecordGetter mr && mr.FormVersion != BuildContext.FormVersion) log.Error($"{path}: form version {mr.FormVersion}, expected {BuildContext.FormVersion}");
        log.Info($"wrote {path}: {count} records, masters [{string.Join(", ", masters)}], header {h.Stats.Version:0.0#}, ESL, next id 0x{h.Stats.NextFormID:X}");
    }

    private static void WriteVariants(BuildContext ctx, string path)
    {
        var root = new JsonObject
        {
            ["_comment"] = "Generated by tools/Generator. effect id -> range -> sub (-1 none, 0-7 attribute, 0-17 Skyrim skill, 100+ Morrowind skill) -> MGEF EditorID (LostArt.esp). Resolve FormIDs through formmap.json.",
        };
        var variants = new JsonObject();
        foreach (var (effect, byRange) in ctx.Variants)
        {
            var r = new JsonObject();
            foreach (var range in new[] { "self", "touch", "target" })
            {
                if (!byRange.TryGetValue(range, out var bySub)) continue;
                var s = new JsonObject();
                foreach (var (sub, edid) in bySub) s[sub.ToString(System.Globalization.CultureInfo.InvariantCulture)] = edid;
                r[range] = s;
            }
            variants[effect] = r;
        }
        root["variants"] = variants;
        var assoc = new JsonObject();
        foreach (var e in ctx.Data.Effects)
        {
            var fk = ctx.Records.Values.FirstOrDefault(rec => rec is Npc or Weapon or Armor
                && ctx.Records.TryGetValue(rec.EditorID!, out _)
                && AssociationName(e) == rec.EditorID);
            if (fk is not null) assoc[e.Id] = fk.EditorID;
        }
        root["associations"] = assoc;
        Json.WriteFile(path, root);
    }

    private static string? AssociationName(EffectDef e)
    {
        if (e.Pascal.StartsWith("Bound", StringComparison.Ordinal)) return "LA_Bound_" + e.Pascal["Bound".Length..];
        return null;
    }
}
