#include "script/host/managed_host.h"

#include <utility>

#include "core/log.h"

namespace rec::script::host {

ManagedHost::~ManagedHost() { Shutdown(); }

void ManagedHost::AddDomain(std::string name, PapyrusGuest& guest,
                            std::function<bool(const std::string&)> loader) {
  auto domain = std::make_unique<Domain>();
  domain->name = std::move(name);
  domain->ctx.guest = &guest;
  domain->ctx.loader = std::move(loader);
  domain->bridge = MakeScriptBridge(domain->ctx);  // ctx address is stable (heap)
  domains_.push_back(std::move(domain));
}

bool ManagedHost::Boot(const std::string& dotnet_root, const std::string& runtime_config,
                       const std::string& assembly) {
  if (domains_.empty()) {
    REC_WARN("managed: no content domain registered, scripting disabled");
    return false;
  }
  domain_table_.clear();
  domain_table_.reserve(domains_.size());
  for (const auto& domain : domains_)
    domain_table_.push_back({domain->name.c_str(), &domain->bridge});
  handshake_.bridge = &domains_[0]->bridge;  // primary, back-compatible
  handshake_.callbacks = {};
  handshake_.domain_count = static_cast<std::int32_t>(domain_table_.size());
  handshake_.domains = domain_table_.data();
  handshake_.ui_widget_ops = ui_widget_ops_;  // null when there is no UI backend
  handshake_.rpc = rpc_bridge_;  // emit/on null when networking is off
  handshake_.realm = realm_;     // server / client / standalone

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

std::int32_t ManagedHost::DispatchUi(const char* func_name, std::uint64_t widget) {
  if (available_ && handshake_.callbacks.dispatch_ui)
    return handshake_.callbacks.dispatch_ui(func_name, widget);
  return 0;
}

void ManagedHost::DispatchRpc(const char* name, std::int32_t sender,
                              std::int32_t from_server, const ApiValue* args,
                              std::int32_t argc) {
  if (available_ && handshake_.callbacks.dispatch_rpc)
    handshake_.callbacks.dispatch_rpc(name, sender, from_server, args, argc);
}

void ManagedHost::PublishEvent(const ManagedEvent& event) {
  if (available_ && handshake_.callbacks.publish_event) handshake_.callbacks.publish_event(&event);
}

void ManagedHost::QueueEvent(const ManagedEvent& event) {
  std::lock_guard<std::mutex> lock(event_mutex_);
  pending_events_.push_back(event);
}

void ManagedHost::DrainEvents() {
  if (!available_) return;
  std::vector<ManagedEvent> events;
  {
    std::lock_guard<std::mutex> lock(event_mutex_);
    events.swap(pending_events_);
  }
  for (const ManagedEvent& e : events) PublishEvent(e);
}

void ManagedHost::Shutdown() {
  if (available_ && handshake_.callbacks.shutdown) handshake_.callbacks.shutdown();
  available_ = false;
  handshake_.callbacks = {};
  clr_.Shutdown();
}

}  // namespace rec::script::host
