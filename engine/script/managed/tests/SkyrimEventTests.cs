using Recreation.Games.Skyrim;
using Recreation.Interop;
using Recreation.Modding;

namespace Recreation.Tests;

// Verifies the event-driven Skyrim systems respond to engine events delivered
// through the bus, the gmod-style hook path.
public static class SkyrimEventTests
{
    public static void Run(Check check)
    {
        QuestTracker(check);
        Combat(check);
    }

    private static void QuestTracker(Check check)
    {
        ModHost.Shutdown();
        EventBus.Clear();

        var tracker = new QuestProgressTracker();
        ModHost.Register(tracker);
        check.Equal("no stage before any event", -1, tracker.LastStage);

        // The engine delivers events as ManagedEvent payloads; route one through
        // the same translator the host uses.
        EngineEvents.Dispatch(new Interop.ManagedEvent
        {
            Id = Interop.ManagedEventId.QuestStageChanged,
            A = 0xABCD,
            I = 40,
        });
        check.Equal("tracker recorded the stage", 40, tracker.LastStage);
        check.Equal("tracker recorded the quest", 0xABCDUL, tracker.LastQuestHandle);

        // A later stage updates it.
        EngineEvents.Dispatch(new Interop.ManagedEvent
        {
            Id = Interop.ManagedEventId.QuestStageChanged,
            A = 0xABCD,
            I = 50,
        });
        check.Equal("tracker follows progression", 50, tracker.LastStage);

        // Once destroyed it unsubscribes and stops tracking.
        ModHost.Unregister(tracker);
        EngineEvents.Dispatch(new Interop.ManagedEvent
        {
            Id = Interop.ManagedEventId.QuestStageChanged,
            A = 0xABCD,
            I = 60,
        });
        check.Equal("unsubscribed after destroy", 50, tracker.LastStage);

        ModHost.Shutdown();
        EventBus.Clear();
    }

    private static void Combat(Check check)
    {
        var fake = new FakeBackend { Player = 0x14 };
        Native.Backend = fake;
        ModHost.Shutdown();
        EventBus.Clear();

        var tracker = new CombatTracker();
        ModHost.Register(tracker);

        EventBus.Publish(new ActorDied(0x99));
        EventBus.Publish(new ActorDied(0xAB));
        check.Equal("kills counted", 2, tracker.Kills);
        check.Equal("last victim recorded", 0xABUL, tracker.LastVictim.Handle);

        // The player's own death is not a kill.
        EventBus.Publish(new ActorDied(fake.Player));
        check.Equal("player death is not a kill", 2, tracker.Kills);

        ModHost.Shutdown();
        EventBus.Clear();
        Native.Backend = null;
    }
}
