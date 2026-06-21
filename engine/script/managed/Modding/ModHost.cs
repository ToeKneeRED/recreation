using System;

namespace Recreation.Modding;

// The runtime that hosts user mods. Boot() runs once when the engine starts the
// managed world; it brings up the mod registry and lifecycle. The full
// discovery and behaviour pipeline is built out in the modding layer; this is
// the entry the engine calls.
public static class ModHost
{
    public static void Boot()
    {
        Console.WriteLine("[managed] mod host booting");
        // Mod discovery, registration and lifecycle are wired here as the
        // modding layer lands.
    }
}
