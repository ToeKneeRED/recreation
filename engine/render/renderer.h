#ifndef RECREATION_RENDER_RENDERER_H_
#define RECREATION_RENDER_RENDERER_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "asset/mesh.h"
#include "core/math.h"
#include "core/window.h"
#include "render/antialiasing.h"
#include "render/mesh_pipeline.h"
#include "render/raytracing.h"
#include "render/render_graph.h"
#include "render/rhi/device.h"
#include "render/rhi/swapchain.h"
#include "render/upscaler.h"

namespace rec::render {

struct RendererDesc {
  bool enable_validation = false;
  AntiAliasingMode aa_mode = AntiAliasingMode::kTaa;
  UpscalerKind upscaler = UpscalerKind::kNone;
  RayTracingSettings raytracing;
  bool enable_raytracing = true;
};

struct CameraPose {
  Vec3 eye{0, 0, 3};
  Vec3 target{};
  f32 fov_y = 1.0472f;  // 60 degrees
};

// What the simulation hands the renderer each frame. The engine extracts
// this from the ECS, keeping the renderer free of gameplay types.
struct DrawItem {
  u64 mesh = 0;  // AssetId hash of an uploaded mesh
  Mat4 transform = Mat4::Identity();
};

struct FrameView {
  CameraPose camera;
  std::vector<DrawItem> draws;
};

class Renderer {
 public:
  Renderer();
  ~Renderer();

  bool Initialize(const RendererDesc& desc, Window& window);
  void RenderFrame(const FrameView& view);
  void Shutdown();

  // Makes a mesh drawable, keyed by its asset id. No-op without a device.
  bool UploadMesh(const asset::Mesh& mesh);

  // Switching AA or upscaler at runtime resets temporal history.
  void SetAntiAliasing(AntiAliasingMode mode);
  void SetUpscaler(UpscalerKind kind);

  const DeviceCaps* caps() const;

 private:
  static constexpr u32 kFramesInFlight = 2;
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  struct FrameResources {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
  };

  void BuildFrameGraph();
  bool CreateFrameResources();
  void DestroyFrameResources();
  void RecreateSwapchain();
  void RecordFrame(VkCommandBuffer cmd, u32 image_index, const FrameView& view);

  RendererDesc desc_;
  Window* window_ = nullptr;
  std::unique_ptr<Device> device_;
  std::unique_ptr<Swapchain> swapchain_;
  GpuImage depth_target_;
  std::unique_ptr<MeshPipeline> mesh_pipeline_;
  std::unordered_map<u64, GpuMesh> meshes_;
  FrameResources frames_[kFramesInFlight];
  std::unique_ptr<Upscaler> upscaler_;
  std::unique_ptr<RayTracingContext> raytracing_;
  RenderGraph graph_;
  TaaPass taa_;
  u32 frame_index_ = 0;
  u32 render_width_ = 0;
  u32 render_height_ = 0;
  u32 output_width_ = 0;
  u32 output_height_ = 0;
};

}  // namespace rec::render

#endif  // RECREATION_RENDER_RENDERER_H_
