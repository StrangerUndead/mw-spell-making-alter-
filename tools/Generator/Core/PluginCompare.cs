using System.Buffers.Binary;

namespace LostArt.Generator.Core;

/// <summary>
/// Compares two plugin files: byte-identical, or equivalent (identical header, group sequence
/// and record bytes, differing only in the order of records inside a group - which is what a
/// Spriggit deserialize on a file system with non-alphabetical directory order produces).
/// </summary>
public static class PluginCompare
{
    public sealed record Result(bool ByteIdentical, bool Equivalent, int ReorderedGroups, List<string> Differences);

    private sealed class Node
    {
        public required string Type;
        public required byte[] Header;          // 24-byte record/group header
        public byte[] Body = Array.Empty<byte>(); // record body (records only)
        public List<Node> Children = new();       // groups only
        public uint FormId => BinaryPrimitives.ReadUInt32LittleEndian(Header.AsSpan(12));
        public string Label => Type == "GRUP" ? $"GRUP[{System.Text.Encoding.Latin1.GetString(Header, 8, 4)}/{BinaryPrimitives.ReadInt32LittleEndian(Header.AsSpan(12))}]" : $"{Type} {FormId:X8}";
    }

    public static Result Compare(string pathA, string pathB)
    {
        var a = File.ReadAllBytes(pathA);
        var b = File.ReadAllBytes(pathB);
        var diffs = new List<string>();
        if (a.AsSpan().SequenceEqual(b)) return new Result(true, true, 0, diffs);
        var na = Parse(a, 0, a.Length);
        var nb = Parse(b, 0, b.Length);
        var reordered = 0;
        CompareLists(na, nb, "", diffs, ref reordered);
        return new Result(false, diffs.Count == 0, reordered, diffs);
    }

    private static List<Node> Parse(byte[] data, int start, int end)
    {
        var list = new List<Node>();
        var off = start;
        while (off < end)
        {
            var type = System.Text.Encoding.Latin1.GetString(data, off, 4);
            var size = (int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(off + 4));
            var header = data.AsSpan(off, 24).ToArray();
            if (type == "GRUP")
            {
                list.Add(new Node { Type = type, Header = header, Children = Parse(data, off + 24, off + size) });
                off += size;
            }
            else
            {
                list.Add(new Node { Type = type, Header = header, Body = data.AsSpan(off + 24, size).ToArray() });
                off += 24 + size;
            }
        }
        return list;
    }

    private static string Key(Node n) => n.Type == "GRUP" ? n.Label : $"{n.Type}:{n.FormId:X8}";

    private static void CompareLists(List<Node> a, List<Node> b, string path, List<string> diffs, ref int reordered)
    {
        if (a.Count != b.Count) diffs.Add($"{path}: {a.Count} vs {b.Count} entries");
        var orderSame = a.Select(Key).SequenceEqual(b.Select(Key));
        var mapB = new Dictionary<string, Node>();
        foreach (var n in b) mapB.TryAdd(Key(n), n);
        if (!orderSame)
        {
            // Groups (top level) must keep their order; records inside a group may be reordered.
            if (a.Any(n => n.Type == "GRUP" && path.Length == 0)) diffs.Add($"{path}: top-level group order differs");
            else reordered++;
        }
        foreach (var n in a)
        {
            var here = $"{path}/{n.Label}";
            if (!mapB.TryGetValue(Key(n), out var m)) { diffs.Add($"{here}: missing in second file"); continue; }
            if (n.Type == "GRUP")
            {
                if (!n.Header.AsSpan(0, 16).SequenceEqual(m.Header.AsSpan(0, 16))) diffs.Add($"{here}: group header differs");
                CompareLists(n.Children, m.Children, here, diffs, ref reordered);
            }
            else if (!n.Header.AsSpan().SequenceEqual(m.Header) || !n.Body.AsSpan().SequenceEqual(m.Body))
                diffs.Add($"{here}: record bytes differ");
        }
    }
}
