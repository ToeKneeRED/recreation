# Render Hardware Interface (RHI)

The renderer is written against the backend-agnostic API in `render/rhi/` —
no pass, system or public header may name a Vulkan/D3D12/console type. Each
backend lives in its own directory and translates:

```
render/rhi/        public API: types.h resources.h bindings.h pipeline.h
                   command_list.h device.h swapchain.h  (+ vulkan_interop.h)
render/vulkan/     Vulkan 1.3 backend (dynamic rendering, sync2, BDA baseline)
render/d3d12/      D3D12 skeleton (FL 12_1 + SM 6.6 target; see d3d12_device.cc)
render/null/       no-op backend: headless/serverside + interface conformance
```

Backend selection: `DeviceDesc::backend` (`kAuto` → platform native, then
Vulkan, then null). Build flags `RECREATION_RHI_VULKAN` / `RECREATION_RHI_D3D12`
gate compilation; the null backend always builds.

## Core model

- **Device** (`rhi/device.h`): resource + pipeline creation, frame ring
  (`BeginFrame(slot)` / `SubmitFrame`), `ImmediateSubmit` for uploads. Owns all
  sync primitives — nothing above the RHI touches fences or semaphores.
- **CommandList** (`rhi/command_list.h`): recording. Dynamic-rendering-style
  raster (`BeginRendering` auto-sets viewport/scissor), compute, transfers,
  acceleration-structure builds, timestamps, debug labels.
- **Resources** (`rhi/resources.h`): `GpuBuffer`/`GpuImage` are value types
  holding an opaque backend handle plus mirrored metadata (`size`, `mapped`,
  `address`, `format`, `extent`). Copies alias; destroy exactly once via the
  Device.
- **Pipelines** (`rhi/pipeline.h`): descriptor-driven. A pass declares its
  binding slots inline in the desc; the backend derives and caches set/pipeline
  layouts. Shared sets (bindless, frame globals) pass a `BindingLayoutHandle`.
- **Bindings** (`rhi/bindings.h`): transient per-record sets via
  `cmd->BindTransient(set, {Bind::Storage(0, img), ...})` (replaces the
  allocate/update/bind descriptor dance); persistent sets via
  `Device::CreateBindingSet` + `UpdateBindingSet` (bindless registry).
- **States** (`rhi/types.h`): images move between coarse `ResourceState`s
  (`kGeneral`, `kShaderRead*`, `kColorTarget`, ...). The render graph derives
  inter-pass barriers; manual transitions use
  `cmd->Barrier(Transition(img, before, after))`. Buffer hazards use coarse
  scopes: `cmd->MemoryBarrier(BarrierScope::kComputeWrite, BarrierScope::kIndirectArgs)`.
- **Shaders**: HLSL SM 6.6 compiled by dxc. The build embeds SPIR-V and
  `REC_SHADER(k_foo_cs_hlsl)` wraps it as a `ShaderBlob`. A DXIL sidecar (dxc
  without `-spirv`) slots into the same embed step for the d3d12 backend; pass
  code does not change.
- **Samplers**: `device.GetSampler(SamplerDesc{...})` — cached, never destroyed
  by callers.
- **Ray tracing**: ray queries only (no RT pipelines/SBT). `AccelTriangles`,
  `TlasInstance` (layout-identical between VK and D3D12) and
  `Device::CreateAccelStruct`/`cmd->BuildBlas/BuildTlas`. `RayTracingContext`
  (`gi/raytracing.*`) owns BLAS/TLAS lifecycles on top.

## Migration pattern (old raw Vulkan → RHI)

| old | new |
|---|---|
| `VkDescriptorSetLayout` + `VkPipelineLayout` + `VkPipeline` members | one `PipelineHandle` |
| `MakeSetLayout` / `vkCreate*Layout` / `vkCreateComputePipelines` | `device.CreateComputePipeline({.shader, .sets, .push_constant_size, .debug_name})` |
| `vkCreateGraphicsPipelines` + `VkPipelineRenderingCreateInfo` | `device.CreateGraphicsPipeline(...)` with `color_formats`/`depth`/`blend` presets |
| `ctx.allocate_set` + `VkWriteDescriptorSet[]` + `vkUpdateDescriptorSets` + `vkCmdBindDescriptorSets` | `ctx.cmd->BindTransient(set_index, {Bind::...})` after `BindPipeline` |
| `vkCmdBindPipeline` / `vkCmdPushConstants` / `vkCmdDispatch` | `cmd->BindPipeline` / `cmd->Push(pc)` / `cmd->Dispatch2D(extent)` |
| `vkCmdBeginRendering` + viewport/scissor | `cmd->BeginRendering({extent, colors, &depth})` |
| `vkCmdPipelineBarrier2` image barrier | `cmd->Barrier(Transition(image, before, after))` |
| `vkCmdPipelineBarrier2` memory barrier | `cmd->MemoryBarrier(src_scope, dst_scope)` |
| `VkFormat` / `VkExtent2D` / `VkImageLayout` | `Format` / `Extent2D` / `ResourceState` |
| `VK_IMAGE_USAGE_*` / `VK_BUFFER_USAGE_*` | `kTextureUsage*` / `kBufferUsage*` |
| `vkCreateSampler` per pass | `device.GetSampler({...})` |
| `vkGetBufferDeviceAddress` | `buffer.address` (created with `kBufferUsageDeviceAddress`) |
| `VkAccelerationStructureKHR` param | `AccelStructHandle` (`Bind::Accel(slot, tlas)`) |

Exemplars: `screenspace/ssao.*` (compute), `gi/recon_path_tracer.*` (ray
query + history imports + shared bindless set), `gi/raytracing.*` (AS builds),
`core/bindless.*` (persistent update-after-bind set).

## Interop escape hatch

Modules that integrate API-specific SDKs — NRD, DLSS, FSR3, the runtime gui
backend, the thumbnailer — include `rhi/vulkan_interop.h` (guarded by
`RECREATION_RHI_VULKAN`) and pull raw handles via `GetVulkanHandles(device)`,
`GetVkCommandBuffer(cmd)`, `GetVkImage/GetVkImageView/...`. This keeps them
fully functional on the Vulkan backend without leaking Vulkan into the
portable surface. On other backends they must degrade gracefully (feature
unavailable), which the existing option plumbing already handles.

## D3D12 / console port checklist

The skeleton in `d3d12/d3d12_device.cc` documents the mapping:
resources via D3D12MA; a format table mirroring `vk_convert.cc`; transient
bindings staged into a shader-visible descriptor-heap ring (bindless array =
SM 6.6 heap indexing); `ResourceState` → enhanced barriers; DXIL blobs from
the same dxc invocation minus `-spirv`; `TlasInstance`/`AccelTriangles` are
already layout-compatible with `D3D12_RAYTRACING_*`. Push constants map to
root constants; `GbufferPush` (256 B) exceeds comfortable root-signature
budget, so the d3d12 backend should spill push blocks > 128 B into a ring
buffer CBV — transparent to pass code.

Console backends follow the same recipe in their own directory; nothing in
pass code assumes descriptor sets, image layouts or SPIR-V.
