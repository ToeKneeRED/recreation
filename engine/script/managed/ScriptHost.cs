using System;
using System.Runtime.InteropServices;
using Recreation.Interop;

namespace Recreation;

// The native entry into the managed world. The engine's ClrHost resolves one of
// these [UnmanagedCallersOnly] methods and calls it with the ScriptBridge, the
// single channel between the two worlds. Both entrypoints first bind the bridge
// so the rest of the SDK (Game, Form, mods, ...) can reach the engine.
public static unsafe class ScriptHost
{
    // Production boot: binds the bridge and starts the modding host, which
    // discovers and initialises user mods. Returns 0 on success.
    [UnmanagedCallersOnly]
    public static int Main(void* bridge)
    {
        Native.Bind((ScriptBridge*)bridge);
        Console.WriteLine("[managed] Recreation scripting host online");
        Modding.ModHost.Boot();
        return 0;
    }

    // Self-test: binds the bridge and exercises the SDK against the engine,
    // returning the number of failed checks (0 = all passed). The native
    // hosttest harness asserts on this. Kept separate from Main so the
    // production boot never assumes a test fixture.
    [UnmanagedCallersOnly]
    public static int SelfTest(void* bridge)
    {
        Native.Bind((ScriptBridge*)bridge);
        return SdkSelfTest.Run();
    }
}
