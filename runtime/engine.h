#ifndef RECREATION_RUNTIME_ENGINE_H_
#define RECREATION_RUNTIME_ENGINE_H_

#include <atomic>
#include <memory>
#include <string>

#include <base/containers/unordered_map.h>

#include "asset/asset_database.h"
#include "asset/vfs.h"
#include "bethesda/game_profile.h"
#include "bethesda/load_order.h"
#include "core/frame_timer.h"
#include "core/job_system.h"
#include "core/window.h"
#include "ecs/scheduler.h"
#include "ecs/world.h"
#include "net/session.h"
#include "render/renderer.h"
#include "world/cell_streaming.h"

namespace rec {

struct EngineConfig {
  std::string data_dir;
  std::string plugins_txt;
  bethesda::Game game = bethesda::Game::kUnknown;  // kUnknown = autodetect
  render::RendererDesc renderer;
  bool headless = false;
  bool host_server = false;
  u16 port = 29700;
  std::string connect_address;
  std::string player_name = "player";
  u32 max_clients = 64;
};

class Engine {
 public:
  bool Initialize(const EngineConfig& config);
  int Run();
  void Shutdown();

  // Safe to call from a signal handler; Run() returns after the current
  // frame.
  void RequestQuit() { quit_.store(true, std::memory_order_relaxed); }

 private:
  bool LoadGameData();
  void MountArchives();
  bool StartNetworking();
  void CreateDemoScene();

  EngineConfig config_;
  bethesda::Game game_ = bethesda::Game::kUnknown;

  std::unique_ptr<Window> window_;
  std::unique_ptr<JobSystem> jobs_;
  FrameTimer timer_;

  ecs::World world_;
  ecs::Scheduler scheduler_;

  asset::Vfs vfs_;
  std::unique_ptr<asset::AssetDatabase> assets_;
  bethesda::RecordStore records_;
  std::unique_ptr<world::CellStreamer> streamer_;

  render::Renderer renderer_;
  // Last frame's world matrices keyed by entity, for motion vectors.
  base::UnorderedMap<u64, Mat4> prev_transforms_;
  std::unique_ptr<net::Session> session_;
  // Typed views into session_, null unless that role is active.
  net::ServerSession* server_session_ = nullptr;
  net::ClientSession* client_session_ = nullptr;
  f32 demo_input_time_ = 0;

  std::atomic<bool> quit_ = false;
};

}  // namespace rec

#endif  // RECREATION_RUNTIME_ENGINE_H_
