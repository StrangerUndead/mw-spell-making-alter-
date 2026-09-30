using System.Text.Json;
using System.Text.Json.Nodes;

namespace LostArt.Generator.Core;

public static class Json
{
    public static readonly JsonSerializerOptions ReadOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowReadingFromString,
    };

    public static readonly JsonDocumentOptions DocOptions = new()
    {
        CommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    public static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    public static JsonNode? ParseFile(string path)
    {
        using var s = File.OpenRead(path);
        return JsonNode.Parse(s, documentOptions: DocOptions);
    }

    public static T? Deserialize<T>(JsonNode? node) => node is null ? default : node.Deserialize<T>(ReadOptions);

    /// <summary>Writes JSON with LF line endings and a trailing newline (stable for git).</summary>
    public static void WriteFile(string path, JsonNode node)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var text = node.ToJsonString(WriteOptions).Replace("\r\n", "\n") + "\n";
        File.WriteAllText(path, text, new System.Text.UTF8Encoding(false));
    }

    // ---- tolerant accessors -------------------------------------------------------------
    public static string? Str(this JsonNode? n, params string[] keys)
    {
        foreach (var k in keys)
        {
            var v = Get(n, k);
            if (v is JsonValue jv)
            {
                if (jv.TryGetValue<string>(out var s)) return s;
                return jv.ToJsonString();
            }
        }
        return null;
    }

    public static double? Num(this JsonNode? n, params string[] keys)
    {
        foreach (var k in keys)
        {
            var v = Get(n, k);
            if (v is JsonValue jv)
            {
                if (jv.TryGetValue<double>(out var d)) return d;
                if (jv.TryGetValue<string>(out var s) && double.TryParse(s, System.Globalization.CultureInfo.InvariantCulture, out d)) return d;
            }
        }
        return null;
    }

    public static bool? Bool(this JsonNode? n, params string[] keys)
    {
        foreach (var k in keys)
        {
            var v = Get(n, k);
            if (v is JsonValue jv)
            {
                if (jv.TryGetValue<bool>(out var b)) return b;
                if (jv.TryGetValue<double>(out var d)) return d != 0;
            }
        }
        return null;
    }

    public static JsonArray? Arr(this JsonNode? n, params string[] keys)
    {
        foreach (var k in keys)
            if (Get(n, k) is JsonArray a) return a;
        return null;
    }

    public static JsonObject? Obj(this JsonNode? n, params string[] keys)
    {
        foreach (var k in keys)
            if (Get(n, k) is JsonObject o) return o;
        return null;
    }

    public static List<string> Strings(this JsonNode? n, params string[] keys)
    {
        var a = Arr(n, keys);
        if (a is null)
        {
            var single = Str(n, keys);
            return single is null ? new() : new() { single };
        }
        return a.Select(x => x is JsonValue v && v.TryGetValue<string>(out var s) ? s : x?.ToJsonString() ?? "").Where(s => s.Length > 0).ToList();
    }

    public static JsonNode? Get(JsonNode? n, string key)
    {
        if (n is not JsonObject o) return null;
        if (o.TryGetPropertyValue(key, out var v)) return v;
        foreach (var kv in o)
            if (string.Equals(kv.Key, key, StringComparison.OrdinalIgnoreCase)) return kv.Value;
        return null;
    }
}
