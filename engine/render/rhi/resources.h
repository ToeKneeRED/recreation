#ifndef RECREATION_RENDER_RHI_RESOURCES_H_
#define RECREATION_RENDER_RHI_RESOURCES_H_

#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/types.h"

namespace rec::render {

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  u64 size = 0;
  void* mapped = nullptr;  // set for host visible buffers
};

struct GpuImage {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
};

struct GpuMesh {
  GpuBuffer vertices;
  GpuBuffer indices;
  u32 index_count = 0;
  u32 vertex_count = 0;
};

}  // namespace rec::render

#endif  // RECREATION_RENDER_RHI_RESOURCES_H_
