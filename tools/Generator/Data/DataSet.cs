using System.Text.Json.Nodes;
using LostArt.Generator.Core;

namespace LostArt.Generator.Data;

/// <summary>Everything the generator reads from data/ (the single source of truth).</summary>
public sealed class DataSet
{
    public required string Root { get; init; }
    public required Translations Translations { get; init; }
    public List<EffectDef> Effects { get; } = new();
    public Dictionary<string, EffectDef> EffectsById { get; } = new(StringComparer.Ordinal);

    /// <summary>Stand-in entries (data/standins/*.json), flattened.</summary>
    public List<JsonObject> StandIns { get; } = new();

    /// <summary>data/content/&lt;name&gt;.json parsed, keyed by file name without extension.</summary>
    public Dictionary<string, JsonNode?> Content { get; } = new(StringComparer.OrdinalIgnoreCase);

    public JsonNode? ContentFile(string name) => Content.TryGetValue(name, out var n) ? n : null;

    public static DataSet Load(string root, Log log)
    {
        if (!Directory.Exists(root)) throw new GeneratorException($"data directory not found: {root}");
        var ds = new DataSet { Root = root, Translations = Translations.Load(root, log) };

        var effectsDir = Path.Combine(root, "effects");
        var effectFiles = Directory.Exists(effectsDir)
            ? Directory.GetFiles(effectsDir, "*.json").OrderBy(f => Path.GetFileName(f), StringComparer.Ordinal).ToList()
            : new List<string>();
        if (effectFiles.Count == 0) log.Warn($"no effect catalog files in {effectsDir}");
        foreach (var file in effectFiles)
        {
            var rel = Path.GetRelativePath(root, file).Replace('\\', '/');
            JsonNode? node;
            try { node = Json.ParseFile(file); }
            catch (Exception ex) { log.Error($"{rel}: invalid JSON: {ex.Message}"); continue; }
            var arr = node as JsonArray ?? node.Arr("effects");
            if (arr is null) { log.Error($"{rel}: expected an array of EffectDef"); continue; }
            foreach (var item in arr)
            {
                if (item is not JsonObject o) continue;
                EffectDef e;
                try { e = EffectDef.Parse(o, rel); }
                catch (Exception ex) { log.Error($"{rel}: {ex.Message}"); continue; }
                if (!ds.EffectsById.TryAdd(e.Id, e)) { log.Error($"{rel}: duplicate effect id {e.Id}"); continue; }
                ds.Effects.Add(e);
            }
        }
        ds.Effects.Sort((a, b) => string.CompareOrdinal(a.Id, b.Id));

        var standinsDir = Path.Combine(root, "standins");
        if (Directory.Exists(standinsDir))
        {
            foreach (var file in Directory.GetFiles(standinsDir, "*.json").OrderBy(f => f, StringComparer.Ordinal))
            {
                var node = Json.ParseFile(file);
                var arr = node as JsonArray ?? node.Arr("standins", "standIns", "entries", "items");
                if (arr is null && node is JsonObject obj)
                {
                    // map form: { "mw.summon_scamp": {...} }
                    arr = new JsonArray();
                    foreach (var (k, v) in obj)
                        if (v is JsonObject vo) { var c = (JsonObject)vo.DeepClone(); c["effect"] ??= k; arr.Add(c); }
                }
                foreach (var item in arr ?? new JsonArray())
                    if (item is JsonObject o) ds.StandIns.Add(o);
            }
        }

        var contentDir = Path.Combine(root, "content");
        if (Directory.Exists(contentDir))
            foreach (var file in Directory.GetFiles(contentDir, "*.json").OrderBy(f => f, StringComparer.Ordinal))
            {
                try { ds.Content[Path.GetFileNameWithoutExtension(file)] = Json.ParseFile(file); }
                catch (Exception ex) { log.Error($"{Path.GetRelativePath(root, file)}: invalid JSON: {ex.Message}"); }
            }
        return ds;
    }

    /// <summary>Returns the list-valued content of a content file (array root or first array property).</summary>
    public List<JsonObject> ContentList(string name, params string[] keys)
    {
        var node = ContentFile(name);
        var arr = node as JsonArray ?? node.Arr(keys);
        if (arr is null && node is JsonObject o)
        {
            arr = o.Select(kv => kv.Value).OfType<JsonArray>().FirstOrDefault();
            if (arr is null)
            {
                // object keyed by id
                arr = new JsonArray();
                foreach (var (k, v) in o)
                    if (v is JsonObject vo) { var c = (JsonObject)vo.DeepClone(); c["id"] ??= k; arr.Add(c); }
            }
        }
        return arr?.OfType<JsonObject>().ToList() ?? new List<JsonObject>();
    }
}
