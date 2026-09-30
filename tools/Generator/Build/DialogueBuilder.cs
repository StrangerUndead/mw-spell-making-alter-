using System.Text.Json.Nodes;
using LostArt.Generator.Core;
using LostArt.Generator.Data;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace LostArt.Generator.Build;

/// <summary>
/// LA_ServicesQuest (start game enabled) with the player topic "I'd like to make a spell."
/// and two INFOs shared by all spellmakers. No VMAD scripts: the DLL watches
/// TESTopicInfoEvent for LA_Info_MakeSpell_Serve and opens the menu, and it sets the global
/// LA_ServiceRefusal (CONTRACTS §8 refusal reasons) for the current speaker before the topic
/// list is evaluated.
/// </summary>
public sealed class DialogueBuilder(BuildContext ctx)
{
    public const string Quest = "LA_ServicesQuest";
    public const string Branch = "LA_Branch_MakeSpell";
    public const string Topic = "LA_Topic_MakeSpell";
    public const string InfoServe = "LA_Info_MakeSpell_Serve";
    public const string InfoRefuse = "LA_Info_MakeSpell_Refuse";

    private Log Log => ctx.Log;

    public sealed record Spellmaker(string Id, FormKey Npc, int RefusalCode, string? RefusalLine);

    public List<Spellmaker> Spellmakers { get; } = new();

    public void Build()
    {
        LoadSpellmakers();

        var quest = ctx.C(Quest, fk => new Quest(fk, BuildContext.Release)
        {
            Name = ctx.Tr.Resolve("$LA_Quest_Services", "Lost Art: Spellmaking Services"),
            Flags = Mutagen.Bethesda.Skyrim.Quest.Flag.StartGameEnabled | Mutagen.Bethesda.Skyrim.Quest.Flag.RunOnce,
            Priority = 50,
            Type = Mutagen.Bethesda.Skyrim.Quest.TypeEnum.Misc,
            Filter = "LostArt\\",
        });

        var branchKey = ctx.ContentLink(Branch);
        var topic = ctx.C(Topic, fk => new DialogTopic(fk, BuildContext.Release)
        {
            Name = ctx.Tr.Resolve("$LA_Topic_MakeSpell", "I'd like to make a spell."),
            Priority = 50f,
            Branch = new FormLinkNullable<IDialogBranchGetter>(branchKey),
            Quest = new FormLinkNullable<IQuestGetter>(quest.FormKey),
            Category = DialogTopic.CategoryEnum.Topic,
            Subtype = DialogTopic.SubtypeEnum.Custom,
            SubtypeName = new Mutagen.Bethesda.Plugins.RecordType("CUST"),
        });

        ctx.C(Branch, fk => new DialogBranch(fk, BuildContext.Release)
        {
            Quest = new FormLink<IQuestGetter>(quest.FormKey),
            Category = DialogBranch.CategoryType.Player,
            Flags = DialogBranch.Flag.TopLevel,
            StartingTopic = new FormLinkNullable<IDialogTopicGetter>(topic.FormKey),
        });

        var refusal = ctx.ContentLink(ItemsBuilder.GlobalServiceRefusal);
        var enabled = ctx.ContentLink(ItemsBuilder.GlobalSpellmakersEnabled);

        // Serve: (GetIsID A OR GetIsID B OR ...) AND LA_SpellmakersEnabled == 1 AND LA_ServiceRefusal == 0
        var serve = ctx.C(InfoServe, fk => new DialogResponses(fk, BuildContext.Release)
        {
            Flags = new DialogResponseFlags { Flags = DialogResponses.Flag.Goodbye },
            Topic = new FormLinkNullable<IDialogTopicGetter>(topic.FormKey),
            Responses = new ExtendedList<DialogResponse>
            {
                Line(ctx.Tr.Resolve("$LA_Dlg_Serve", "Very well. Tell me what you have in mind."), 1),
            },
            Conditions = SpeakerConditions()
                .Append(GlobalCond(enabled, CompareOperator.EqualTo, 1f))
                .Append(GlobalCond(refusal, CompareOperator.EqualTo, 0f))
                .ToExtendedList(),
        });

        // Refuse, per spellmaker: their own line (tomes.json refusalLine) for their own gate
        // (reasons 1-3: College membership, wanted in the hold, quest not done).
        var infos = new List<DialogResponses> { serve };
        foreach (var sm in Spellmakers)
        {
            var line = ctx.Tr.Resolve(sm.RefusalLine, "I'm afraid I can't help you with that.");
            infos.Add(ctx.C($"{InfoRefuse}_{Naming.SnakeToPascal(sm.Id)}", fk => new DialogResponses(fk, BuildContext.Release)
            {
                Flags = new DialogResponseFlags { Flags = 0 },
                Topic = new FormLinkNullable<IDialogTopicGetter>(topic.FormKey),
                Responses = new ExtendedList<DialogResponse> { Line(line, 1) },
                Conditions = new[] { IsId(sm.Npc, last: true) }
                    .Append(GlobalCond(enabled, CompareOperator.EqualTo, 1f))
                    .Append(GlobalCond(refusal, CompareOperator.GreaterThan, 0f))
                    .Append(GlobalCond(refusal, CompareOperator.LessThan, 4f))
                    .ToExtendedList(),
            }));
        }

        // Refuse, shared: the universal reasons (4 stage-4 vampire, 5 relationship below Acquaintance).
        infos.Add(ctx.C(InfoRefuse, fk => new DialogResponses(fk, BuildContext.Release)
        {
            Flags = new DialogResponseFlags { Flags = 0 },
            Topic = new FormLinkNullable<IDialogTopicGetter>(topic.FormKey),
            Responses = new ExtendedList<DialogResponse>
            {
                Line(ctx.Tr.Resolve("$LA_Refuse_Generic", "I'm afraid I can't help you with that."), 1),
            },
            Conditions = SpeakerConditions()
                .Append(GlobalCond(enabled, CompareOperator.EqualTo, 1f))
                .Append(GlobalCond(refusal, CompareOperator.GreaterThanOrEqualTo, 4f))
                .ToExtendedList(),
        }));

        // INFO order follows the EditorID sort; the conditions are mutually exclusive, so order is irrelevant in-game.
        foreach (var info in infos.OrderBy(i => BuildContext.SortKey(i.EditorID!), StringComparer.Ordinal))
            topic.Responses.Add(info);

        ctx.Verify("DIAL/INFO LA_Topic_MakeSpell: responses are new silent lines (no voice files) - check subtitle timing in-game, or point ResponseData at each NPC's shared merchant/trainer lines in CK; " +
                   "open the quest once in CK to let it build the Dialogue View (DLVW is editor-only) and confirm the Top-Level branch shows the topic");
    }

    public const string McmQuest = "LA_MCMQuest";

    /// <summary>
    /// MCM Helper quest: start-game enabled, script LostArt_MCM (extends MCM_ConfigBase) and a
    /// PlayerAlias (forced to PlayerRef) running SKI_PlayerLoadGameAlias so MCM Helper re-registers
    /// the menu on every load. Scripts carry no properties.
    /// </summary>
    public void BuildMcmQuest()
    {
        var playerRef = new FormKey(VanillaIndex.Skyrim, 0x000014);
        ctx.C(McmQuest, fk => new Quest(fk, BuildContext.Release)
        {
            Name = ctx.Tr.Resolve("$LA_MCM_Title", "Lost Art of Spellmaking"),
            Flags = Mutagen.Bethesda.Skyrim.Quest.Flag.StartGameEnabled,
            Priority = 0,
            Type = Mutagen.Bethesda.Skyrim.Quest.TypeEnum.None,
            Filter = "LostArt\\",
            NextAliasID = 1,
            VirtualMachineAdapter = new QuestAdapter
            {
                Version = 5,
                ObjectFormat = 2,
                Scripts = new ExtendedList<ScriptEntry> { new ScriptEntry { Name = "LostArt_MCM", Flags = ScriptEntry.Flag.Local } },
                Aliases = new ExtendedList<QuestFragmentAlias>
                {
                    new QuestFragmentAlias
                    {
                        Property = new ScriptObjectProperty { Object = new FormLink<ISkyrimMajorRecordGetter>(fk), Alias = 0 },
                        Version = 5,
                        ObjectFormat = 2,
                        Scripts = new ExtendedList<ScriptEntry> { new ScriptEntry { Name = "SKI_PlayerLoadGameAlias", Flags = ScriptEntry.Flag.Local } },
                    },
                },
            },
            Aliases = new ExtendedList<QuestAlias>
            {
                new QuestAlias
                {
                    ID = 0,
                    Type = QuestAlias.TypeEnum.Reference,
                    Name = "PlayerAlias",
                    Flags = 0,
                    ForcedReference = new FormLinkNullable<IPlacedGetter>(playerRef),
                    VoiceTypes = new FormLinkNullable<IAliasVoiceTypeGetter>(),
                },
            },
        });
    }

    private static DialogResponse Line(string text, byte number) => new()
    {
        Emotion = Emotion.Neutral,
        EmotionValue = 50,
        ResponseNumber = number,
        Text = text,
        Flags = 0,
    };

    private IEnumerable<Condition> SpeakerConditions()
    {
        if (Spellmakers.Count == 0)
        {
            Log.Error("spellmakers.json: no spellmakers; the service topic would be offered by nobody");
            yield break;
        }
        for (var i = 0; i < Spellmakers.Count; i++)
            yield return IsId(Spellmakers[i].Npc, last: i == Spellmakers.Count - 1);
    }

    /// <summary>GetIsID speaker == npc; OR-flagged unless it closes the OR group.</summary>
    private static Condition IsId(FormKey npc, bool last)
    {
        var data = new GetIsIDConditionData { RunOnType = Condition.RunOnType.Subject };
        data.Object.Link.SetTo(npc);
        return new ConditionFloat
        {
            CompareOperator = CompareOperator.EqualTo,
            ComparisonValue = 1f,
            Flags = last ? 0 : Condition.Flag.OR,
            Data = data,
        };
    }

    private static Condition GlobalCond(FormKey global, CompareOperator op, float value)
    {
        var data = new GetGlobalValueConditionData { RunOnType = Condition.RunOnType.Subject };
        data.Global.Link.SetTo(global);
        return new ConditionFloat { CompareOperator = op, ComparisonValue = value, Flags = 0, Data = data };
    }

    private void LoadSpellmakers()
    {
        foreach (var o in ctx.Data.ContentList("spellmakers", "spellmakers", "npcs", "entries"))
        {
            var id = o.Str("id", "name", "editorId") ?? "?";
            var npcRef = o.Str("npc", "actor", "base", "formKey", "form", "baseId", "actorBase");
            var fk = ctx.Resolve(npcRef ?? o.Str("editorId"), $"spellmakers:{id}", "Npc");
            if (fk is null) continue;
            var refusal = o.Str("refusal") ?? "";
            var code = refusal.StartsWith("college") ? 1 : refusal.StartsWith("wanted") ? 2 : refusal.StartsWith("quest") ? 3 : 0;
            if (code == 0) Log.Warn($"spellmakers:{id}: unknown refusal '{refusal}'");
            Spellmakers.Add(new Spellmaker(id, fk.Value, code, o.Str("refusalLine")));
        }
        // Stable order: by FormKey (plugin order, then id).
        Spellmakers.Sort((a, b) =>
        {
            var pa = Array.IndexOf(VanillaIndex.MasterOrder, a.Npc.ModKey);
            var pb = Array.IndexOf(VanillaIndex.MasterOrder, b.Npc.ModKey);
            return pa != pb ? pa.CompareTo(pb) : a.Npc.ID.CompareTo(b.Npc.ID);
        });
        if (Spellmakers.Count != 15) Log.Warn($"spellmakers.json lists {Spellmakers.Count} spellmakers (the outline has 15)");
    }
}
