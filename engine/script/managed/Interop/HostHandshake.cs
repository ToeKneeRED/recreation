using System.Runtime.InteropServices;

namespace Recreation.Interop;

// The id of an engine event crossing into managed code. Mirrors host/bridge.h
// ManagedEventId; append only, never reorder.
public enum ManagedEventId : int
{
    None = 0,
    ActorDied = 1,           // A = actor handle
    ItemAdded = 2,           // A = container handle, B = item handle, I = count
    QuestStageChanged = 3,   // A = quest form id, I = stage
    FormLoaded = 4,          // A = form handle (its scripts attached / it went live)
    PlayerActivated = 5,     // A = activated target handle
    ItemRemoved = 6,         // A = container handle, B = item handle, I = count removed
    LocationChanged = 7,     // A = interior cell id (0 outside), I = 1 if interior
    KeyPressed = 8,          // A = key code (the engine's Key enum)
    FormUnloaded = 9,        // A = form handle (it streamed out / left the world)
}

// An engine event payload. Byte-for-byte mirror of host/bridge.h ManagedEvent;
// only the fields an id documents are meaningful.
[StructLayout(LayoutKind.Sequential)]
public struct ManagedEvent
{
    public ManagedEventId Id;
    public ulong A;
    public ulong B;
    public int I;
    public float F;
}

// The outbound callbacks the managed entrypoint fills so the engine can drive
// the managed world. Mirror of host/bridge.h HostCallbacks.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct HostCallbacks
{
    public delegate* unmanaged<float, void> Tick;
    public delegate* unmanaged<ManagedEvent*, void> PublishEvent;
    public delegate* unmanaged<void> Shutdown;
    // Routes a ultragui handler (button click, on_change) into the managed UI
    // layer. (funcName UTF-8, packed widget) -> 1 if a handler claimed it.
    // Append-only: keep after the originals.
    public delegate* unmanaged<byte*, ulong, int> DispatchUi;
}

// One loaded game's content domain. Mirror of host/bridge.h DomainBridge.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct DomainBridge
{
    public byte* Name;        // UTF-8 display name
    public ScriptBridge* Bridge;
}

// What the managed entrypoint receives: the inbound bridge plus the outbound
// callbacks slot to fill. Mirror of host/bridge.h HostHandshake. `Bridge` is the
// primary domain (== Domains[0].Bridge); `Domains` lists every loaded game so a
// mod can consume Skyrim and Fallout content at the same time. Append only.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct HostHandshake
{
    public ScriptBridge* Bridge;
    public HostCallbacks Callbacks;
    public int DomainCount;
    public DomainBridge* Domains;
    // The ultragui widget-operation table (WidgetOps*), so UI handlers can read
    // and mutate live widgets. Null when there is no UI backend. Append-only.
    public WidgetOps* UiWidgetOps;
}
