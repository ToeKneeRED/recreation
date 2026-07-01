#ifndef RECREATION_RENDER_RHI_SWAPCHAIN_H_
#define RECREATION_RENDER_RHI_SWAPCHAIN_H_

#include "core/types.h"
#include "render/rhi/resources.h"
#include "render/rhi/types.h"

namespace rec::render {

enum class AcquireResult : u8 {
  kOk,
  kSuboptimal,  // usable this frame; recreate when convenient
  kOutOfDate,   // recreate before rendering
  kFailed,
};

// Presentation images. Created via Device::CreateSwapchain; presentation goes
// through Device::SubmitFrame (which owns the acquire/present sync for the
// frame slot passed to Acquire).
class Swapchain {
 public:
  virtual ~Swapchain() = default;

  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;

  // Acquires the next image using frame slot `slot`'s sync primitives; the
  // following Device::SubmitFrame with the same slot waits on it.
  virtual AcquireResult Acquire(u32 slot, u32* out_image_index) = 0;

  virtual Format format() const = 0;
  virtual Extent2D extent() const = 0;
  virtual u32 image_count() const = 0;
  virtual const GpuImage& image(u32 index) const = 0;
  // True when the backbuffer can be sampled (e.g. for UI backdrop blur).
  virtual bool can_sample() const = 0;

 protected:
  Swapchain() = default;
};

}  // namespace rec::render

#endif  // RECREATION_RENDER_RHI_SWAPCHAIN_H_
