#ifndef RECREATION_NET_SESSION_H_
#define RECREATION_NET_SESSION_H_

#include <base/containers/unordered_map.h>
#include <base/containers/vector.h>
#include <base/strings/xstring.h>

#include <znet/z_client.h>
#include <znet/z_server.h>

#include "ecs/world.h"
#include "net/protocol.h"
#include "net/replication.h"

namespace rec::net {

struct SessionConfig {
  u16 port = 29700;
  base::String address;  // client: server to join
  base::NameString player_name{"player"};
  u32 max_clients = 64;
  u32 tick_rate = 60;
  u32 snapshot_interval_ticks = 3;   // 20 Hz at the 60 Hz fixed step
  u32 keyframe_interval_ticks = 60;  // full snapshot every second
  f32 client_timeout_seconds = 10.0f;
  u64 player_mesh = 0;  // AssetId hash spawned for joining players
};

// The server simulates, clients render what snapshots tell them. One process
// can run both (listen server) since sessions only touch the world through
// the ECS.
class Session {
 public:
  virtual ~Session() = default;

  // Called from the fixed-step sim stage.
  virtual void Tick(ecs::World& world, f32 dt) = 0;
};

class ServerSession final : public Session {
 public:
  explicit ServerSession(SessionConfig config);
  ~ServerSession() override;

  bool Start();
  void Tick(ecs::World& world, f32 dt) override;

  u32 client_count() const { return static_cast<u32>(clients_.size()); }
  u64 tick() const { return tick_; }

 private:
  struct RemoteClient {
    base::NameString name;
    ecs::Entity player = ecs::kInvalidEntity;
    u64 player_net_id = 0;
    PlayerInput input;
    f32 since_last_packet = 0;
  };

  void PollMessages(ecs::World& world);
  void HandleJoin(ecs::World& world, u32 peer, const ClientJoinView& join);
  void DropClient(ecs::World& world, u32 peer);
  void SimulatePlayers(ecs::World& world, f32 dt);
  void TimeoutClients(ecs::World& world, f32 dt);
  void BroadcastSnapshot(ecs::World& world);

  SessionConfig config_;
  tx::network::ZServer server_;
  SnapshotBuilder builder_;
  Snapshot snapshot_;  // reused so the vectors keep their capacity
  base::UnorderedMap<u32, RemoteClient> clients_;
  base::Vector<u32> scratch_dropped_;
  u64 tick_ = 0;
  bool force_keyframe_ = false;
  bool started_ = false;
};

class ClientSession final : public Session {
 public:
  explicit ClientSession(SessionConfig config);
  ~ClientSession() override;

  bool Start();
  void Tick(ecs::World& world, f32 dt) override;

  // Local input forwarded to the server every tick once joined.
  void SetInput(const PlayerInput& input) { input_ = input; }

  bool joined() const { return joined_; }
  u64 player_net_id() const { return player_net_id_; }
  ecs::Entity player_entity() const { return applier_.Find(player_net_id_); }
  u32 replicated_entity_count() const { return applier_.entity_count(); }

 private:
  void PollMessages(ecs::World& world);

  SessionConfig config_;
  tx::network::ZClient client_;
  SnapshotApplier applier_;
  PlayerInput input_;
  u64 player_net_id_ = 0;
  u64 tick_ = 0;
  f32 snapshot_dt_ = 1.0f / 20.0f;
  bool join_sent_ = false;
  bool joined_ = false;
  bool failure_logged_ = false;
};

}  // namespace rec::net

#endif  // RECREATION_NET_SESSION_H_
