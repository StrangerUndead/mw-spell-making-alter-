using LostArt.Generator.Core;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator.Build;

/// <summary>
/// OUTLINE pipeline step 3: records authored in the Creation Kit live in the same plugins as the
/// generated ones. With --base &lt;dir&gt; the previous LostArt.esp / LostArt_Slots.esp (for example
/// deserialized from plugin/ by Spriggit, then edited in CK) is read; every top-level record whose
/// EditorID the generator does not produce is carried over unchanged, with its FormID reserved in
/// the formmap. Generated records are always rebuilt from data.
/// </summary>
public sealed class HandMadeRecords(BuildContext ctx)
{
    private readonly List<(ModKey Plugin, ISkyrimMajorRecordGetter Record)> _candidates = new();
    private readonly List<IDisposable> _open = new();

    public int Kept { get; private set; }

    /// <summary>Phase 1 (before building): read base plugins and reserve their ids.</summary>
    public void Load(string? baseDir)
    {
        if (string.IsNullOrEmpty(baseDir)) return;
        foreach (var key in new[] { BuildContext.ContentKey, BuildContext.SlotsKey })
        {
            var path = Path.Combine(baseDir, key.FileName);
            if (!File.Exists(path)) { ctx.Log.Warn($"--base: {path} not found; nothing to keep for {key.FileName}"); continue; }
            var mod = SkyrimMod.CreateFromBinaryOverlay(path, BuildContext.Release);
            _open.Add(mod);
            foreach (var rec in mod.EnumerateMajorRecords())
            {
                if (rec.FormKey.ModKey != key) { ctx.Log.Error($"--base {key.FileName}: {rec.EditorID ?? rec.FormKey.ToString()} overrides a record of {rec.FormKey.ModKey} (vanilla records must never be edited)"); continue; }
                if (string.IsNullOrEmpty(rec.EditorID)) { ctx.Log.Error($"--base {key.FileName}: record {rec.FormKey} has no EditorID"); continue; }
                ctx.FormMap.Reserve(key.FileName, rec.EditorID, rec.FormKey.ID);
                if (rec is ISkyrimMajorRecordGetter s) _candidates.Add((key, s));
            }
        }
    }

    /// <summary>Phase 2 (after building, before commit): carry over records the generator did not produce.</summary>
    public void Merge()
    {
        foreach (var (plugin, rec) in _candidates)
        {
            if (ctx.Records.ContainsKey(rec.EditorID!)) continue;          // regenerated from data
            if (rec is IDialogResponsesGetter or IPlacedGetter or ICellGetter or INavigationMeshGetter)
            {
                ctx.Log.Warn($"--base {plugin.FileName}: {rec.EditorID} is a nested record ({rec.GetType().Name}); not carried over - author it through data or keep it in its own top-level parent");
                continue;
            }
            var copy = (ISkyrimMajorRecord)rec.DeepCopy();
            ctx.AddExisting(plugin, copy);
            ctx.FormMap.Get(plugin.FileName, rec.EditorID!); // mark as used this run
            Kept++;
        }
        foreach (var d in _open) d.Dispose();
        _open.Clear();
        if (Kept > 0) ctx.Log.Info($"--base: kept {Kept} hand-made record(s)");
    }
}
