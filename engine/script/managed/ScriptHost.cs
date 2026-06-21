using System;
using System.Runtime.InteropServices;
using Recreation.Interop;
using Recreation.Modding;

namespace Recreation;

// The native entry into the managed world. The engine's ManagedHost resolves one
// of these [UnmanagedCallersOnly] methods and calls it across the boundary. Both
// first bind the bridge so the rest of the SDK (Game, Form, mods, ...) can reach
// the engine.
public static unsafe class ScriptHost
{
    // Production boot. Binds the inbound bridge, starts the mod host, then hands
    // the engine the outbound callbacks it drives the managed world through.
    // Returns 0 on success.
    [UnmanagedCallersOnly]
    public static int Main(void* handshakePtr)
    {
        var handshake = (HostHandshake*)handshakePtr;
        Native.Bind(handshake->Bridge);
        RegisterDomains(handshake);
        Console.WriteLine($"[managed] Recreation scripting host online, {Domains.Count} game(s)");
        if (Environment.GetEnvironmentVariable("REC_DOMAINS_REPORT") != null) ReportDomains();
        ModHost.Boot();

        // Load drop-in user mods from RECREATION_MODS_DIR, if set.
        string? modsDir = Environment.GetEnvironmentVariable("RECREATION_MODS_DIR");
        if (!string.IsNullOrEmpty(modsDir)) ModLoader.LoadDirectory(modsDir);

        handshake->Callbacks.Tick = &OnTick;
        handshake->Callbacks.PublishEvent = &OnPublishEvent;
        handshake->Callbacks.Shutdown = &OnShutdown;
        return 0;
    }

    // Builds the Domains registry from the handshake's per-game bridge table, so
    // mods can reach every loaded game. The primary (rendered) game is entry 0
    // and reuses the bound Native backend; the rest each get their own backend.
    private static void RegisterDomains(HostHandshake* handshake)
    {
        Domains.Clear();
        for (int i = 0; i < handshake->DomainCount; i++)
        {
            DomainBridge db = handshake->Domains[i];
            string name = Marshal.PtrToStringUTF8((IntPtr)db.Name) ?? $"domain{i}";
            IEngineBackend backend =
                i == 0 ? Native.Backend! : new NativeBackend(db.Bridge);
            Domains.Register(new GameWorld(name, backend), isPrimary: i == 0);
        }
        // The bare SelfTest path passes no domain table; fall back to one world
        // over the bound primary backend so Domains is never empty when bound.
        if (Domains.Count == 0 && Native.Backend != null)
            Domains.Register(new GameWorld("primary", Native.Backend), isPrimary: true);
    }

    // REC_DOMAINS_REPORT: list every game a mod can reach this session.
    private static void ReportDomains()
    {
        Console.WriteLine($"[managed] {Domains.Count} game domain(s) live:");
        foreach (GameWorld world in Domains.All)
            Console.WriteLine($"[managed]   - {world.Name}{(world == Domains.Primary ? " (primary)" : "")}");
    }

    // Self-test: binds the bridge and exercises the SDK against the engine,
    // returning the number of failed checks (0 = all passed). The native
    // hosttest harness asserts on this. Separate from Main so the production
    // boot never assumes a test fixture.
    [UnmanagedCallersOnly]
    public static int SelfTest(void* bridge)
    {
        Native.Bind((ScriptBridge*)bridge);
        return SdkSelfTest.Run();
    }

    [UnmanagedCallersOnly]
    private static void OnTick(float deltaTime) => ModHost.Tick(deltaTime);

    [UnmanagedCallersOnly]
    private static void OnPublishEvent(ManagedEvent* e) => EngineEvents.Dispatch(*e);

    [UnmanagedCallersOnly]
    private static void OnShutdown() => ModHost.Shutdown();
}
