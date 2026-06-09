#ifndef RECREATION_RUNTIME_ENGINE_H_
#define RECREATION_RUNTIME_ENGINE_H_

#include <memory>
#include <string>

#include "recreation/asset/asset_database.h"
#include "recreation/asset/vfs.h"
#include "recreation/bethesda/game_profile.h"
#include "recreation/bethesda/load_order.h"
#include "recreation/core/frame_timer.h"
#include "recreation/core/job_system.h"
#include "recreation/core/window.h"
#include "recreation/ecs/scheduler.h"
#include "recreation/ecs/world.h"
#include "recreation/net/session.h"
#include "recreation/render/renderer.h"
#include "recreation/world/cell_streaming.h"

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
};

class Engine {
 public:
  bool Initialize(const EngineConfig& config);
  int Run();
  void Shutdown();

 private:
  bool LoadGameData();
  void MountArchives();

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
  net::ReplicationRegistry replication_;
  std::unique_ptr<net::Session> session_;

  bool quit_ = false;
};

}  // namespace rec

#endif  // RECREATION_RUNTIME_ENGINE_H_
