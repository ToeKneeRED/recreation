#ifndef RECREATION_SCRIPT_HOST_MANAGED_HOST_H_
#define RECREATION_SCRIPT_HOST_MANAGED_HOST_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "script/host/bridge.h"
#include "script/host/clr_host.h"
#include "script/host/guest_bridge.h"

namespace rec::script {
class PapyrusGuest;
}

namespace rec::script::host {

// The engine-facing handle to the managed scripting world. It wires the two
// directions of the boundary: it builds the inbound bridge over a Papyrus guest,
// boots the .NET host, and the managed entrypoint hands back the outbound
// callbacks this then drives every frame.
//
// Like the renderer's optional backends, it degrades gracefully: if .NET or the
// assembly is missing, Boot() returns false and available() stays false, so the
// engine runs without managed scripting instead of failing. The guest must
// outlive the host.
class ManagedHost {
 public:
  ManagedHost() = default;
  ~ManagedHost();

  ManagedHost(const ManagedHost&) = delete;
  ManagedHost& operator=(const ManagedHost&) = delete;

  // Registers a content domain the managed world can reach, before Boot. Call
  // once per loaded game, primary (rendered) game first: it becomes the default
  // bridge and domains[0]. `name` is the game's display name; loader pulls a
  // script (and its ancestor chain) from that domain's VFS by name for the
  // bridge's load_script (pass {} to load only already-present types). The guest
  // must outlive the host.
  void AddDomain(std::string name, PapyrusGuest& guest,
                 std::function<bool(const std::string&)> loader);

  // Boots the managed world over the registered domains. The paths locate the
  // .NET runtime and the Recreation.Scripting assembly (see ClrHost::Initialize).
  // Returns false (and leaves available() false) when no domain was registered,
  // or the runtime or assembly is unavailable.
  bool Boot(const std::string& dotnet_root, const std::string& runtime_config,
            const std::string& assembly);

  bool available() const { return available_; }

  // Advances the managed world one frame. No-op when unavailable.
  void Tick(float dt);
  // Delivers an engine event to the managed event bus. Must run on the host
  // (main) thread; no-op when unavailable.
  void PublishEvent(const ManagedEvent& event);

  // Enqueues an event from any thread (the guest thread raises most of them).
  // The main loop later drains them with DrainEvents, which is where they reach
  // managed code, so engine threads never call into the CLR directly.
  void QueueEvent(const ManagedEvent& event);
  // Publishes every queued event. Host (main) thread only.
  void DrainEvents();

  // Tears the managed world down. Safe to call more than once.
  void Shutdown();

 private:
  // One registered domain. Heap allocated so its BridgeContext address (which the
  // ScriptBridge::ctx points at) stays stable as more domains are added.
  struct Domain {
    std::string name;
    BridgeContext ctx;
    ScriptBridge bridge{};
  };

  ClrHost clr_;
  std::vector<std::unique_ptr<Domain>> domains_;
  std::vector<DomainBridge> domain_table_;  // handshake view: borrows name/bridge
  HostHandshake handshake_{};
  bool available_ = false;

  std::mutex event_mutex_;
  std::vector<ManagedEvent> pending_events_;
};

}  // namespace rec::script::host

#endif  // RECREATION_SCRIPT_HOST_MANAGED_HOST_H_
