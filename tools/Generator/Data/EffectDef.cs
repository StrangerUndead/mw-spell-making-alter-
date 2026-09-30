using System.Text.Json.Nodes;
using LostArt.Generator.Core;

namespace LostArt.Generator.Data;

/// <summary>One entry of data/effects/*.json (CONTRACTS §3). Parsed tolerantly from JSON.</summary>
public sealed class EffectDef
{
    public required string Id { get; init; }
    public required string NameKey { get; init; }
    public required string Set { get; init; }      // mw | mwx | sk
    public required string Tier { get; init; }     // native | custom | standin
    public required string SourceFile { get; init; }
    public JsonObject Raw { get; init; } = new();

    public int? MwIndex { get; init; }
    public string? MwSchool { get; init; }
    public double? MwBaseCost { get; init; }

    public required string School { get; init; }    // Skyrim school
    public double BaseCost { get; init; }
    public string? Archetype { get; init; }
    public string? ActorValue { get; init; }
    public string? SecondAV { get; init; }
    public double? SecondAVWeight { get; init; }
    public string? Resist { get; init; }
    public string? VanillaEffect { get; init; }
    public string? Projectile { get; init; }
    /// <summary>skyrim.creature / skyrim.weapon / skyrim.armor: the vanilla actor or item this effect binds or models on.</summary>
    public string? Association { get; init; }
    public List<string> Flags { get; init; } = new();
    public bool FlagsSpecified { get; init; }

    public List<string> Ranges { get; init; } = new();  // self | touch | target
    public bool HasMagnitude { get; init; } = true;
    public string MagnitudeUnit { get; init; } = "pts";
    public bool Ticking { get; init; }
    public bool HasDuration { get; init; }
    public bool HasArea { get; init; }
    public string Target { get; init; } = "none";       // none | attribute | skill
    public List<string> Keywords { get; init; } = new();
    public List<string> Riders { get; init; } = new();
    public bool Reflectable { get; init; } = true;
    public bool Hostile { get; init; }
    public List<string> Sources { get; init; } = new();

    /// <summary>Pascal form used in EditorIDs: from the translation key, else from the id.</summary>
    public string Pascal
    {
        get
        {
            const string prefix = "$LA_Effect_";
            if (NameKey.StartsWith(prefix, StringComparison.Ordinal) && NameKey.Length > prefix.Length) return NameKey[prefix.Length..];
            return Naming.SnakeToPascal(Id[(Id.IndexOf('.') + 1)..]);
        }
    }

    public static EffectDef Parse(JsonObject o, string file)
    {
        var id = o.Str("id") ?? throw new GeneratorException($"{file}: effect without id");
        var sk = o.Obj("skyrim");
        var mw = o.Obj("morrowind");
        var mag = o.Obj("magnitude");
        var flagsNode = sk.Arr("flags");
        double baseCost = sk.Num("baseCost") ?? 0;
        if (sk.Obj("baseCost") is JsonObject bc) baseCost = bc.Num("value", "default") ?? 0;
        return new EffectDef
        {
            Id = id,
            NameKey = o.Str("name") ?? "$LA_Effect_" + Naming.SnakeToPascal(id[(id.IndexOf('.') + 1)..]),
            Set = o.Str("set") ?? id.Split('.')[0],
            Tier = (o.Str("tier") ?? "native").ToLowerInvariant(),
            SourceFile = file,
            Raw = o,
            MwIndex = (int?)mw.Num("index"),
            MwSchool = mw.Str("school"),
            MwBaseCost = mw.Num("baseCost"),
            School = sk.Str("school") ?? mw.Str("school") ?? "Alteration",
            BaseCost = baseCost,
            Archetype = sk.Str("archetype"),
            ActorValue = sk.Str("actorValue"),
            SecondAV = sk.Str("secondAV", "secondActorValue"),
            SecondAVWeight = sk.Num("secondAVWeight", "secondActorValueWeight"),
            Resist = sk.Str("resist", "resistValue"),
            VanillaEffect = sk.Str("vanillaEffect"),
            Projectile = sk.Str("projectile"),
            Association = sk.Str("association", "creature", "weapon", "armor", "light", "spell"),
            Flags = sk.Strings("flags"),
            FlagsSpecified = flagsNode is not null,
            Ranges = o.Strings("ranges").Select(r => r.ToLowerInvariant()).ToList(),
            HasMagnitude = mag.Bool("has") ?? (mag is not null),
            MagnitudeUnit = mag.Str("unit") ?? "pts",
            Ticking = mag.Bool("ticking") ?? false,
            HasDuration = o.Bool("duration") ?? false,
            HasArea = o.Bool("area") ?? false,
            Target = (o.Str("target") ?? "none").ToLowerInvariant(),
            Keywords = o.Strings("keywords"),
            Riders = o.Strings("riders"),
            Reflectable = o.Bool("reflectable") ?? true,
            Hostile = o.Bool("hostile") ?? false,
            Sources = o.Strings("sources"),
        };
    }
}

public static class Naming
{
    public static readonly string[] RangeNames = { "Self", "Touch", "Target" };

    public static string SnakeToPascal(string snake) =>
        string.Concat(snake.Split('_', '-', ' ').Where(p => p.Length > 0).Select(p => char.ToUpperInvariant(p[0]) + p[1..]));

    /// <summary>"Night-Eye's Charm" -> "NightEyesCharm" (spaces, hyphens, apostrophes removed; CONTRACTS §2).</summary>
    public static string ToPascal(string text)
    {
        var parts = text.Replace("'", "").Replace("’", "").Split(new[] { ' ', '-', '_', '.', ',', ':', '(', ')', '/' }, StringSplitOptions.RemoveEmptyEntries);
        return string.Concat(parts.Select(p => char.ToUpperInvariant(p[0]) + p[1..]));
    }

    public static string RangeName(string range) => range.ToLowerInvariant() switch
    {
        "self" => "Self",
        "touch" => "Touch",
        "target" => "Target",
        _ => throw new GeneratorException($"unknown range '{range}'"),
    };
}
