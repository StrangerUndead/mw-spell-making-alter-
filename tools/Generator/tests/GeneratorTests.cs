using System.Text.Json.Nodes;
using LostArt.Generator.Build;
using LostArt.Generator.Core;
using Mutagen.Bethesda.Skyrim;
using Xunit;

namespace LostArt.Generator.Tests;

public sealed class TempDir : IDisposable
{
    public string Path { get; } = Directory.CreateTempSubdirectory("lostart-gen-").FullName;
    public void Dispose() { try { Directory.Delete(Path, true); } catch { /* best effort */ } }
}

public static class Fixture
{
    public static string DataDir => System.IO.Path.Combine(AppContext.BaseDirectory, "fixtures", "data");

    /// <summary>The repository's real data/ folder, if the effect catalog is present.</summary>
    public static string? RepoDataDir
    {
        get
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir is not null && !File.Exists(System.IO.Path.Combine(dir.FullName, "docs", "dev", "CONTRACTS.md"))) dir = dir.Parent;
            if (dir is null) return null;
            var data = System.IO.Path.Combine(dir.FullName, "data");
            return Directory.Exists(System.IO.Path.Combine(data, "effects")) && Directory.GetFiles(System.IO.Path.Combine(data, "effects"), "*.json").Length > 0 ? data : null;
        }
    }

    public static GeneratorResult Run(string dataDir, string outDir, string formMap, string? baseDir = null) =>
        PluginGenerator.Run(new GeneratorOptions { DataDir = dataDir, OutDir = outDir, FormMapPath = formMap, Quiet = true, BaseDir = baseDir });
}

public class StableFormIdTests
{
    [Fact]
    public void RegeneratingWithTheSameFormMapIsByteIdentical()
    {
        using var tmp = new TempDir();
        var map = Path.Combine(tmp.Path, "gen", "formmap.json");
        var r1 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "a"), map);
        Assert.True(r1.Success, string.Join("\n", r1.Log.Errors));
        var mapText1 = File.ReadAllText(map);
        var r2 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "b"), map);
        Assert.True(r2.Success, string.Join("\n", r2.Log.Errors));
        Assert.Equal(mapText1, File.ReadAllText(map));
        foreach (var p in new[] { "LostArt.esp", "LostArt_Slots.esp" })
            Assert.Equal(File.ReadAllBytes(Path.Combine(tmp.Path, "a", p)), File.ReadAllBytes(Path.Combine(tmp.Path, "b", p)));
    }

    [Fact]
    public void ExistingIdsNeverMoveAndNewIdsTakeTheNextFreeOne()
    {
        using var tmp = new TempDir();
        var map = Path.Combine(tmp.Path, "gen", "formmap.json");
        var r1 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "a"), map);
        Assert.True(r1.Success);

        // Simulate an older formmap: move one record to an unusual id and drop another.
        var root = JsonNode.Parse(File.ReadAllText(map))!.AsObject();
        var content = root["LostArt.esp"]!.AsObject();
        content["LA_FireDamage_Target"] = "0x000FF0";
        var dropped = content["LA_Open_Target"]!.GetValue<string>();
        content.Remove("LA_Open_Target");
        File.WriteAllText(map, root.ToJsonString());
        var maxBefore = content.Select(kv => FormMap.ParseId(kv.Value!.GetValue<string>())).Max();

        var r2 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "b"), map);
        Assert.True(r2.Success, string.Join("\n", r2.Log.Errors));
        var after = JsonNode.Parse(File.ReadAllText(map))!["LostArt.esp"]!.AsObject();
        Assert.Equal("0x000FF0", after["LA_FireDamage_Target"]!.GetValue<string>());
        Assert.Equal(maxBefore + 1, FormMap.ParseId(after["LA_Open_Target"]!.GetValue<string>()));
        Assert.NotEqual(dropped, after["LA_Open_Target"]!.GetValue<string>());

        using var mod = SkyrimMod.CreateFromBinaryOverlay(Path.Combine(tmp.Path, "b", "LostArt.esp"), SkyrimRelease.SkyrimSE);
        Assert.Equal(0xFF0u, mod.MagicEffects.Single(m => m.EditorID == "LA_FireDamage_Target").FormKey.ID);
    }

    [Fact]
    public void RetiredIdsAreNotReused()
    {
        using var tmp = new TempDir();
        var path = Path.Combine(tmp.Path, "formmap.json");
        var fm = FormMap.Load(path);
        Assert.Equal(0x800u, fm.Get("X.esp", "LA_A"));
        Assert.Equal(0x801u, fm.Get("X.esp", "LA_B"));
        fm.Save(new[] { "X.esp" });

        var fm2 = FormMap.Load(path);
        Assert.Equal(0x800u, fm2.Get("X.esp", "LA_A"));
        Assert.Equal(1, fm2.Prune("X.esp")); // LA_B not requested -> retired
        fm2.Save(new[] { "X.esp" });

        var fm3 = FormMap.Load(path);
        Assert.Equal(0x802u, fm3.Get("X.esp", "LA_C")); // 0x801 stays burned
        Assert.Equal(0x801u, fm3.Get("X.esp", "LA_B")); // coming back restores its old id
    }
}

public class HandMadeRecordTests
{
    [Fact]
    public void RecordsAuthoredInCkSurviveRegeneration()
    {
        using var tmp = new TempDir();
        var map = Path.Combine(tmp.Path, "formmap.json");
        var r1 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "gen1"), map);
        Assert.True(r1.Success);

        // "CK edit": add a static to the generated plugin, as Spriggit would deserialize it from plugin/.
        var baseDir = Path.Combine(tmp.Path, "base");
        Directory.CreateDirectory(baseDir);
        var mod = SkyrimMod.CreateFromBinary(r1.ContentPath, SkyrimRelease.SkyrimSE);
        var id = mod.ModHeader.Stats.NextFormID;
        var stat = new Static(new Mutagen.Bethesda.Plugins.FormKey(mod.ModKey, id), SkyrimRelease.SkyrimSE) { EditorID = "LA_AltarFocusStone" };
        mod.Statics.Add(stat);
        PluginGenerator.Write(mod, Path.Combine(baseDir, "LostArt.esp"));
        File.Copy(r1.SlotsPath, Path.Combine(baseDir, "LostArt_Slots.esp"));

        var r2 = Fixture.Run(Fixture.DataDir, Path.Combine(tmp.Path, "gen2"), map, baseDir);
        Assert.True(r2.Success, string.Join("\n", r2.Log.Errors));
        using var outMod = SkyrimMod.CreateFromBinaryOverlay(r2.ContentPath, SkyrimRelease.SkyrimSE);
        Assert.Equal(id, outMod.Statics.Single(s => s.EditorID == "LA_AltarFocusStone").FormKey.ID);
        Assert.Equal(FormMap.FormatId(id), JsonNode.Parse(File.ReadAllText(map))!["LostArt.esp"]!["LA_AltarFocusStone"]!.GetValue<string>());
        // Everything generated is unchanged apart from the kept record.
        Assert.Equal(outMod.EnumerateMajorRecords().Count() - 1, SkyrimMod.CreateFromBinaryOverlay(r1.ContentPath, SkyrimRelease.SkyrimSE).EnumerateMajorRecords().Count());
    }
}

public class EslRangeTests
{
    [Fact]
    public void AllRecordsAreInEslRangeWithCorrectHeader()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        Assert.True(r.Success, string.Join("\n", r.Log.Errors));
        foreach (var p in new[] { r.ContentPath, r.SlotsPath })
        {
            using var mod = SkyrimMod.CreateFromBinaryOverlay(p, SkyrimRelease.SkyrimSE);
            Assert.True(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
            Assert.False(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Localized));
            Assert.Equal(1.7f, mod.ModHeader.Stats.Version, 3);
            var records = mod.EnumerateMajorRecords().ToList();
            Assert.True(records.Count <= 2048);
            Assert.All(records, rec =>
            {
                Assert.InRange(rec.FormKey.ID, 0x800u, 0xFFFu);
                Assert.Equal(mod.ModKey, rec.FormKey.ModKey); // no overrides of vanilla records
                Assert.Equal((ushort)44, rec.FormVersion);
            });
        }
    }

    [Fact]
    public void SlotsPluginHas1000BlankSelfSpells()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(r.SlotsPath, SkyrimRelease.SkyrimSE);
        var spells = mod.Spells.ToList();
        Assert.Equal(1000, spells.Count);
        Assert.Equal(500, spells.Count(s => s.EditorID!.StartsWith("LA_Slot_")));
        Assert.Equal(500, spells.Count(s => s.EditorID!.StartsWith("LA_Sub_")));
        var blank = mod.MagicEffects.Single(m => m.EditorID == "LA_BlankEffect");
        Assert.True(blank.Flags.HasFlag(MagicEffect.Flag.HideInUI));
        Assert.All(spells, s =>
        {
            Assert.Equal(SpellType.Spell, s.Type);
            Assert.Equal(CastType.FireAndForget, s.CastType);
            Assert.Equal(TargetType.Self, s.TargetType);
            Assert.True(s.Flags.HasFlag(SpellDataFlag.ManualCostCalc));
            Assert.Equal(0u, s.BaseCost);
            Assert.Single(s.Effects);
            Assert.Equal(blank.FormKey, s.Effects[0].BaseEffect.FormKey);
            Assert.Equal(0f, s.Effects[0].Data!.Magnitude);
        });
        Assert.Equal(new[] { "Skyrim.esm" }, mod.ModHeader.MasterReferences.Select(m => m.Master.FileName.String));
    }
}

public class VariantTests
{
    /// <summary>Independent expectation: sum over effects of |ranges| x |targets|.</summary>
    private static int ExpectedVariants(string dataDir)
    {
        var special = 0;
        var skills = Path.Combine(dataDir, "content", "skills.json");
        var skyrimSkills = new HashSet<string>(Mappings.SkyrimSkills, StringComparer.OrdinalIgnoreCase);
        var data = LostArt.Generator.Data.DataSet.Load(dataDir, new Log { Quiet = true });
        special = EffectBuilder.SpecialMorrowindSkills(data).Count;
        var total = 0;
        foreach (var file in Directory.GetFiles(Path.Combine(dataDir, "effects"), "*.json"))
            foreach (var e in JsonNode.Parse(File.ReadAllText(file), documentOptions: Json.DocOptions)!.AsArray())
            {
                var ranges = e!["ranges"]!.AsArray().Count;
                var targets = (e["target"]?.GetValue<string>() ?? "none") switch { "attribute" => 8, "skill" => 18 + special, _ => 1 };
                total += ranges * targets;
            }
        return total;
    }

    [Fact]
    public void VariantCountMatchesCatalog()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        Assert.True(r.Success, string.Join("\n", r.Log.Errors));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(r.ContentPath, SkyrimRelease.SkyrimSE);
        Assert.Equal(ExpectedVariants(Fixture.DataDir), mod.MagicEffects.Count);
        // fixture: 2 special Morrowind skills (Acrobatics, Hand-to-Hand) on Fortify Skill
        Assert.Contains(mod.MagicEffects, m => m.EditorID == "LA_FortifySkill_Acrobatics_Self");
        Assert.Contains(mod.MagicEffects, m => m.EditorID == "LA_FortifySkill_HandToHand_Target");
        Assert.DoesNotContain(mod.MagicEffects, m => m.EditorID == "LA_FortifySkill_LongBlade_Self");
    }

    [Fact]
    public void VariantFieldsFollowTheRangeRules()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(r.ContentPath, SkyrimRelease.SkyrimSE);
        var touchProj = mod.Projectiles.Single(p => p.EditorID == "LA_TouchProjectile");
        Assert.Equal(192f, touchProj.Range);
        var m = mod.MagicEffects.ToDictionary(x => x.EditorID!);
        Assert.Equal(TargetType.Self, m["LA_FireDamage_Self"].TargetType);
        Assert.Equal(TargetType.Aimed, m["LA_FireDamage_Touch"].TargetType);
        Assert.Equal(touchProj.FormKey, m["LA_FireDamage_Touch"].Projectile.FormKey);
        Assert.Equal("Skyrim.esm", m["LA_FireDamage_Target"].Projectile.FormKey.ModKey.FileName.String);
        Assert.All(m.Values, x => Assert.Equal(CastType.FireAndForget, x.CastType));
        // Ticking damage: no Recover; held modifier (Shield): Recover.
        Assert.False(m["LA_FireDamage_Target"].Flags.HasFlag(MagicEffect.Flag.Recover));
        Assert.True(m["LA_Shield_Self"].Flags.HasFlag(MagicEffect.Flag.Recover));
        Assert.True(m["LA_FortifySkill_OneHanded_Self"].Flags.HasFlag(MagicEffect.Flag.Recover));
        Assert.Equal(ActorValue.OneHanded, m["LA_FortifySkill_OneHanded_Self"].Archetype.ActorValue);
        // Custom tier: Script archetype + LA_KW_Custom.
        var kwCustom = mod.Keywords.Single(k => k.EditorID == "LA_KW_Custom").FormKey;
        Assert.IsAssignableFrom<IMagicEffectArchetypeGetter>(m["LA_Levitate_Self"].Archetype);
        Assert.Contains(m["LA_Levitate_Self"].Keywords!, k => k.FormKey == kwCustom);
        Assert.Contains(m["LA_FortifySkill_Acrobatics_Self"].Keywords!, k => k.FormKey == kwCustom);
        // Stand-in summon points at the generated NPC.
        var scamp = mod.Npcs.Single(n => n.EditorID == "LA_StandIn_SummonScamp");
        var summon = Assert.IsAssignableFrom<IMagicEffectSummonCreatureArchetypeGetter>(m["LA_SummonScamp_Self"].Archetype);
        Assert.Equal(scamp.FormKey, summon.Association.FormKey);
    }

    [Fact]
    public void BoundItemsAndLinkedSpells()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        Assert.True(r.Success, string.Join("\n", r.Log.Errors));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(r.ContentPath, SkyrimRelease.SkyrimSE);
        var m = mod.MagicEffects.ToDictionary(x => x.EditorID!);
        // Vanilla bound weapon (Dragonborn's bound dagger) is used directly; Daedric templates get a new WEAP.
        var dagger = Assert.IsAssignableFrom<IMagicEffectBoundArchetypeGetter>(m["LA_BoundDagger_Self"].Archetype);
        Assert.Equal("Dragonborn.esm", dagger.Association.FormKey.ModKey.FileName.String);
        var spear = Assert.IsAssignableFrom<IMagicEffectBoundArchetypeGetter>(m["LA_BoundSpear_Self"].Archetype);
        Assert.Equal(mod.Weapons.Single(w => w.EditorID == "LA_Bound_Spear").FormKey, spear.Association.FormKey);
        Assert.Contains(mod.Armors, a => a.EditorID == "LA_Bound_Boots" && a.Weight == 0f);
        // Mixed-range tome: primary Aimed spell + linked Self sub-spell, each delivery-consistent.
        var primary = mod.Spells.Single(s => s.EditorID == "LA_TomeSpell_OndusisOpenDoor");
        var sub = mod.Spells.Single(s => s.EditorID == "LA_TomeSpell_OndusisOpenDoor_Self");
        Assert.Equal(TargetType.Aimed, primary.TargetType);
        Assert.Equal(TargetType.Self, sub.TargetType);
        Assert.Equal(40u, primary.BaseCost);
        Assert.Equal(0u, sub.BaseCost);
        var book = mod.Books.Single(b => b.EditorID == "LA_Tome_OndusisOpenDoor");
        Assert.Equal(primary.FormKey, Assert.IsAssignableFrom<IBookSpellGetter>(book.Teaches).Spell.FormKey);
        // One refuse INFO per spellmaker + shared serve/refuse.
        var topic = mod.DialogTopics.Single(t => t.EditorID == "LA_Topic_MakeSpell");
        Assert.Equal(2 + 3, topic.Responses.Count);
        Assert.Contains(topic.Responses, i => i.EditorID == "LA_Info_MakeSpell_Serve");
        Assert.Contains(topic.Responses, i => i.EditorID == "LA_Info_MakeSpell_Refuse_Tolfdir");
        Assert.Contains(mod.Quests, q => q.EditorID == "LA_MCMQuest" && q.Flags.HasFlag(Quest.Flag.StartGameEnabled) && q.VirtualMachineAdapter!.Scripts[0].Name == "LostArt_MCM");
    }

    [Fact]
    public void RealCatalogCompilesWhenPresent()
    {
        var data = Fixture.RepoDataDir;
        if (data is null) return; // the effect catalog is authored separately; nothing to check yet
        using var tmp = new TempDir();
        var r = Fixture.Run(data, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        Assert.True(r.Success, string.Join("\n", r.Log.Errors));
        using var mod = SkyrimMod.CreateFromBinaryOverlay(r.ContentPath, SkyrimRelease.SkyrimSE);
        Assert.Equal(ExpectedVariants(data), mod.MagicEffects.Count);
        Assert.True(mod.EnumerateMajorRecords().Count() <= 2048);
    }
}

public class CompareTests
{
    [Fact]
    public void CompareDetectsIdentityAndDifferences()
    {
        using var tmp = new TempDir();
        var r = Fixture.Run(Fixture.DataDir, tmp.Path, Path.Combine(tmp.Path, "formmap.json"));
        Assert.True(PluginCompare.Compare(r.ContentPath, r.ContentPath).ByteIdentical);
        var res = PluginCompare.Compare(r.ContentPath, r.SlotsPath);
        Assert.False(res.Equivalent);
    }
}
