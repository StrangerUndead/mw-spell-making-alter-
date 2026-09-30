using System.Globalization;
using System.Text.Json.Nodes;

namespace LostArt.Generator.Core;

/// <summary>
/// Persistent EditorID -> local FormID allocation (data/generated/formmap.json, CONTRACTS §2).
/// Rules: an existing id is never moved; ids of removed records are retired (never reused);
/// new EditorIDs get the next free id after the highest id ever used.
/// </summary>
public sealed class FormMap
{
    public const uint MinId = 0x800;
    public const uint MaxId = 0xFFF;

    private readonly Dictionary<string, SortedDictionary<string, uint>> _map = new(StringComparer.Ordinal);
    private readonly Dictionary<string, SortedDictionary<string, uint>> _retired = new(StringComparer.Ordinal);
    private readonly Dictionary<string, HashSet<string>> _requested = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<string>> _newlyAllocated = new(StringComparer.Ordinal);

    public string Path { get; }
    public string RetiredPath => System.IO.Path.Combine(System.IO.Path.GetDirectoryName(Path)!, System.IO.Path.GetFileNameWithoutExtension(Path) + ".retired.json");

    private FormMap(string path) { Path = path; }

    public static FormMap Load(string path)
    {
        var fm = new FormMap(path);
        if (File.Exists(path)) Read(Json.ParseFile(path), fm._map, path);
        if (File.Exists(fm.RetiredPath)) Read(Json.ParseFile(fm.RetiredPath), fm._retired, fm.RetiredPath);
        return fm;
    }

    private static void Read(JsonNode? root, Dictionary<string, SortedDictionary<string, uint>> into, string path)
    {
        if (root is not JsonObject o) throw new GeneratorException($"{path}: expected an object");
        foreach (var (plugin, node) in o)
        {
            if (node is not JsonObject entries) continue;
            var m = new SortedDictionary<string, uint>(StringComparer.Ordinal);
            foreach (var (edid, val) in entries)
            {
                var s = val?.GetValue<string>() ?? throw new GeneratorException($"{path}: {plugin}/{edid} has no id");
                m[edid] = ParseId(s);
            }
            into[plugin] = m;
        }
    }

    public static uint ParseId(string s)
    {
        s = s.Trim();
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) s = s[2..];
        return uint.Parse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture) & 0x00FFFFFF;
    }

    public static string FormatId(uint id) => $"0x{id:X6}";

    private SortedDictionary<string, uint> MapFor(string plugin)
    {
        if (!_map.TryGetValue(plugin, out var m)) _map[plugin] = m = new SortedDictionary<string, uint>(StringComparer.Ordinal);
        return m;
    }

    public IReadOnlyDictionary<string, uint> Entries(string plugin) => MapFor(plugin);

    public IReadOnlyList<string> NewlyAllocated(string plugin) => _newlyAllocated.TryGetValue(plugin, out var l) ? l : Array.Empty<string>();

    /// <summary>Returns the stable local id for an EditorID, allocating one if needed.</summary>
    public uint Get(string plugin, string editorId)
    {
        var m = MapFor(plugin);
        if (!_requested.TryGetValue(plugin, out var req)) _requested[plugin] = req = new HashSet<string>(StringComparer.Ordinal);
        req.Add(editorId);
        if (m.TryGetValue(editorId, out var id)) return id;

        if (_retired.TryGetValue(plugin, out var ret) && ret.TryGetValue(editorId, out var old))
        {
            // Same EditorID coming back: restore its old id rather than burning a new one.
            ret.Remove(editorId);
            m[editorId] = old;
            return old;
        }

        var used = new HashSet<uint>(m.Values);
        if (_retired.TryGetValue(plugin, out var r2)) used.UnionWith(r2.Values);
        uint next = used.Count == 0 ? MinId : used.Max() + 1;
        if (next > MaxId)
        {
            // Past the top: fall back to the lowest never-used gap.
            next = MinId;
            while (next <= MaxId && used.Contains(next)) next++;
            if (next > MaxId) throw new GeneratorException($"{plugin}: ESL FormID range 0x800-0xFFF exhausted");
        }
        m[editorId] = next;
        if (!_newlyAllocated.TryGetValue(plugin, out var nl)) _newlyAllocated[plugin] = nl = new List<string>();
        nl.Add(editorId);
        return next;
    }

    /// <summary>
    /// Records an id that already exists in a plugin (hand-made record kept from a base plugin).
    /// Fails if the EditorID is mapped to another id, or the id belongs to another EditorID.
    /// </summary>
    public void Reserve(string plugin, string editorId, uint id)
    {
        var m = MapFor(plugin);
        if (m.TryGetValue(editorId, out var existing))
        {
            if (existing != id) throw new GeneratorException($"{plugin}: {editorId} is 0x{id:X6} in the base plugin but 0x{existing:X6} in the formmap");
            return;
        }
        var owner = m.FirstOrDefault(kv => kv.Value == id).Key
                    ?? (_retired.TryGetValue(plugin, out var ret) ? ret.FirstOrDefault(kv => kv.Value == id).Key : null);
        if (owner is not null) throw new GeneratorException($"{plugin}: id 0x{id:X6} of {editorId} in the base plugin already belongs to {owner}");
        if (id < MinId || id > MaxId) throw new GeneratorException($"{plugin}: {editorId} has id 0x{id:X6} outside the ESL range");
        m[editorId] = id;
    }

    /// <summary>EditorIDs present in the map but not generated this run.</summary>
    public IReadOnlyList<string> Stale(string plugin)
    {
        var req = _requested.TryGetValue(plugin, out var r) ? r : new HashSet<string>();
        return MapFor(plugin).Keys.Where(k => !req.Contains(k)).ToList();
    }

    /// <summary>Moves stale entries to the retired file (their ids are never reused).</summary>
    public int Prune(string plugin)
    {
        var stale = Stale(plugin);
        if (stale.Count == 0) return 0;
        if (!_retired.TryGetValue(plugin, out var ret)) _retired[plugin] = ret = new SortedDictionary<string, uint>(StringComparer.Ordinal);
        var m = MapFor(plugin);
        foreach (var k in stale) { ret[k] = m[k]; m.Remove(k); }
        return stale.Count;
    }

    public JsonObject ToJson(IEnumerable<string> pluginOrder) => Serialize(_map, pluginOrder);

    public void Save(IEnumerable<string> pluginOrder)
    {
        var order = pluginOrder.ToList();
        Json.WriteFile(Path, Serialize(_map, order));
        if (_retired.Values.Any(v => v.Count > 0)) Json.WriteFile(RetiredPath, Serialize(_retired, order));
        else if (File.Exists(RetiredPath)) File.Delete(RetiredPath);
    }

    private static JsonObject Serialize(Dictionary<string, SortedDictionary<string, uint>> src, IEnumerable<string> pluginOrder)
    {
        var root = new JsonObject();
        foreach (var plugin in pluginOrder.Concat(src.Keys.OrderBy(k => k, StringComparer.Ordinal)).Distinct())
        {
            if (!src.TryGetValue(plugin, out var m)) continue;
            var o = new JsonObject();
            foreach (var (k, v) in m) o[k] = FormatId(v);
            root[plugin] = o;
        }
        return root;
    }
}
