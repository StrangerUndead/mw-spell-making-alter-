using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator.Build;

/// <summary>Shared state for one generator run: both mods, the FormID map and lookups.</summary>
public sealed class BuildContext
{
    public const string ContentName = "LostArt.esp";
    public const string SlotsName = "LostArt_Slots.esp";
    public static readonly ModKey ContentKey = ModKey.FromFileName(ContentName);
    public static readonly ModKey SlotsKey = ModKey.FromFileName(SlotsName);
    public const SkyrimRelease Release = SkyrimRelease.SkyrimSE;
    public const ushort FormVersion = 44;

    public Log Log { get; }
    public DataSet Data { get; }
    public Translations Tr => Data.Translations;
    public VanillaIndex Vanilla { get; } = VanillaIndex.Instance;
    public FormMap FormMap { get; }

    public SkyrimMod Content { get; }
    public SkyrimMod Slots { get; }

    /// <summary>Every generated record by EditorID (both plugins).</summary>
    public Dictionary<string, ISkyrimMajorRecord> Records { get; } = new(StringComparer.Ordinal);
    private readonly List<(ModKey Plugin, ISkyrimMajorRecord Record)> _pending = new();

    /// <summary>effectId -> range -> sub -> MGEF EditorID (written to data/generated/variants.json).</summary>
    public SortedDictionary<string, SortedDictionary<string, SortedDictionary<int, string>>> Variants { get; } = new(StringComparer.Ordinal);

    /// <summary>Human-facing notes on fields that need Creation Kit / in-game verification.</summary>
    public SortedSet<string> VerifyNotes { get; } = new(StringComparer.Ordinal);

    public BuildContext(DataSet data, FormMap formMap, Log log)
    {
        Data = data;
        FormMap = formMap;
        Log = log;
        Content = NewMod(ContentKey);
        Slots = NewMod(SlotsKey);
    }

    private static SkyrimMod NewMod(ModKey key)
    {
        var mod = new SkyrimMod(key, Release, headerVersion: 1.7f);
        mod.ModHeader.Stats.Version = 1.7f;
        mod.ModHeader.Flags |= SkyrimModHeader.HeaderFlag.Small;
        mod.ModHeader.Author = "Lost Art of Spellmaking generator";
        return mod;
    }

    public SkyrimMod Mod(ModKey plugin) => plugin == SlotsKey ? Slots : Content;

    /// <summary>Stable FormKey for an EditorID (allocates in the formmap if new).</summary>
    public FormKey Key(ModKey plugin, string editorId)
    {
        if (!editorId.StartsWith("LA_", StringComparison.Ordinal))
            throw new GeneratorException($"generated EditorID must start with LA_: {editorId}");
        return new FormKey(plugin, FormMap.Get(plugin.FileName, editorId));
    }

    public FormKey ContentLink(string editorId) => Key(ContentKey, editorId);

    /// <summary>Creates a record with a stable FormKey; it is added to the mod in FormID order at the end.</summary>
    public T Create<T>(ModKey plugin, string editorId, Func<FormKey, T> factory) where T : ISkyrimMajorRecord
    {
        if (Records.ContainsKey(editorId)) throw new GeneratorException($"duplicate EditorID {editorId}");
        var fk = Key(plugin, editorId);
        var rec = factory(fk);
        rec.EditorID = editorId;
        if (rec is SkyrimMajorRecord smr) smr.FormVersion = FormVersion;
        Records[editorId] = rec;
        _pending.Add((plugin, rec));
        return rec;
    }

    public T C<T>(string editorId, Func<FormKey, T> factory) where T : ISkyrimMajorRecord => Create(ContentKey, editorId, factory);

    /// <summary>
    /// Sort key for records inside a group: the EditorID compared like Spriggit's file names
    /// ("&lt;EditorID&gt; - &lt;FormID&gt;_&lt;Plugin&gt;.yaml") on a case-insensitive file system, so a
    /// Spriggit deserialize on Windows reproduces the generated plugin byte for byte.
    /// </summary>
    public static string SortKey(string editorId) => (editorId + " - ").ToUpperInvariant();

    /// <summary>Adds all created records to their mods, sorted by EditorID (deterministic output).</summary>
    public void Commit()
    {
        foreach (var (plugin, rec) in _pending.OrderBy(p => p.Plugin.FileName.String, StringComparer.Ordinal).ThenBy(p => SortKey(p.Record.EditorID!), StringComparer.Ordinal))
        {
            var mod = Mod(plugin);
            switch (rec)
            {
                case Keyword r: mod.Keywords.Add(r); break;
                case MagicEffect r: mod.MagicEffects.Add(r); break;
                case Spell r: mod.Spells.Add(r); break;
                case Projectile r: mod.Projectiles.Add(r); break;
                case Explosion r: mod.Explosions.Add(r); break;
                case Book r: mod.Books.Add(r); break;
                case Global r: mod.Globals.Add(r); break;
                case Npc r: mod.Npcs.Add(r); break;
                case Weapon r: mod.Weapons.Add(r); break;
                case Armor r: mod.Armors.Add(r); break;
                case Furniture r: mod.Furniture.Add(r); break;
                case Quest r: mod.Quests.Add(r); break;
                case DialogBranch r: mod.DialogBranches.Add(r); break;
                case DialogTopic r: mod.DialogTopics.Add(r); break;
                case LeveledItem r: mod.LeveledItems.Add(r); break;
                case FormList r: mod.FormLists.Add(r); break;
                case Message r: mod.Messages.Add(r); break;
                case DialogResponses: break; // lives inside its DialogTopic
                default: throw new GeneratorException($"no group mapping for {rec.GetType().Name}");
            }
        }
        _pending.Clear();
    }

    // ---- resolution of references written in data -------------------------------------

    /// <summary>
    /// Resolves a reference from data: "Skyrim.esm|0x012FD0", an LA_ EditorID of a generated
    /// record, or a vanilla EditorID (looked up in the given FormKeys record classes).
    /// </summary>
    public FormKey? Resolve(string? reference, string context, params string[] vanillaTypes)
    {
        if (string.IsNullOrWhiteSpace(reference)) return null;
        var fk = VanillaIndex.ParseRef(reference);
        if (fk is not null)
        {
            if (fk.Value.ModKey == ContentKey || fk.Value.ModKey == SlotsKey) return fk;
            if (!Vanilla.Exists(fk.Value)) Log.Error($"{context}: {reference} is not a known vanilla FormKey (Mutagen.Bethesda.FormKeys.SkyrimSE)");
            return fk;
        }
        if (reference.StartsWith("LA_", StringComparison.Ordinal))
        {
            // Generated record: allocate/resolve lazily; existence is checked in validation.
            var plugin = reference.StartsWith("LA_Slot_") || reference.StartsWith("LA_Sub_") || reference is "LA_BlankEffect" or "LA_KW_CustomSpell" ? SlotsKey : ContentKey;
            return Key(plugin, reference);
        }
        foreach (var t in vanillaTypes)
        {
            var hit = Vanilla.Find(t, reference);
            if (hit is not null) return hit;
        }
        if (vanillaTypes.Length == 0)
        {
            var any = Vanilla.FindAny(reference).ToList();
            if (any.Count == 1) return any[0].Key;
            if (any.Count > 1) { Log.Error($"{context}: vanilla EditorID '{reference}' is ambiguous ({string.Join(", ", any.Select(a => a.Type))})"); return any[0].Key; }
        }
        Log.Error($"{context}: cannot resolve '{reference}' ({(vanillaTypes.Length == 0 ? "any type" : string.Join("/", vanillaTypes))})");
        return null;
    }

    public FormKey Vanilla_(string type, string editorId)
    {
        return Vanilla.Find(type, editorId) ?? throw new GeneratorException($"vanilla {type} '{editorId}' not in Mutagen.Bethesda.FormKeys.SkyrimSE");
    }

    public void Verify(string note) => VerifyNotes.Add(note);
}
