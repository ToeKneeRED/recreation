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
