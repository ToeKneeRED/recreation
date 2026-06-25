using Recreation;
using Recreation.Games.Skyrim;
using Recreation.Interop;
using Recreation.Modding;

namespace Recreation.Tests;

// Exercises the Skyrim attribute-regeneration system against a fake engine,
// driving it through the real ModHost lifecycle, proving the soft logic without
// a running game.
public static class SkyrimRegenTests
{
    public static void Run(Check check)
    {
        var fake = new FakeBackend();
        Native.Backend = fake;
        ModHost.Shutdown();

        // Player at half on each pool, with round numbers for the rates.
        fake.SetValue(fake.Player, ActorValue.Health, current: 50, baseValue: 100);
        fake.SetValue(fake.Player, ActorValue.Magicka, current: 50, baseValue: 100);
        fake.SetValue(fake.Player, ActorValue.Stamina, current: 50, baseValue: 100);

        var regen = new AttributeRegeneration
        {
            HealthRatePerSecond = 0.10f,   // 10 hp/s at base 100
            MagickaRatePerSecond = 0.20f,  // 20 mp/s
            StaminaRatePerSecond = 0.05f,  // 5 sp/s
        };
        ModHost.Register(regen);

        ModHost.Tick(1.0f);
        check.Equal("health regenerates", 60f, fake.GetCurrent(fake.Player, ActorValue.Health));
        check.Equal("magicka regenerates", 70f, fake.GetCurrent(fake.Player, ActorValue.Magicka));
        check.Equal("stamina regenerates", 55f, fake.GetCurrent(fake.Player, ActorValue.Stamina));

        // Regeneration clamps at base and never overshoots.
        for (int i = 0; i < 100; i++) ModHost.Tick(1.0f);
        check.Equal("health caps at base", 100f, fake.GetCurrent(fake.Player, ActorValue.Health));
        check.Equal("magicka caps at base", 100f, fake.GetCurrent(fake.Player, ActorValue.Magicka));

        // In combat, health pauses but magicka and stamina keep recovering.
        fake.SetValue(fake.Player, ActorValue.Health, 50, 100);
        fake.SetValue(fake.Player, ActorValue.Magicka, 50, 100);
        fake.SetInCombat(fake.Player, true);
        ModHost.Tick(1.0f);
        check.Equal("health paused in combat", 50f, fake.GetCurrent(fake.Player, ActorValue.Health));
        check.Equal("magicka regenerates in combat", 70f,
                    fake.GetCurrent(fake.Player, ActorValue.Magicka));

        // Disabling the combat pause lets health recover while fighting.
        regen.PauseHealthInCombat = false;
        ModHost.Tick(1.0f);
        check.Equal("health recovers when pause is off", 60f,
                    fake.GetCurrent(fake.Player, ActorValue.Health));

        ModHost.Shutdown();
        Native.Backend = null;

        FastTravelLock(check);
    }

    private static void FastTravelLock(Check check)
    {
        var fake = new FakeBackend();
        Native.Backend = fake;
        ModHost.Shutdown();

        var lockSystem = new CombatFastTravelLock();
        ModHost.Register(lockSystem);

        // Out of combat: the first frame enables fast travel.
        fake.SetInCombat(fake.Player, false);
        ModHost.Tick(0.1f);
        check.That("fast travel enabled out of combat", fake.FastTravelEnabled);

        // Entering combat locks it.
        fake.SetInCombat(fake.Player, true);
        ModHost.Tick(0.1f);
        check.That("fast travel locked in combat", !fake.FastTravelEnabled);

        // Leaving combat unlocks it again.
        fake.SetInCombat(fake.Player, false);
        ModHost.Tick(0.1f);
        check.That("fast travel restored after combat", fake.FastTravelEnabled);

        ModHost.Shutdown();
        Native.Backend = null;
    }
}
