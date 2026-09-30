using System.Text;

namespace LostArt.Generator.Core;

/// <summary>Reads Interface/Translations/LostArt_ENGLISH.txt style files (UTF-16 LE with BOM, key TAB value).</summary>
public sealed class Translations
{
    private readonly Dictionary<string, string> _map = new(StringComparer.Ordinal);
    public int Count => _map.Count;
    public string? SourcePath { get; private set; }

    public static Translations Load(string dataDir, Log log, string language = "ENGLISH")
    {
        var t = new Translations();
        var dir = Path.Combine(dataDir, "translations");
        var path = Path.Combine(dir, $"LostArt_{language}.txt");
        if (!File.Exists(path))
        {
            log.Warn($"translations: {path} not found; display names fall back to generated English");
            return t;
        }
        t.SourcePath = path;
        var bytes = File.ReadAllBytes(path);
        string text;
        if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) text = Encoding.Unicode.GetString(bytes, 2, bytes.Length - 2);
        else if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) text = Encoding.UTF8.GetString(bytes, 3, bytes.Length - 3);
        else
        {
            log.Warn($"translations: {path} has no UTF-16 LE BOM (CONTRACTS §6); reading as UTF-8");
            text = Encoding.UTF8.GetString(bytes);
        }
        foreach (var raw in text.Split('\n'))
        {
            var line = raw.TrimEnd('\r');
            if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#") || line.StartsWith("//")) continue;
            var tab = line.IndexOf('\t');
            if (tab <= 0) continue;
            var key = line[..tab].Trim();
            var value = line[(tab + 1)..];
            t._map[key] = value;
        }
        return t;
    }

    public bool Has(string key) => _map.ContainsKey(Normalize(key));

    public string? Get(string? key)
    {
        if (string.IsNullOrEmpty(key)) return null;
        return _map.TryGetValue(Normalize(key), out var v) ? v : null;
    }

    /// <summary>Resolves a "$LA_..." key; a plain string (no $) is returned as-is.</summary>
    public string Resolve(string? keyOrText, string fallback)
    {
        if (string.IsNullOrEmpty(keyOrText)) return fallback;
        if (!keyOrText.StartsWith("$")) return keyOrText;
        return Get(keyOrText) ?? fallback;
    }

    private static string Normalize(string key) => key.StartsWith("$") ? key : "$" + key;
}
