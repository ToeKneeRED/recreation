#ifndef RECREATION_RENDER_AMBIENT_OCCLUSION_H_
#define RECREATION_RENDER_AMBIENT_OCCLUSION_H_

#include "core/math.h"
#include "render/render_graph.h"
#include "render/rhi/resources.h"

namespace rec::render {

class Device;
class RayTracingContext;

// Ray traced ambient occlusion: cosine hemisphere rays through the frame
// TLAS, a running temporal mean reprojected through motion vectors, then a
// depth aware spatial blur. Needs ray query support; the renderer binds a
// white fallback when unavailable or disabled.
class RtaoPass {
 public:
  struct Settings {
    f32 radius = 1.2f;     // meters
    f32 intensity = 1.0f;  // ao = ao^intensity
    u32 ray_count = 2;
  };

  bool Initialize(Device& device);
  void Resize(Device& device, VkExtent2D extent);
  void Destroy(Device& device);

  void Configure(const Settings& settings) { settings_ = settings; }
  void Reset() { history_valid_ = false; }

  // Adds trace + blur and returns the filtered single channel ao target.
  ResourceHandle AddToGraph(RenderGraph& graph, RayTracingContext& raytracing, u32 tlas_slot,
                            ResourceHandle depth, ResourceHandle normals, ResourceHandle motion,
                            const Mat4& inv_view_proj, u32 frame_index);

 private:
  Settings settings_;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout trace_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout trace_layout_ = VK_NULL_HANDLE;
  VkPipeline trace_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout blur_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout blur_layout_ = VK_NULL_HANDLE;
  VkPipeline blur_pipeline_ = VK_NULL_HANDLE;
  GpuImage accum_[2];  // rg16f: ao mean + sample count, ping pong
  VkImageLayout accum_layouts_[2] = {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
  VkExtent2D extent_{};
  bool history_valid_ = false;
};

}  // namespace rec::render

#endif  // RECREATION_RENDER_AMBIENT_OCCLUSION_H_
