#ifndef RECREATION_SCRIPT_HOST_MANAGED_HOST_H_
#define RECREATION_SCRIPT_HOST_MANAGED_HOST_H_

#include <functional>
#include <string>

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

  // Boots the managed world over guest. loader pulls a script (and its ancestor
  // chain) from the VFS by name for the bridge's load_script; pass {} to leave
  // it loading only already-present types. The remaining paths locate the .NET
  // runtime and the Recreation.Scripting assembly (see ClrHost::Initialize).
  // Returns false (and leaves available() false) when the runtime or assembly
  // is unavailable.
  bool Boot(PapyrusGuest& guest, std::function<bool(const std::string&)> loader,
            const std::string& dotnet_root, const std::string& runtime_config,
            const std::string& assembly);

  bool available() const { return available_; }

  // Advances the managed world one frame. No-op when unavailable.
  void Tick(float dt);
  // Delivers an engine event to the managed event bus. No-op when unavailable.
  void PublishEvent(const ManagedEvent& event);
  // Tears the managed world down. Safe to call more than once.
  void Shutdown();

 private:
  ClrHost clr_;
  BridgeContext ctx_{};
  ScriptBridge bridge_{};
  HostHandshake handshake_{};
  bool available_ = false;
};

}  // namespace rec::script::host

#endif  // RECREATION_SCRIPT_HOST_MANAGED_HOST_H_
