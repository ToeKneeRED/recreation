#include "rhi_bindings.hlsli"
// Soft, sun-lit billboard particles. A sphere impostor normal gives volumetric
// shading; the prepass depth fades the particle out as it nears geometry (soft
// particles) and clips it where it is fully occluded.
[[vk::binding(1, 0)]] Texture2D<float> scene_depth : register(t1, space0);  // reversed-z, point-fetched

struct PushData {
  column_major float4x4 view_proj;
  float3 cam_right;
  float near_plane;
  float3 cam_up;
  float soft_fade;
  float3 sun_dir;
  float sun_intensity;
  float3 sun_color;
  float ambient;
  column_major float4x4 prev_view_proj;
  uint emissive;  // 1: hdr additive (fire) - color is radiance, no sun/ambient
  float3 pad;
  float4 cluster_params;  // x slice scale, y slice bias, zw tile size px
  float4 froxel_params;   // x near, y far, z enabled
};
PUSH_CONSTANTS(PushData, push);

struct PointLight {
  float4 pos_radius;
  float4 color_intensity;
  float4 direction_type;
  float4 params;
};
[[vk::binding(2, 0)]] StructuredBuffer<PointLight> lights : register(t2, space0);
[[vk::binding(3, 0)]] StructuredBuffer<uint> cluster_counts : register(t3, space0);
[[vk::binding(4, 0)]] StructuredBuffer<uint> cluster_indices : register(t4, space0);
struct LocalShadowFace {
  column_major float4x4 view_proj;
  float4 rect;
};
[[vk::binding(5, 0)]] StructuredBuffer<LocalShadowFace> local_shadow_faces : register(t5, space0);
[[vk::combinedImageSampler]] [[vk::binding(6, 0)]] Texture2D local_shadow_atlas : register(t6, space0);
[[vk::combinedImageSampler]] [[vk::binding(6, 0)]] SamplerComparisonState local_shadow_sampler : register(s6, space0);
[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] Texture3D<float4> froxel_volume : register(t7, space0);
[[vk::combinedImageSampler]] [[vk::binding(7, 0)]] SamplerState froxel_sampler : register(s7, space0);

static const uint kClusterTilesX = 16;
static const uint kClusterTilesY = 9;
static const uint kClusterSlices = 24;
static const uint kMaxLightsPerCluster = 32;

uint CubeFaceIndex(float3 d) {
  float3 a = abs(d);
  if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
  if (a.y >= a.z) return d.y > 0.0 ? 2u : 3u;
  return d.z > 0.0 ? 4u : 5u;
}

float LocalShadow(uint face_index, float3 world_pos) {
  LocalShadowFace face = local_shadow_faces[face_index];
  float4 clip = mul(face.view_proj, float4(world_pos, 1.0));
  if (clip.w <= 0.0) return 1.0;
  float3 ndc = clip.xyz / clip.w;
  float2 uv = ndc.xy * 0.5 + 0.5;
  if (any(uv < 0.0) || any(uv > 1.0) || ndc.z <= 0.0 || ndc.z >= 1.0) return 1.0;
  const float inset = 1.5 / 512.0;
  uv = clamp(uv, inset, 1.0 - inset) * face.rect.xy + face.rect.zw;
  return local_shadow_atlas.SampleCmpLevelZero(local_shadow_sampler, uv, ndc.z - 0.002);
}

// Clustered lights on a fluffy impostor: wrapped diffuse so puffs read round.
float3 ClusterLight(float3 world_pos, float3 n, float2 sv_xy, float view_z) {
  uint tx = min(uint(sv_xy.x / push.cluster_params.z), kClusterTilesX - 1u);
  uint ty = min(uint(sv_xy.y / push.cluster_params.w), kClusterTilesY - 1u);
  uint tz = uint(clamp(log2(max(view_z, 1e-3)) * push.cluster_params.x +
                       push.cluster_params.y, 0.0, float(kClusterSlices - 1u)));
  uint cluster = (tz * kClusterTilesY + ty) * kClusterTilesX + tx;
  uint count = min(cluster_counts[cluster] & 0xffffu, kMaxLightsPerCluster);
  float3 sum = float3(0.0, 0.0, 0.0);
  for (uint ci = 0; ci < count; ++ci) {
    PointLight pl = lights[cluster_indices[cluster * kMaxLightsPerCluster + ci]];
    float3 to_l = pl.pos_radius.xyz - world_pos;
    float dist2 = dot(to_l, to_l);
    float lr = pl.pos_radius.w;
    if (dist2 >= lr * lr) continue;
    float3 pl_l = to_l * rsqrt(max(dist2, 1e-8));
    uint ltype = uint(pl.direction_type.w + 0.5);
    float falloff = saturate(1.0 - dist2 / (lr * lr));
    falloff *= falloff;
    if (ltype == 1u) {
      float cd = dot(-pl_l, normalize(pl.direction_type.xyz));
      float att = saturate((cd - pl.params.y) / max(pl.params.x - pl.params.y, 1e-4));
      falloff *= att * att;
      if (falloff <= 0.0) continue;
    }
    uint shadow_face = uint(pl.params.w + 0.5);
    if (shadow_face != 0u && ltype <= 1u) {
      uint face = shadow_face - 1u + (ltype == 0u ? CubeFaceIndex(-pl_l) : 0u);
      falloff *= LocalShadow(face, world_pos);
      if (falloff <= 0.0) continue;
    }
    float wrap = saturate((dot(n, pl_l) + 0.6) / 1.6);
    sum += pl.color_intensity.rgb * pl.color_intensity.w * falloff * wrap * 0.25;
  }
  return sum;
}

struct PsIn {
  float4 pos : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 color : COLOR0;
  [[vk::location(2)]] float2 motion : TEXCOORD1;
  [[vk::location(3)]] float3 world_pos : TEXCOORD2;
};

struct PsOut {
  float4 color : SV_Target0;
  float4 motion : SV_Target1;  // xy = velocity; w = alpha for the blend
};

PsOut main(PsIn input) {
  float r2 = dot(input.uv, input.uv);
  if (r2 > 1.0) discard;  // round sprite

  // Sphere impostor normal, oriented to the camera basis.
  float3 cam_forward = normalize(cross(push.cam_right, push.cam_up));
  float3 n = normalize(push.cam_right * input.uv.x + push.cam_up * input.uv.y +
                       cam_forward * sqrt(max(1.0 - r2, 0.0)));
  float ndl = saturate(dot(n, normalize(-push.sun_dir)));
  float3 lit = input.color.rgb * (push.ambient + push.sun_color * push.sun_intensity * ndl * 0.25);

  // Soft particles: fade against the opaque depth (linear view z).
  uint w, h;
  scene_depth.GetDimensions(w, h);
  float scene_d = scene_depth.Load(int3(int2(input.pos.xy), 0)).r;
  float scene_vz = push.near_plane / max(scene_d, 1e-6);
  float part_vz = push.near_plane / max(input.pos.z, 1e-6);
  float soft = saturate((scene_vz - part_vz) / max(push.soft_fade, 1e-3));

  // Lit translucency: the clustered lights (with their shadow maps) wrap
  // around the puff, and the froxel volume's transmittance dims it with the
  // fog in front (the inscatter is already on screen behind it).
  float2 uv_screen = input.pos.xy / float2(w, h);
  float fog_t = 1.0;
  if (push.froxel_params.z > 0.5) {
    float slice = saturate(log2(max(part_vz, push.froxel_params.x) / push.froxel_params.x) /
                           log2(push.froxel_params.y / push.froxel_params.x));
    fog_t = froxel_volume.SampleLevel(froxel_sampler, float3(uv_screen, slice), 0.0).a;
  }
  if (push.emissive == 0u) {
    lit += input.color.rgb * ClusterLight(input.world_pos, n, input.pos.xy, part_vz);
  }

  float alpha = input.color.a * smoothstep(1.0, 0.55, sqrt(r2)) * soft;
  if (alpha <= 0.001) discard;
  lit *= fog_t;
  PsOut o;
  if (push.emissive != 0u) {
    // Additive HDR: the sim authored radiance; a soft gaussian-ish falloff
    // shapes the puff and the blend accumulates the flame body. Bloom does
    // the rest of the glow.
    float shape = exp(-r2 * 2.6) * smoothstep(1.0, 0.7, sqrt(r2));
    o.color = float4(input.color.rgb * shape * soft * fog_t, 0.0);
  } else {
    o.color = float4(lit, alpha);
  }
  o.motion = float4(input.motion, 0.0, alpha);  // alpha-weighted into the motion buffer
  return o;
}
