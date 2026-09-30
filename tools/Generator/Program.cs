using LostArt.Generator;
using LostArt.Generator.Build;
using LostArt.Generator.Core;

if (args.Length == 3 && args[0] == "compare")
{
    var r = PluginCompare.Compare(args[1], args[2]);
    if (r.ByteIdentical) Console.WriteLine($"byte-identical: {args[1]} == {args[2]}");
    else if (r.Equivalent) Console.WriteLine($"equivalent: same header, groups and record bytes; record order differs inside {r.ReorderedGroups} group(s) (file-system order of Spriggit's YAML files)");
    else
    {
        Console.WriteLine($"DIFFERENT: {args[1]} vs {args[2]}");
        foreach (var d in r.Differences.Take(40)) Console.WriteLine("  " + d);
    }
    return r.Equivalent ? 0 : 1;
}

var opt = new GeneratorOptions();
for (var i = 0; i < args.Length; i++)
{
    string Next() => i + 1 < args.Length ? args[++i] : throw new GeneratorException($"{args[i]} needs a value");
    switch (args[i])
    {
        case "--data": opt.DataDir = Next(); break;
        case "--out": opt.OutDir = Next(); break;
        case "--formmap": opt.FormMapPath = Next(); break;
        case "--generated": opt.GeneratedDir = Next(); break;
        case "--prune": opt.Prune = true; break;
        case "--base": opt.BaseDir = Next(); break;
        case "--no-formmap-write": opt.WriteFormMap = false; break;
        case "--quiet": opt.Quiet = true; break;
        case "-h":
        case "--help":
            Console.WriteLine("""
                Lost Art of Spellmaking plugin generator
                  dotnet run --project tools/Generator -- --data data --out build/plugins [--formmap data/generated/formmap.json]
                  dotnet run --project tools/Generator -- compare <a.esp> <b.esp>   (round-trip check helper)
                Options:
                  --data <dir>          data/ root (effects, standins, content, translations)
                  --out <dir>           where LostArt.esp and LostArt_Slots.esp are written
                  --formmap <file>      EditorID -> local FormID allocation (read, merged, written back)
                  --generated <dir>     where variants.json goes (default: the formmap's folder)
                  --base <dir>          previous LostArt.esp/LostArt_Slots.esp (e.g. Spriggit-deserialized from plugin/,
                                        edited in CK): records the generator does not produce are kept unchanged
                  --prune               retire formmap entries whose records are no longer generated
                  --no-formmap-write    do not write formmap.json / variants.json (dry run for ids)
                  --quiet               only print errors and the summary
                """);
            return 0;
        default:
            Console.Error.WriteLine($"unknown argument {args[i]} (see --help)");
            return 2;
    }
}

try
{
    var result = PluginGenerator.Run(opt);
    var ctx = result.Context;
    Console.WriteLine();
    Console.WriteLine("== Record counts");
    foreach (var mod in new[] { ctx.Content, ctx.Slots })
    {
        var counts = Validator.CountByType(mod);
        Console.WriteLine($"{mod.ModKey.FileName,-20} total {counts.Values.Sum(),5}  " + string.Join("  ", counts.Select(kv => $"{kv.Key} {kv.Value}")));
    }
    Console.WriteLine($"variants: {ctx.Variants.Sum(v => v.Value.Sum(r => r.Value.Count))} MGEF variants for {ctx.Variants.Count} effects");
    if (ctx.VerifyNotes.Count > 0)
    {
        Console.WriteLine();
        Console.WriteLine("== Verify in CK / in-game");
        foreach (var n in ctx.VerifyNotes) Console.WriteLine(" - " + n);
    }
    Console.WriteLine();
    Console.WriteLine($"{result.Log.Warnings.Count} warning(s), {result.Log.Errors.Count} error(s)");
    if (!result.Success)
    {
        foreach (var e in result.Log.Errors.Take(50)) Console.Error.WriteLine("error: " + e);
        Console.Error.WriteLine("generation FAILED; plugins not written");
        return 1;
    }
    return 0;
}
catch (GeneratorException ex)
{
    Console.Error.WriteLine("error: " + ex.Message);
    return 1;
}
