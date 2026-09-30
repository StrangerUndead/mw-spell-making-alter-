using Mutagen.Bethesda.Plugins;
using Noggog;
using Mutagen.Bethesda.Skyrim;

namespace LostArt.Generator.Build;

public static class Links
{
    public static ExtendedList<IFormLinkGetter<IKeywordGetter>> Keywords(IEnumerable<FormKey> keys)
    {
        var list = new ExtendedList<IFormLinkGetter<IKeywordGetter>>();
        foreach (var k in keys) list.Add(new FormLink<IKeywordGetter>(k));
        return list;
    }
}
