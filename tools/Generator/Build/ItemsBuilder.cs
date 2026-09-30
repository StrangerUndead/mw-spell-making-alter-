using System.Text.Json.Nodes;
using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace LostArt.Generator.Build;

/// <summary>Globals, spell tomes and their spells, lore books, leveled lists and altar furniture.</summary>
public sealed class ItemsBuilder(BuildContext ctx)
{
    public const string GlobalServiceRefusal = "LA_ServiceRefusal";
    public const string GlobalAltarsEnabled = "LA_AltarsEnabled";
    public const string GlobalSpellmakersEnabled = "LA_SpellmakersEnabled";
    public const string TomeLoot = "LA_TomeLoot";
    public const string AltarCollege = "LA_AltarCollege";
    public const string AltarTelvanni = "LA_AltarTelvanni";

    public static readonly (string EditorId, string TitleKey, string Title)[] LoreBooks =
    {
        ("LA_Book_FragmentsOnTheLostArt", "$LA_Book_FragmentsOnTheLostArt", "Fragments on the Lost Art"),
        ("LA_Book_SpellmakersPrimer", "$LA_Book_SpellmakersPrimer", "The Spellmaker's Primer"),
        ("LA_Book_OnTelvanniInscription", "$LA_Book_OnTelvanniInscription", "On Telvanni Inscription"),
    };

    private Log Log => ctx.Log;

    /// <summary>Tome records built: (book, spell, school, level).</summary>
    public List<(Book Book, Spell Spell, string School, short Level, JsonObject Entry)> Tomes { get; } = new();

    public void BuildGlobals()
    {
        Global(GlobalAltarsEnabled, 1);
        Global(GlobalServiceRefusal, 0);
        Global(GlobalSpellmakersEnabled, 1);
    }

    private void Global(string edid, short value) =>
        ctx.C(edid, fk => new GlobalShort(fk, BuildContext.Release) { Data = value });

    // ------------------------------------------------------------------------------------
    // Tomes
    // ------------------------------------------------------------------------------------

    public void BuildTomes()
    {
        var entries = ctx.Data.ContentList("tomes", "tomes", "entries", "items");
        if (entries.Count == 0) Log.Warn("tomes.json: no tomes");
        var built = new List<(string Pascal, JsonObject Entry)>();
        foreach (var t in entries)
        {
            var pascal = TomePascal(t);
            if (pascal is null) { Log.Error("tomes.json: tome without id/name"); continue; }
            built.Add((pascal, t));
        }
        foreach (var (pascal, t) in built.OrderBy(b => b.Pascal, StringComparer.Ordinal))
            BuildTome(pascal, t);
    }

    public static string? TomePascal(JsonObject t)
    {
        var edid = t.Str("editorId", "bookEditorId");
        if (edid is not null && edid.StartsWith("LA_Tome_")) return edid["LA_Tome_".Length..];
        var id = t.Str("id", "pascal", "key");
        if (id is not null) return id.Contains('_') || id.Contains('.') || char.IsLower(id[0]) ? Naming.SnakeToPascal(id.Split('.').Last()) : Naming.ToPascal(id);
        var name = t.Str("name", "spell", "spellName");
        if (name is null) return null;
        if (name.StartsWith("$")) name = name[(name.LastIndexOf('_') + 1)..];
        return Naming.ToPascal(name);
    }

    private void BuildTome(string pascal, JsonObject t)
    {
        var context = $"tomes:{pascal}";
        var effects = t.Arr("effects") ?? new JsonArray();
        if (effects.Count == 0) { Log.Error($"{context}: no effects"); return; }

        var spellEffects = new ExtendedList<Effect>();
        TargetType? delivery = null;
        string? school = t.Str("school");
        var costliest = -1.0;
        foreach (var node in effects)
        {
            if (node is not JsonObject fx) continue;
            var effectId = fx.Str("effect", "id", "effectId");
            if (effectId is null || !ctx.Data.EffectsById.TryGetValue(effectId, out var e)) { Log.Error($"{context}: unknown effect '{effectId}'"); continue; }
            var range = (fx.Str("range") ?? e.Ranges.FirstOrDefault() ?? "self").ToLowerInvariant();
            if (!e.Ranges.Contains(range)) Log.Error($"{context}: {effectId} does not allow range '{range}'");
            var sub = SubFor(fx, e, context);
            var targetName = TargetNameForSub(e, sub);
            var variantId = EffectBuilder.VariantEditorId(e, range, targetName);
            var d = range == "self" ? TargetType.Self : TargetType.Aimed;
            if (delivery is not null && delivery != d) Log.Error($"{context}: mixes Self and Touch/Target effects; a spell has one delivery (split it into two tomes)");
            delivery ??= d;

            var min = fx.Num("min", "minMag", "magnitude") ?? 0;
            var max = fx.Num("max", "maxMag", "magnitude") ?? min;
            var mag = e.HasMagnitude ? (float)Math.Round((min + max) / 2.0) : 0f;
            var dur = e.HasDuration ? (int)(fx.Num("duration") ?? 0) : 0;
            var area = e.HasArea && range != "self" ? (int)(fx.Num("area") ?? 0) : 0;
            spellEffects.Add(new Effect
            {
                BaseEffect = new FormLinkNullable<IMagicEffectGetter>(ctx.ContentLink(variantId)),
                Data = new EffectData { Magnitude = mag, Duration = dur, Area = area },
            });
            var weight = e.BaseCost * Math.Max(1, mag) * Math.Max(1, dur);
            if (weight > costliest) { costliest = weight; school ??= e.School; if (t.Str("school") is null) school = e.School; }
        }
        if (spellEffects.Count == 0) return;
        school ??= "Alteration";

        var spellName = ctx.Tr.Resolve(t.Str("spellName", "name", "spell"), EffectBuilder.SplitPascal(pascal));
        var rank = RankPerk(t.Str("rank"), school, t.Num("level"));
        var cost = t.Num("cost");
        var spell = ctx.C($"LA_TomeSpell_{pascal}", fk => new Spell(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            Name = spellName,
            EquipmentType = new FormLinkNullable<IEquipTypeGetter>(ctx.Vanilla_("EquipType", "EitherHand")),
            Description = "",
            BaseCost = cost is null ? 0u : (uint)Math.Round(cost.Value),
            Flags = cost is null ? 0 : SpellDataFlag.ManualCostCalc,
            Type = SpellType.Spell,
            ChargeTime = 0.5f,
            CastType = CastType.FireAndForget,
            TargetType = delivery ?? TargetType.Self,
            CastDuration = 0f,
            Range = 0f,
            HalfCostPerk = new FormLink<IPerkGetter>(ctx.Vanilla_("Perk", rank)),
            Effects = spellEffects,
        });

        var bookTitle = ctx.Tr.Resolve(t.Str("title", "bookName", "tomeName"), $"Spell Tome: {spellName}");
        var level = (short)(t.Num("level", "lootLevel") ?? 1);
        var value = (uint)(t.Num("value", "price", "gold") ?? DefaultValue(rank));
        var book = ctx.C($"LA_Tome_{pascal}", fk => new Book(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            Name = bookTitle,
            Model = new Model { File = $@"Clutter\Books\SpellTome{school}LowPoly.nif" },
            BookText = ctx.Tr.Resolve(t.Str("text", "bookText"), ""),
            PickUpSound = new FormLinkNullable<ISoundDescriptorGetter>(ctx.Vanilla_("SoundDescriptor", "ITMGenericBookUpSD")),
            PutDownSound = new FormLinkNullable<ISoundDescriptorGetter>(ctx.Vanilla_("SoundDescriptor", "ITMGenericBookDownSD")),
            Keywords = Links.Keywords(new[] { ctx.Vanilla_("Keyword", "VendorItemSpellTome") }),
            Flags = 0,
            Type = Book.BookType.BookOrTome,
            Teaches = new BookSpell { Spell = new FormLink<ISpellGetter>(spell.FormKey) },
            Value = value,
            Weight = 1f,
            InventoryArt = new FormLinkNullable<IStaticGetter>(ctx.Vanilla_("Static", $"HighPoly{school}Book")),
            Description = "",
        });
        Tomes.Add((book, spell, school, level, t));
    }

    private static int SubFor(JsonObject fx, EffectDef e, string context)
    {
        var sub = fx.Num("sub");
        if (sub is not null) return (int)sub.Value;
        var target = fx.Str("target", "attribute", "skill");
        if (target is null) return -1;
        var p = Naming.ToPascal(target);
        var i = Array.FindIndex(Mappings.Attributes, a => a.Equals(p, StringComparison.OrdinalIgnoreCase));
        if (e.Target == "attribute" && i >= 0) return i;
        i = Array.FindIndex(Mappings.SkyrimSkills, a => a.Equals(p, StringComparison.OrdinalIgnoreCase));
        if (e.Target == "skill" && i >= 0) return i;
        i = Array.FindIndex(Mappings.MorrowindSkills, a => a.Equals(p, StringComparison.OrdinalIgnoreCase));
        if (e.Target == "skill" && i >= 0) return 100 + i;
        return -1;
    }

    private static string? TargetNameForSub(EffectDef e, int sub)
    {
        if (e.Target == "attribute") return sub is >= 0 and < 8 ? Mappings.Attributes[sub] : Mappings.Attributes[0];
        if (e.Target == "skill")
        {
            if (sub is >= 0 and < 18) return Mappings.SkyrimSkills[sub];
            if (sub is >= 100 and < 127) return Mappings.MorrowindSkills[sub - 100];
            return Mappings.SkyrimSkills[0];
        }
        return null;
    }

    private static string RankPerk(string? rank, string school, double? level)
    {
        var r = (rank ?? "").ToLowerInvariant() switch
        {
            "novice" => "Novice00",
            "apprentice" => "Apprentice25",
            "adept" => "Adept50",
            "expert" => "Expert75",
            "master" => "Master100",
            _ => level switch
            {
                >= 40 => "Expert75",
                >= 25 => "Adept50",
                >= 10 => "Apprentice25",
                _ => "Novice00",
            },
        };
        return school + r;
    }

    private static uint DefaultValue(string rankPerk) => rankPerk switch
    {
        _ when rankPerk.EndsWith("Novice00") => 50,
        _ when rankPerk.EndsWith("Apprentice25") => 100,
        _ when rankPerk.EndsWith("Adept50") => 330,
        _ when rankPerk.EndsWith("Expert75") => 685,
        _ => 1200,
    };

    // ------------------------------------------------------------------------------------
    // Lore books
    // ------------------------------------------------------------------------------------

    public void BuildLoreBooks()
    {
        var models = new[] { @"Clutter\Books\BasicBook01.nif", @"Clutter\Books\BasicBook03.nif", @"Clutter\Books\BasicBook05.nif" };
        var arts = new[] { "HighPolyBasicBook01", "HighPolyBasicBook03", "HighPolyBasicBook05" };
        for (var i = 0; i < LoreBooks.Length; i++)
        {
            var (edid, titleKey, title) = LoreBooks[i];
            var textKey = titleKey + "_Text";
            var text = ctx.Tr.Get(textKey);
            if (text is null)
            {
                text = LoreText(edid) ?? $"<p align='center'>{title}</p><p>[Placeholder text - see docs/OUTLINE.md \"Icons and text\".]</p>";
                if (LoreText(edid) is null) ctx.Verify($"BOOK {edid}: body text is a placeholder (no {textKey} in translations or docs/lore)");
            }
            var model = models[i];
            var art = arts[i];
            ctx.C(edid, fk => new Book(fk, BuildContext.Release)
            {
                ObjectBounds = new ObjectBounds(),
                Name = ctx.Tr.Resolve(titleKey, title),
                Model = new Model { File = model },
                BookText = text.Replace("\\n", "\n"),
                PickUpSound = new FormLinkNullable<ISoundDescriptorGetter>(ctx.Vanilla_("SoundDescriptor", "ITMGenericBookUpSD")),
                PutDownSound = new FormLinkNullable<ISoundDescriptorGetter>(ctx.Vanilla_("SoundDescriptor", "ITMGenericBookDownSD")),
                Keywords = Links.Keywords(new[] { ctx.Vanilla_("Keyword", "VendorItemBook") }),
                Flags = 0,
                Type = Book.BookType.BookOrTome,
                Teaches = new BookTeachesNothing(),
                Value = 40,
                Weight = 1f,
                InventoryArt = new FormLinkNullable<IStaticGetter>(ctx.Vanilla_("Static", art)),
                Description = "",
            });
        }
        ctx.Verify("BOOK models (Clutter\\Books\\SpellTome<School>LowPoly.nif, BasicBook0x.nif) are hand-typed vanilla paths - verify; new per-school tome cover textures are an art task");
    }

    /// <summary>Optional lore text from docs/lore/&lt;EditorID&gt;.txt (next to data/), else null.</summary>
    private string? LoreText(string edid)
    {
        var path = Path.Combine(ctx.Data.Root, "..", "docs", "lore", edid + ".txt");
        return File.Exists(path) ? File.ReadAllText(path).Replace("\r\n", "\n").TrimEnd() : null;
    }

    // ------------------------------------------------------------------------------------
    // Leveled lists (the DLL injects them into vanilla lists / vendor chests at runtime)
    // ------------------------------------------------------------------------------------

    public void BuildLeveledLists()
    {
        var loot = ctx.Data.ContentFile("tomes").Obj("loot");
        var chanceNone = (byte)(loot.Num("chanceNone") ?? 0);
        ctx.C(TomeLoot, fk => new LeveledItem(fk, BuildContext.Release)
        {
            ObjectBounds = new ObjectBounds(),
            ChanceNone = new Noggog.Percent(chanceNone / 100.0),
            Flags = LeveledItem.Flag.CalculateFromAllLevelsLessThanOrEqualPlayer,
            Entries = Tomes.Where(t => t.Entry.Bool("loot") != false)
                .OrderBy(t => t.Level).ThenBy(t => t.Book.EditorID, StringComparer.Ordinal)
                .Select(t => Entry(t.Book.FormKey, t.Level)).ToExtendedList(),
        });

        foreach (var school in Mappings.Schools)
        {
            var tomes = Tomes.Where(t => t.School == school && t.Entry.Bool("vendor") != false).ToList();
            if (tomes.Count == 0) continue;
            ctx.C($"LA_VendorTomes_{school}", fk => new LeveledItem(fk, BuildContext.Release)
            {
                ObjectBounds = new ObjectBounds(),
                ChanceNone = new Noggog.Percent(0),
                Flags = LeveledItem.Flag.CalculateFromAllLevelsLessThanOrEqualPlayer | LeveledItem.Flag.UseAll,
                Entries = tomes.OrderBy(t => t.Level).ThenBy(t => t.Book.EditorID, StringComparer.Ordinal)
                    .Select(t => Entry(t.Book.FormKey, t.Level)).ToExtendedList(),
            });
        }
    }

    private static LeveledItemEntry Entry(FormKey item, short level) => new()
    {
        Data = new LeveledItemEntryData { Level = level, Count = 1, Reference = new FormLink<IItemGetter>(item) },
    };

    // ------------------------------------------------------------------------------------
    // Altars (placed at runtime by the DLL - no CELL edits)
    // ------------------------------------------------------------------------------------

    public void BuildAltars()
    {
        var altars = ctx.Data.ContentList("altars", "altars", "entries");
        foreach (var (edid, fallbackName, key) in new[] { (AltarCollege, "Altar of Spellmaking", "college"), (AltarTelvanni, "Telvanni Altar of Spellmaking", "telvanni") })
        {
            var entry = altars.FirstOrDefault(a => (a.Str("editorId", "furniture", "id") ?? "").Contains(key, StringComparison.OrdinalIgnoreCase)
                                                   || (a.Str("editorId", "furniture") ?? "") == edid);
            var name = ctx.Tr.Resolve(entry?.Str("name"), ctx.Tr.Resolve($"${edid}", fallbackName));
            var model = entry?.Str("model") ?? @"Furniture\Workstations\EnchantingWorkbench01.nif";
            ctx.C(edid, fk => new Furniture(fk, BuildContext.Release)
            {
                ObjectBounds = new ObjectBounds
                {
                    First = new P3Int16(-60, -40, 0),
                    Second = new P3Int16(60, 40, 90),
                },
                Name = name,
                Model = new Model { File = model },
                Keywords = Links.Keywords(new[] { ctx.Vanilla_("Keyword", "isEnchanting") }),
                Flags = Furniture.Flag.MustExitToTalk,
                WorkbenchData = new WorkbenchData { BenchType = WorkbenchData.Type.None },
                Markers = new ExtendedList<FurnitureMarker>
                {
                    new FurnitureMarker
                    {
                        Enabled = true,
                        DisabledEntryPoints = new EntryPoints { Type = default, Points = 0 },
                        EntryPoints = new EntryPoints { Type = default, Points = Furniture.Entry.Front },
                    },
                },
            });
        }
        ctx.Verify("FURN LA_AltarCollege/LA_AltarTelvanni: use the enchanting table mesh (Furniture\\Workstations\\EnchantingWorkbench01.nif, hand-typed) and one front marker; " +
                   "BenchType None so the vanilla enchanting menu never opens - in CK confirm the entry/idle animation plays (FNPR marker keyword / isEnchanting), " +
                   "then swap in the final altar meshes (College plinth, Telvanni mushroom lectern) and bounds");
    }
}
