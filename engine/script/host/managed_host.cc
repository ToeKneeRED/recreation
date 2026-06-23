#include "script/host/managed_host.h"

#include <utility>

#include "core/log.h"

namespace rec::script::host {

ManagedHost::~ManagedHost() { Shutdown(); }

bool ManagedHost::Boot(PapyrusGuest& guest, std::function<bool(const std::string&)> loader,
                       const std::string& dotnet_root, const std::string& runtime_config,
                       const std::string& assembly) {
  ctx_.guest = &guest;
  ctx_.loader = std::move(loader);
  bridge_ = MakeScriptBridge(ctx_);
  handshake_.bridge = &bridge_;
  handshake_.callbacks = {};

  if (!clr_.Initialize(dotnet_root, runtime_config, assembly,
                       "Recreation.ScriptHost, Recreation.Scripting", "Main")) {
    REC_INFO("managed: .NET host unavailable, scripting disabled");
    return false;
  }

  // The managed entrypoint binds the bridge, boots its mods, and fills in the
  // outbound callbacks we drive from here on.
  if (clr_.Invoke(&handshake_) != 0) {
    REC_WARN("managed: entrypoint returned nonzero, scripting disabled");
    return false;
  }
  available_ = true;
  REC_INFO("managed: scripting world online");
  return true;
}

void ManagedHost::Tick(float dt) {
  if (available_ && handshake_.callbacks.tick) handshake_.callbacks.tick(dt);
}

void ManagedHost::PublishEvent(const ManagedEvent& event) {
  if (available_ && handshake_.callbacks.publish_event) handshake_.callbacks.publish_event(&event);
}

void ManagedHost::Shutdown() {
  if (available_ && handshake_.callbacks.shutdown) handshake_.callbacks.shutdown();
  available_ = false;
  handshake_.callbacks = {};
  clr_.Shutdown();
}

}  // namespace rec::script::host
