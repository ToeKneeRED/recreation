using System;
using Recreation.Modding;

namespace Recreation.Games.Skyrim;

// Reacts to quest progression, a worked example of an event-driven (gmod-style
// hook) system: it subscribes to QuestStageChanged on the bus and keeps the last
// quest and stage seen, so other mods and UI can read where the story is. The
// engine raises the event whenever a quest advances a stage; nothing here polls.
public sealed class QuestProgressTracker : GameBehaviour
{
    private EventBus.Subscription? _subscription;

    // The most recent quest stage transition observed, or (0, -1) before any.
    public ulong LastQuestHandle { get; private set; }
    public int LastStage { get; private set; } = -1;

    protected override void OnStart() =>
        _subscription = EventBus.Subscribe<QuestStageChanged>(OnStageChanged);

    protected override void OnDestroy() => _subscription?.Dispose();

    private void OnStageChanged(QuestStageChanged e)
    {
        LastQuestHandle = e.QuestHandle;
        LastStage = e.Stage;
        Console.WriteLine($"[skyrim] quest 0x{e.QuestHandle:X} reached stage {e.Stage}");
    }
}
