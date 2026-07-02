// D3D12 swapchain. There is no DXGI through vkd3d on Linux, so the baseline
// is an offscreen ring of three presentable render targets: Acquire cycles
// them, SubmitFrame's fence provides the pacing and "present" is a no-op.
// The whole frame graph (and REC_UI_SHOT screenshot readback) runs unchanged.
// A DXGI flip-model swapchain for Windows is future work wired at the same
// seam (Create below), tracked in RHI.md.

#include "render/d3d12/d3d12_backend.h"

#include "core/log.h"

namespace rec::render::d3d12 {

std::unique_ptr<D3D12Swapchain> D3D12Swapchain::Create(D3D12Device& device, u32 width, u32 height,
                                                       bool vsync) {
  auto swapchain = std::unique_ptr<D3D12Swapchain>(new D3D12Swapchain(device));
  if (!swapchain->Init(width, height, vsync)) return nullptr;
  return swapchain;
}

bool D3D12Swapchain::Init(u32 width, u32 height, bool vsync) {
  (void)vsync;  // nothing to pace against offscreen
  extent_ = {width, height};
  format_ = Format::kBGRA8Unorm;  // matches the vulkan backend's surface pick

  constexpr u32 kImageCount = 3;
  for (u32 i = 0; i < kImageCount; ++i) {
    GpuImage image = device_.CreateImage2D(
        format_, extent_,
        kTextureUsageColorTarget | kTextureUsageSampled | kTextureUsageTransferSrc |
            kTextureUsageTransferDst,
        1);
    if (!image) {
      REC_ERROR("d3d12: offscreen swapchain image creation failed");
      return false;
    }
    images_.push_back(image);
  }
  REC_INFO("swapchain: {}x{}, {} images, offscreen (no display path through vkd3d)",
           extent_.width, extent_.height, kImageCount);
  return true;
}

D3D12Swapchain::~D3D12Swapchain() {
  device_.WaitIdle();
  for (GpuImage& image : images_) device_.DestroyImage(image);
}

AcquireResult D3D12Swapchain::Acquire(u32 slot, u32* out_image_index) {
  (void)slot;  // frame pacing comes from the device's per-slot fences
  *out_image_index = static_cast<u32>(frame_counter_++ % images_.size());
  return AcquireResult::kOk;
}

}  // namespace rec::render::d3d12
