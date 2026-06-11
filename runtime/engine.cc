#include "engine.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

#include "asset/primitives.h"
#include "bethesda/archive.h"
#include "bethesda/converters.h"
#include "core/log.h"
#include "core/math.h"
#include "world/components.h"

namespace rec {
namespace {

struct Spin {
  f32 angle = 0;
  f32 speed = 0.9f;
};

Mat4 TransformMatrix(const world::Transform& transform) {
  return MakeTranslation({transform.position[0], transform.position[1], transform.position[2]}) *
         MakeFromQuat(transform.rotation[0], transform.rotation[1], transform.rotation[2],
                      transform.rotation[3]) *
         MakeScale(transform.scale);
}

}  // namespace

bool Engine::Initialize(const EngineConfig& config) {
  config_ = config;
  jobs_ = std::make_unique<JobSystem>();

  if (!config_.headless) {
    window_ = Window::Create({});
    if (!renderer_.Initialize(config_.renderer, *window_)) return false;
  }

  if (!config_.data_dir.empty()) {
    if (!LoadGameData()) return false;
  } else {
    CreateDemoScene();
  }

  if (!StartNetworking()) return false;

  scheduler_.AddSystem(ecs::Stage::kPostSim, "cell_streaming", [this](ecs::World& world, f32) {
    if (!streamer_) return;
    f32 player_position[3] = {0, 0, 0};  // TODO: from the player entity
    streamer_->Update(world, player_position);
  });

  return true;
}

bool Engine::StartNetworking() {
  net::SessionConfig net_config;
  net_config.port = config_.port;
  net_config.player_name = base::NameString(config_.player_name.c_str());
  net_config.max_clients = config_.max_clients;
  // Joining players replicate as cubes until there are real actor assets.
  net_config.player_mesh = asset::MakeAssetId("builtin/cube").hash;

  if (config_.host_server) {
    auto server = std::make_unique<net::ServerSession>(std::move(net_config));
    if (!server->Start()) return false;
    server_session_ = server.get();
    session_ = std::move(server);
  } else if (!config_.connect_address.empty()) {
    net_config.address = base::String(config_.connect_address.c_str());
    auto client = std::make_unique<net::ClientSession>(std::move(net_config));
    if (!client->Start()) return false;
    client_session_ = client.get();
    session_ = std::move(client);
  } else {
    return true;
  }

  scheduler_.AddSystem(ecs::Stage::kSim, "net", [this](ecs::World& world, f32 dt) {
    session_->Tick(world, dt);
  });
  if (client_session_) {
    // Remote transforms blend between snapshots. With a renderer that runs
    // per frame; headless clients smooth at the fixed step instead.
    const ecs::Stage stage =
        config_.headless ? ecs::Stage::kPostSim : ecs::Stage::kPreRender;
    scheduler_.AddSystem(stage, "net_interpolation", [](ecs::World& world, f32 dt) {
      net::TickInterpolation(world, dt);
    });
  }
  return true;
}

void Engine::CreateDemoScene() {
  asset::Mesh cube = asset::MakeCube(0.7f, asset::MakeAssetId("builtin/cube"));
  asset::Mesh ground = asset::MakeCube(2.5f, asset::MakeAssetId("builtin/ground"));
  if (!config_.headless) {
    renderer_.UploadMesh(cube);
    renderer_.UploadMesh(ground);
  }

  if (!config_.connect_address.empty()) {
    // Clients get their world from server snapshots; the meshes above are
    // uploaded so replicated Renderables resolve. The demo input swings the
    // player cube in a circle to exercise the client-to-server path.
    scheduler_.AddSystem(ecs::Stage::kSim, "demo_input", [this](ecs::World&, f32 dt) {
      if (!client_session_ || !client_session_->joined()) return;
      demo_input_time_ += dt;
      net::PlayerInput input;
      input.move_x = std::cos(demo_input_time_ * 0.8f) * 0.5f;
      input.move_z = std::sin(demo_input_time_ * 0.8f) * 0.5f;
      input.yaw = demo_input_time_ * 0.8f;
      client_session_->SetInput(input);
    });
    REC_INFO("no game data given, joining as demo client");
    return;
  }

  ecs::Entity entity = world_.Create();
  world_.Add(entity, world::Transform{});
  world_.Add(entity, world::Renderable{cube.id});
  world_.Add(entity, Spin{});

  // Ground under the cube so raytraced shadows have something to land on.
  ecs::Entity floor = world_.Create();
  world_.Add(floor, world::Transform{.position = {0, -3.6f, 0}});
  world_.Add(floor, world::Renderable{ground.id});

  if (config_.host_server) {
    world_.Add(entity, net::AllocateNetworkId());
    world_.Add(floor, net::AllocateNetworkId());
  }

  scheduler_.AddSystem(ecs::Stage::kSim, "demo_spin", [](ecs::World& world, f32 dt) {
    world.Each<Spin, world::Transform>([dt](ecs::Entity, Spin& spin, world::Transform& t) {
      spin.angle += spin.speed * dt;
      t.rotation[1] = std::sin(spin.angle * 0.5f);
      t.rotation[3] = std::cos(spin.angle * 0.5f);
    });
  });
  REC_INFO("no game data given, spinning a cube instead");
}

bool Engine::LoadGameData() {
  game_ = config_.game != bethesda::Game::kUnknown
              ? config_.game
              : bethesda::GameProfile::DetectFromDataDir(config_.data_dir);
  if (game_ == bethesda::Game::kUnknown) {
    REC_ERROR("could not detect a supported game in {}", config_.data_dir);
    return false;
  }
  const auto& profile = bethesda::GameProfile::For(game_);
  REC_INFO("detected {}", profile.name);

  MountArchives();
  // Loose files mount last so they win over archives.
  vfs_.Mount(asset::MakeLooseFileProvider(config_.data_dir));

  assets_ = std::make_unique<asset::AssetDatabase>(vfs_);
  bethesda::RegisterConverters(*assets_, profile);

  auto order = bethesda::LoadOrder::FromPluginsTxt(config_.plugins_txt, profile);
  if (!records_.LoadAll(config_.data_dir, order, profile)) return false;
  REC_INFO("{} plugins, {} records", order.plugins().size(), records_.record_count());

  streamer_ = std::make_unique<world::CellStreamer>(records_, *assets_);
  return true;
}

void Engine::MountArchives() {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(config_.data_dir, ec)) {
    std::string path = entry.path().string();
    // TODO: archive order should follow plugin order plus the ini resource
    // lists, alphabetical is a placeholder.
    if (auto provider = bethesda::OpenArchive(path)) vfs_.Mount(std::move(provider));
  }
}

int Engine::Run() {
  while (!quit_.load(std::memory_order_relaxed)) {
    if (window_ && !window_->PumpEvents()) break;

    int steps = timer_.Tick();
    f32 dt = static_cast<f32>(timer_.fixed_step());
    for (int i = 0; i < steps; ++i) {
      scheduler_.RunStage(ecs::Stage::kPreSim, world_, dt);
      scheduler_.RunStage(ecs::Stage::kSim, world_, dt);
      scheduler_.RunStage(ecs::Stage::kPostSim, world_, dt);
    }

    if (!config_.headless) {
      scheduler_.RunStage(ecs::Stage::kPreRender, world_,
                          static_cast<f32>(timer_.frame_delta()));

      render::FrameView view;
      view.camera.eye = {2.4f, 1.8f, 2.4f};
      // Rebuilt every frame so destroyed entities drop out on their own.
      base::UnorderedMap<u64, Mat4> transforms;
      world_.Each<world::Transform, world::Renderable>(
          [&](ecs::Entity entity, world::Transform& transform, world::Renderable& renderable) {
            u64 key = static_cast<u64>(entity.generation) << 32 | entity.index;
            Mat4 current = TransformMatrix(transform);
            const Mat4* prev = prev_transforms_.find(key);
            view.draws.push_back({renderable.mesh.hash, current, prev ? *prev : current});
            transforms.insert(key, current);
          });
      prev_transforms_ = std::move(transforms);
      renderer_.RenderFrame(view);
    } else {
      // No vsync to pace the loop; yield between fixed steps instead of
      // spinning a core.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return 0;
}

void Engine::Shutdown() {
  if (!config_.headless) renderer_.Shutdown();
  jobs_->WaitIdle();
}

}  // namespace rec
