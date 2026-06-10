#ifndef RECREATION_RENDER_MESH_PIPELINE_H_
#define RECREATION_RENDER_MESH_PIPELINE_H_

#include <memory>

#include "core/math.h"
#include "render/rhi/device.h"

namespace rec::render {

struct MeshPushConstants {
  Mat4 mvp;
  Mat4 model;
};

// Bringup pipeline: classic vertex buffer, push constant matrices, lambert
// shading, reversed z depth. The real material system replaces this once
// descriptors and bindless textures land.
class MeshPipeline {
 public:
  static std::unique_ptr<MeshPipeline> Create(Device& device, VkFormat color_format,
                                              VkFormat depth_format);
  ~MeshPipeline();

  MeshPipeline(const MeshPipeline&) = delete;
  MeshPipeline& operator=(const MeshPipeline&) = delete;

  void Bind(VkCommandBuffer cmd);
  void Draw(VkCommandBuffer cmd, const GpuMesh& mesh, const MeshPushConstants& push);

 private:
  explicit MeshPipeline(Device& device) : device_(device) {}

  Device& device_;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
};

}  // namespace rec::render

#endif  // RECREATION_RENDER_MESH_PIPELINE_H_
