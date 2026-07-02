#include "render/gi/recon_path_tracer.h"

#include "core/log.h"
#include "render/gi/raytracing.h"
#include "render/rhi/device.h"
#include "shaders/recon_atrous_cs_hlsl.h"
#include "shaders/recon_composite_cs_hlsl.h"
#include "shaders/recon_gbuffer_cs_hlsl.h"
#include "shaders/recon_temporal_cs_hlsl.h"

namespace rec::render {
namespace {

constexpr Format kIrradiance = Format::kRGBA16Float;
constexpr Format kNormalRough = Format::kRGBA16Float;
constexpr Format kMoments = Format::kRGBA16Float;
constexpr Format kViewZ = Format::kR32Float;
constexpr Format kMotion = Format::kRG16Float;
constexpr Format kMatId = Format::kR32Uint;

struct GbufferPush {
  Mat4 inv_view_proj;
  Mat4 view_proj;
  Mat4 prev_view_proj;
  f32 camera_pos[4];
  f32 sun_direction[4];
  f32 sun_color[4];
  u32 spp;
  f32 pixel_spread;
  u32 frame_index;
  u32 bounces;
};
struct TemporalPush {
  u32 size[2];
  f32 inv_size[2];
  f32 current_weight_min;
  f32 max_history;
  f32 reset;
  f32 pad;
};
struct AtrousPush {
  u32 size[2];
  u32 step_size;
  f32 normal_phi;
  f32 depth_phi;
  f32 luma_phi;
  u32 spec_mode;
  f32 spec_lobe;
};
struct CompositePush {
  u32 size[2];
  u32 debug_mode;
  f32 max_history;
};

}  // namespace

bool ReconPathTracer::Initialize(Device& device, BindingLayoutHandle bindless_layout) {
  if (!bindless_layout) return false;
  device_ = &device;
  return CreatePipelines(device, bindless_layout);
}

bool ReconPathTracer::CreatePipelines(Device& device, BindingLayoutHandle bindless_layout) {
  // gbuffer: 7 storage outputs, tlas (7), sky (8), noisy specular out (9); set 1 bindless.
  gbuffer_pipeline_ = device.CreateComputePipeline({
      .shader = REC_SHADER(k_recon_gbuffer_cs_hlsl),
      .sets = {{.slots = {{0, BindingType::kStorageImage},
                          {1, BindingType::kStorageImage},
                          {2, BindingType::kStorageImage},
                          {3, BindingType::kStorageImage},
                          {4, BindingType::kStorageImage},
                          {5, BindingType::kStorageImage},
                          {6, BindingType::kStorageImage},
                          {7, BindingType::kAccelStruct},
                          {8, BindingType::kCombinedTextureSampler},
                          {9, BindingType::kStorageImage}}},
               {.shared = bindless_layout}},
      .push_constant_size = sizeof(GbufferPush),
      .debug_name = "recon_gbuffer",
  });

  temporal_pipeline_ = device.CreateComputePipeline({
      .shader = REC_SHADER(k_recon_temporal_cs_hlsl),
      .sets = {{.slots = {{0, BindingType::kStorageImage},
                          {1, BindingType::kStorageImage},
                          {2, BindingType::kSampledImage},
                          {3, BindingType::kSampledImage},
                          {4, BindingType::kSampledImage},
                          {5, BindingType::kSampledImage},
                          {6, BindingType::kSampledImage},
                          {7, BindingType::kSampledImage},
                          {8, BindingType::kSampledImage},
                          {9, BindingType::kSampledImage},
                          {10, BindingType::kSampledImage},
                          {11, BindingType::kSampledImage}}}},
      .push_constant_size = sizeof(TemporalPush),
      .debug_name = "recon_temporal",
  });

  atrous_pipeline_ = device.CreateComputePipeline({
      .shader = REC_SHADER(k_recon_atrous_cs_hlsl),
      .sets = {{.slots = {{0, BindingType::kStorageImage},
                          {1, BindingType::kSampledImage},
                          {2, BindingType::kSampledImage},
                          {3, BindingType::kSampledImage},
                          {4, BindingType::kSampledImage}}}},
      .push_constant_size = sizeof(AtrousPush),
      .debug_name = "recon_atrous",
  });

  composite_pipeline_ = device.CreateComputePipeline({
      .shader = REC_SHADER(k_recon_composite_cs_hlsl),
      .sets = {{.slots = {{0, BindingType::kStorageImage},
                          {1, BindingType::kSampledImage},
                          {2, BindingType::kSampledImage},
                          {3, BindingType::kSampledImage},
                          {4, BindingType::kSampledImage},
                          {5, BindingType::kSampledImage},
                          {6, BindingType::kSampledImage},
                          {7, BindingType::kSampledImage}}}},
      .push_constant_size = sizeof(CompositePush),
      .debug_name = "recon_composite",
  });

  if (!gbuffer_pipeline_ || !temporal_pipeline_ || !atrous_pipeline_ || !composite_pipeline_) {
    REC_ERROR("recon path tracer pipeline creation failed");
    return false;
  }
  return true;
}

void ReconPathTracer::CreateBuffers(Device& device, Extent2D extent) {
  extent_ = extent;
  auto make = [&](PingPong& pp, Format fmt) {
    for (u32 i = 0; i < 2; ++i) {
      pp.image[i] =
          device.CreateImage2D(fmt, extent, kTextureUsageSampled | kTextureUsageStorage);
      pp.state[i] = ResourceState::kUndefined;
    }
  };
  make(accum_, kIrradiance);
  make(moments_, kMoments);
  make(spec_accum_, kIrradiance);
  make(spec_moments_, kMoments);
  make(normal_rough_, kNormalRough);
  make(viewz_, kViewZ);
  make(matid_, kMatId);
  history_invalid_ = true;

  // Prime every owned image to kGeneral so the first frame's barriers have a
  // defined source state (and reads of the not-yet-written prev slot are legal).
  device.ImmediateSubmit([&](CommandList& cmd) {
    base::Vector<TextureBarrier> barriers;
    for (PingPong* pp :
         {&accum_, &moments_, &spec_accum_, &spec_moments_, &normal_rough_, &viewz_, &matid_})
      for (u32 i = 0; i < 2; ++i) {
        barriers.push_back(Transition(pp->image[i], ResourceState::kUndefined,
                                      ResourceState::kGeneral));
        pp->state[i] = ResourceState::kGeneral;
      }
    cmd.TextureBarriers({barriers.data(), barriers.size()});
  });
}

void ReconPathTracer::DestroyBuffers(Device& device) {
  for (PingPong* pp :
       {&accum_, &moments_, &spec_accum_, &spec_moments_, &normal_rough_, &viewz_, &matid_})
    for (u32 i = 0; i < 2; ++i)
      if (pp->image[i]) device.DestroyImage(pp->image[i]);
}

void ReconPathTracer::Resize(Device& device, Extent2D extent) {
  if (extent == extent_ && accum_.image[0]) return;
  DestroyBuffers(device);
  CreateBuffers(device, extent);
}

void ReconPathTracer::Destroy(Device& device) {
  DestroyBuffers(device);
  for (PipelineHandle* p :
       {&gbuffer_pipeline_, &temporal_pipeline_, &atrous_pipeline_, &composite_pipeline_}) {
    device.DestroyPipeline(*p);
    *p = {};
  }
}

void ReconPathTracer::RunTemporal(RenderGraph& graph, ResourceHandle noisy, ResourceHandle ac_c,
                                  ResourceHandle ac_p, ResourceHandle mo_c, ResourceHandle mo_p,
                                  ResourceHandle nr_c, ResourceHandle nr_p, ResourceHandle vz_c,
                                  ResourceHandle vz_p, ResourceHandle id_c, ResourceHandle id_p,
                                  ResourceHandle motion, const Frame& frame) {
  graph.AddPass(
      "recon_temporal",
      [&](RenderGraph::PassBuilder& b) {
        b.Write(ac_c, ResourceUsage::kStorageWrite);
        b.Write(mo_c, ResourceUsage::kStorageWrite);
        for (ResourceHandle h : {noisy, ac_p, nr_c, nr_p, vz_c, vz_p, motion, id_c, id_p, mo_p})
          b.Read(h, ResourceUsage::kSampledCompute);
      },
      [this, ac_c, mo_c, noisy, ac_p, nr_c, nr_p, vz_c, vz_p, motion, id_c, id_p, mo_p,
       frame](PassContext& ctx) {
        ResourceHandle reads[10] = {noisy, ac_p, nr_c, nr_p, vz_c, vz_p, motion, id_c, id_p, mo_p};
        base::Vector<BindingItem> items;
        items.push_back(Bind::Storage(0, ctx.graph->image(ac_c)));
        items.push_back(Bind::Storage(1, ctx.graph->image(mo_c)));
        for (u32 i = 0; i < 10; ++i) {
          items.push_back(Bind::Sampled(i + 2, ctx.graph->image(reads[i])));
        }

        TemporalPush p{};
        p.size[0] = extent_.width; p.size[1] = extent_.height;
        p.inv_size[0] = 1.0f / extent_.width; p.inv_size[1] = 1.0f / extent_.height;
        p.current_weight_min = frame.current_weight_min;
        p.max_history = static_cast<f32>(frame.max_history);
        p.reset = frame.reset ? 1.0f : 0.0f;
        ctx.cmd->BindPipeline(temporal_pipeline_);
        ctx.cmd->BindTransient(0, {items.data(), items.size()});
        ctx.cmd->Push(p);
        ctx.cmd->Dispatch2D(extent_);
      });
}

ResourceHandle ReconPathTracer::RunAtrous(RenderGraph& graph, ResourceHandle in, ResourceHandle ping,
                                          ResourceHandle pong, ResourceHandle nr_c,
                                          ResourceHandle vz_c, ResourceHandle mo_c, u32 passes,
                                          bool spec) {
  ResourceHandle denoised = in;
  for (u32 i = 0; i < passes; ++i) {
    ResourceHandle out = (i & 1u) ? pong : ping;
    graph.AddPass(
        "recon_atrous",
        [&](RenderGraph::PassBuilder& b) {
          b.Write(out, ResourceUsage::kStorageWrite);
          b.Read(in, ResourceUsage::kSampledCompute);
          b.Read(nr_c, ResourceUsage::kSampledCompute);
          b.Read(vz_c, ResourceUsage::kSampledCompute);
          b.Read(mo_c, ResourceUsage::kSampledCompute);
        },
        [this, in, out, nr_c, vz_c, mo_c, i, spec](PassContext& ctx) {
          AtrousPush p{};
          p.size[0] = extent_.width; p.size[1] = extent_.height;
          p.step_size = 1u << i;
          p.normal_phi = 64.0f;
          p.depth_phi = 80.0f;
          p.luma_phi = 4.0f;
          p.spec_mode = spec ? 1u : 0u;
          p.spec_lobe = 8.0f;  // smooth reflectors keep tight lobes, rough ones filter normally
          ctx.cmd->BindPipeline(atrous_pipeline_);
          ctx.cmd->BindTransient(0, {Bind::Storage(0, ctx.graph->image(out)),
                                     Bind::Sampled(1, ctx.graph->image(in)),
                                     Bind::Sampled(2, ctx.graph->image(nr_c)),
                                     Bind::Sampled(3, ctx.graph->image(vz_c)),
                                     Bind::Sampled(4, ctx.graph->image(mo_c))});
          ctx.cmd->Push(p);
          ctx.cmd->Dispatch2D(extent_);
        });
    in = out;
    denoised = out;
  }
  return denoised;
}

void ReconPathTracer::AddToGraph(RenderGraph& graph, RayTracingContext& raytracing, u32 tlas_slot,
                                 BindingSetHandle bindless_set, TextureView sky_view,
                                 SamplerHandle sky_sampler, ResourceHandle output,
                                 const Frame& original_frame) {
  // Freshly (re)created history images hold undefined data; force one reset
  // frame so the temporal pass never blends garbage into the moments EMA.
  Frame frame = original_frame;
  frame.reset |= history_invalid_;
  history_invalid_ = false;

  u32 cur = frame.frame_index & 1u;
  u32 prv = 1u - cur;
  auto imp = [&](const char* name, PingPong& pp, u32 i) {
    return graph.ImportImage(name, pp.image[i], &pp.state[i]);
  };

  // Shared gbuffer history (both signals reproject/reject against the same surface).
  ResourceHandle nr_c = imp("recon_nr_c", normal_rough_, cur);
  ResourceHandle nr_p = imp("recon_nr_p", normal_rough_, prv);
  ResourceHandle vz_c = imp("recon_vz_c", viewz_, cur);
  ResourceHandle vz_p = imp("recon_vz_p", viewz_, prv);
  ResourceHandle id_c = imp("recon_id_c", matid_, cur);
  ResourceHandle id_p = imp("recon_id_p", matid_, prv);
  // Diffuse + specular history.
  ResourceHandle ac_c = imp("recon_ac_c", accum_, cur);
  ResourceHandle ac_p = imp("recon_ac_p", accum_, prv);
  ResourceHandle mo_c = imp("recon_mo_c", moments_, cur);
  ResourceHandle mo_p = imp("recon_mo_p", moments_, prv);
  ResourceHandle sac_c = imp("recon_sac_c", spec_accum_, cur);
  ResourceHandle sac_p = imp("recon_sac_p", spec_accum_, prv);
  ResourceHandle smo_c = imp("recon_smo_c", spec_moments_, cur);
  ResourceHandle smo_p = imp("recon_smo_p", spec_moments_, prv);

  auto tex = [&](const char* name, Format fmt) {
    return graph.CreateTexture({.name = name, .format = fmt, .width = extent_.width,
                                .height = extent_.height});
  };
  ResourceHandle noisy = tex("recon_noisy", kIrradiance);
  ResourceHandle spec_noisy = tex("recon_spec_noisy", kIrradiance);
  ResourceHandle motion = tex("recon_motion", kMotion);
  ResourceHandle albedo = tex("recon_albedo", kIrradiance);
  ResourceHandle emissive = tex("recon_emissive", kIrradiance);
  ResourceHandle ping = tex("recon_ping", kIrradiance);
  ResourceHandle pong = tex("recon_pong", kIrradiance);
  ResourceHandle spec_ping = tex("recon_spec_ping", kIrradiance);
  ResourceHandle spec_pong = tex("recon_spec_pong", kIrradiance);

  // --- 1. gbuffer ---
  graph.AddPass(
      "recon_gbuffer",
      [&](RenderGraph::PassBuilder& b) {
        for (ResourceHandle h : {noisy, nr_c, vz_c, motion, id_c, albedo, emissive, spec_noisy})
          b.Write(h, ResourceUsage::kStorageWrite);
      },
      [this, &raytracing, tlas_slot, bindless_set, sky_view, sky_sampler, noisy, nr_c, vz_c, motion,
       id_c, albedo, emissive, spec_noisy, frame](PassContext& ctx) {
        ResourceHandle outs[7] = {noisy, nr_c, vz_c, motion, id_c, albedo, emissive};
        base::Vector<BindingItem> items;
        for (u32 i = 0; i < 7; ++i) items.push_back(Bind::Storage(i, ctx.graph->image(outs[i])));
        items.push_back(Bind::Accel(7, raytracing.tlas(tlas_slot)));
        items.push_back(Bind::Combined(8, sky_view, sky_sampler));
        items.push_back(Bind::Storage(9, ctx.graph->image(spec_noisy)));

        GbufferPush p{};
        p.inv_view_proj = frame.inv_view_proj;
        p.view_proj = frame.view_proj;
        p.prev_view_proj = frame.prev_view_proj;
        p.camera_pos[0] = frame.camera_pos.x; p.camera_pos[1] = frame.camera_pos.y;
        p.camera_pos[2] = frame.camera_pos.z;
        Vec3 sun = Normalize(frame.sun_direction);
        p.sun_direction[0] = sun.x; p.sun_direction[1] = sun.y; p.sun_direction[2] = sun.z;
        p.sun_direction[3] = frame.sun_intensity;
        p.sun_color[0] = frame.sun_color.x; p.sun_color[1] = frame.sun_color.y;
        p.sun_color[2] = frame.sun_color.z; p.sun_color[3] = frame.sun_radius;
        p.spp = frame.spp < 1 ? 1u : frame.spp;
        p.pixel_spread = frame.pixel_spread;
        p.frame_index = frame.frame_index;
        p.bounces = bounces_;
        ctx.cmd->BindPipeline(gbuffer_pipeline_);
        ctx.cmd->BindTransient(0, {items.data(), items.size()});
        ctx.cmd->BindSet(1, bindless_set);
        ctx.cmd->Push(p);
        ctx.cmd->Dispatch2D(extent_);
      });

  // --- 2. temporal accumulation (diffuse + specular share the gbuffer history) ---
  RunTemporal(graph, noisy, ac_c, ac_p, mo_c, mo_p, nr_c, nr_p, vz_c, vz_p, id_c, id_p, motion,
              frame);
  RunTemporal(graph, spec_noisy, sac_c, sac_p, smo_c, smo_p, nr_c, nr_p, vz_c, vz_p, id_c, id_p,
              motion, frame);

  // --- 3. a-trous (N passes, ping-pong) for each signal ---
  u32 passes = frame.atrous_passes == 0 ? 1u : frame.atrous_passes;
  ResourceHandle denoised = RunAtrous(graph, ac_c, ping, pong, nr_c, vz_c, mo_c, passes, false);
  ResourceHandle spec_denoised =
      RunAtrous(graph, sac_c, spec_ping, spec_pong, nr_c, vz_c, smo_c, passes, true);

  // --- 4. composite ---
  graph.AddPass(
      "recon_composite",
      [&](RenderGraph::PassBuilder& b) {
        b.Write(output, ResourceUsage::kStorageWrite);
        for (ResourceHandle h : {albedo, denoised, emissive, mo_c, nr_c, motion, spec_denoised})
          b.Read(h, ResourceUsage::kSampledCompute);
      },
      [this, output, albedo, denoised, emissive, mo_c, nr_c, motion, spec_denoised,
       frame](PassContext& ctx) {
        ResourceHandle reads[7] = {albedo, denoised, emissive, mo_c, nr_c, motion, spec_denoised};
        base::Vector<BindingItem> items;
        items.push_back(Bind::Storage(0, ctx.graph->image(output)));
        for (u32 i = 0; i < 7; ++i) {
          items.push_back(Bind::Sampled(i + 1, ctx.graph->image(reads[i])));
        }

        CompositePush p{};
        p.size[0] = extent_.width; p.size[1] = extent_.height;
        p.debug_mode = frame.debug_mode;
        p.max_history = static_cast<f32>(frame.max_history);
        ctx.cmd->BindPipeline(composite_pipeline_);
        ctx.cmd->BindTransient(0, {items.data(), items.size()});
        ctx.cmd->Push(p);
        ctx.cmd->Dispatch2D(extent_);
      });
}

}  // namespace rec::render
