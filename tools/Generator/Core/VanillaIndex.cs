using System.Globalization;
using System.Reflection;
using Mutagen.Bethesda.Plugins;

namespace LostArt.Generator.Core;

/// <summary>
/// Index of vanilla / DLC FormKeys by record type and EditorID, built by reflecting over
/// Mutagen.Bethesda.FormKeys.SkyrimSE (no game files are needed).
/// </summary>
public sealed class VanillaIndex
{
    public static readonly ModKey Skyrim = ModKey.FromNameAndExtension("Skyrim.esm");
    public static readonly ModKey Update = ModKey.FromNameAndExtension("Update.esm");
    public static readonly ModKey Dawnguard = ModKey.FromNameAndExtension("Dawnguard.esm");
    public static readonly ModKey HearthFires = ModKey.FromNameAndExtension("HearthFires.esm");
    public static readonly ModKey Dragonborn = ModKey.FromNameAndExtension("Dragonborn.esm");

    /// <summary>Master order used for every generated plugin.</summary>
    public static readonly ModKey[] MasterOrder = { Skyrim, Update, Dawnguard, HearthFires, Dragonborn };

    // recordType (FormKeys class name, e.g. "Keyword") -> EditorID -> FormKey
    private readonly Dictionary<string, Dictionary<string, FormKey>> _byType = new(StringComparer.OrdinalIgnoreCase);
    // FormKey -> (recordType, EditorID)
    private readonly Dictionary<FormKey, (string Type, string EditorId)> _byKey = new();

    private static VanillaIndex? _instance;
    public static VanillaIndex Instance => _instance ??= Build();

    public int Count => _byKey.Count;

    private static VanillaIndex Build()
    {
        var idx = new VanillaIndex();
        var asm = typeof(Mutagen.Bethesda.FormKeys.SkyrimSE.Skyrim).Assembly;
        foreach (var top in asm.GetTypes().Where(t => t.IsClass && t.DeclaringType == null && t.Namespace == "Mutagen.Bethesda.FormKeys.SkyrimSE"))
        {
            foreach (var nested in top.GetNestedTypes(BindingFlags.Public))
            {
                var typeName = nested.Name;
                if (!idx._byType.TryGetValue(typeName, out var map))
                    idx._byType[typeName] = map = new Dictionary<string, FormKey>(StringComparer.OrdinalIgnoreCase);
                foreach (var p in nested.GetProperties(BindingFlags.Public | BindingFlags.Static))
                {
                    if (p.GetValue(null) is not IFormLinkIdentifier link) continue;
                    var fk = link.FormKey;
                    map.TryAdd(p.Name, fk);
                    idx._byKey.TryAdd(fk, (typeName, p.Name));
                }
            }
        }
        return idx;
    }

    public bool Exists(FormKey fk) => _byKey.ContainsKey(fk);

    public (string Type, string EditorId)? Describe(FormKey fk) => _byKey.TryGetValue(fk, out var d) ? d : null;

    /// <summary>Looks up a vanilla record by FormKeys class name ("Keyword", "Npc", ...) and EditorID.</summary>
    public FormKey? Find(string recordType, string editorId)
    {
        if (_byType.TryGetValue(recordType, out var map) && map.TryGetValue(editorId, out var fk)) return fk;
        return null;
    }

    /// <summary>Looks up by EditorID in any record type (first match in a stable order).</summary>
    public IEnumerable<(string Type, FormKey Key)> FindAny(string editorId)
    {
        foreach (var kv in _byType.OrderBy(k => k.Key, StringComparer.Ordinal))
            if (kv.Value.TryGetValue(editorId, out var fk)) yield return (kv.Key, fk);
    }

    /// <summary>
    /// Parses "Skyrim.esm|0x012FD0" (project convention), "012FD0:Skyrim.esm" (Mutagen) or
    /// "Skyrim.esm:0x012FD0". Returns null if the string is not a FormKey reference.
    /// </summary>
    public static FormKey? ParseRef(string? s)
    {
        if (string.IsNullOrWhiteSpace(s)) return null;
        s = s.Trim();
        string? plugin = null, id = null;
        var bar = s.IndexOf('|');
        if (bar > 0) { plugin = s[..bar]; id = s[(bar + 1)..]; }
        else
        {
            var colon = s.IndexOf(':');
            if (colon > 0)
            {
                var a = s[..colon]; var b = s[(colon + 1)..];
                if (LooksLikePlugin(a)) { plugin = a; id = b; }
                else if (LooksLikePlugin(b)) { plugin = b; id = a; }
            }
        }
        if (plugin is null || id is null || !LooksLikePlugin(plugin)) return null;
        id = id.Trim();
        if (id.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) id = id[2..];
        if (!uint.TryParse(id, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var raw)) return null;
        raw &= 0x00FFFFFF;
        return new FormKey(ModKey.FromFileName(plugin.Trim()), raw);
    }

    private static bool LooksLikePlugin(string s) =>
        s.EndsWith(".esm", StringComparison.OrdinalIgnoreCase) || s.EndsWith(".esp", StringComparison.OrdinalIgnoreCase) || s.EndsWith(".esl", StringComparison.OrdinalIgnoreCase);

    public static string Format(FormKey fk) => $"{fk.ModKey.FileName}|0x{fk.ID:X6}";
}
