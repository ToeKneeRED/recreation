// D3D12 backend skeleton. Adapter selection and capability mapping are real;
// the resource/command implementation is not started, so CreateD3D12Device
// reports the adapter it would use and returns null, letting Device::Create
// fall through to Vulkan. The port plan lives in engine/render/RHI.md:
//  - resources: D3D12MA, per-format table mirroring vk_convert.cc
//  - bindings: shader-visible descriptor heap ring; BindTransient stages CPU
//    descriptors and copies into the ring (SM 6.6 heap-indexed for bindless)
//  - barriers: enhanced barriers (D3D12_BARRIER_*), 1:1 with ResourceState
//  - shaders: DXIL sidecar from the existing dxc step (-Fo without -spirv)
//  - TlasInstance/AccelTriangles already match D3D12_RAYTRACING_* layouts

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "core/log.h"
#include "render/rhi/device.h"

namespace rec::render::d3d12 {

using Microsoft::WRL::ComPtr;

std::unique_ptr<Device> CreateD3D12Device(const DeviceDesc& desc, Window& window) {
  (void)window;

  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
    REC_WARN("d3d12: dxgi factory creation failed, backend unavailable");
    return nullptr;
  }

  ComPtr<IDXGIAdapter1> adapter;
  for (u32 i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_ADAPTER_DESC1 adapter_desc;
    adapter->GetDesc1(&adapter_desc);
    if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    // Feature level 12_1 with SM 6.6 is the backend baseline (matches the
    // engine's bindless + ray-query feature set).
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1,
                                    __uuidof(ID3D12Device), nullptr))) {
      char name[128] = {};
      // Lossy but sufficient for the log line.
      for (u32 c = 0; c < 127 && adapter_desc.Description[c]; ++c) {
        name[c] = static_cast<char>(adapter_desc.Description[c] & 0x7f);
      }
      REC_INFO("d3d12: capable adapter found ({}), but the backend is a skeleton; "
               "falling back to vulkan",
               name);
      break;
    }
    adapter.Reset();
  }

  (void)desc;
  return nullptr;
}

}  // namespace rec::render::d3d12
