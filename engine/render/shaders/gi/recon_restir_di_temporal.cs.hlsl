#include "rhi_bindings.hlsli"
// ReSTIR DI, stage 1: light-sample reservoirs (Bitterli et al. 2020) for the
// recon path tracer's PRIMARY direct lighting: the sun disk plus the engine's
// dynamic point lights (which the path-traced mode previously dropped).
// Streaming RIS over a discrete candidate set - the sun (deterministic
// proposal, with a fresh disk direction for the penumbra) and K uniform picks
// from the light buffer - then temporal reuse of the reprojected reservoir.
// No rays here; the spatial stage traces ONE alpha-tested shadow ray for the
// winner and shades. Targets are unshadowed contributions, demodulated
// (no albedo), matching the raster forward pass's windowed falloff.
struct ReconRestirDiTemporalPush {
  float4 sun_direction;  // xyz travel direction, w intensity
  float4 sun_color;      // rgb, w sun angular radius (radians)
  uint2 size;
  uint frame_index;
  uint light_count;
  uint candidates;  // uniform light picks per pixel
  float m_max;      // reservoir age cap
  float reset;      // 1 = drop history
  float pad0;
};
PUSH_CONSTANTS(ReconRestirDiTemporalPush, pc);

// Reservoir layout (t = this frame's transient, p = previous persistent):
//   R0: xyz sun-sample disk direction (point lights derive their sample from
//       the id + the CURRENT light buffer, so moving lights stay correct),
//       w = light id as float (0 sun, 1+i point light i, <0 none)
//   R1: x w_sum, y M, z W
[[vk::binding(0, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> r0_out : register(u0, space0);
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> r1_out : register(u1, space0);
[[vk::binding(2, 0)]] Texture2D<float4> p_pos : register(t2, space0);    // primary pos (.w 0 = sky)
[[vk::binding(3, 0)]] Texture2D<float4> curr_nr : register(t3, space0);  // normal + roughness
[[vk::binding(4, 0)]] Texture2D<float4> prev_nr : register(t4, space0);
[[vk::binding(5, 0)]] Texture2D<float> curr_viewz : register(t5, space0);
[[vk::binding(6, 0)]] Texture2D<float> prev_viewz : register(t6, space0);
[[vk::binding(7, 0)]] Texture2D<uint> curr_matid : register(t7, space0);
[[vk::binding(8, 0)]] Texture2D<uint> prev_matid : register(t8, space0);
[[vk::binding(9, 0)]] Texture2D<float2> motion : register(t9, space0);
[[vk::binding(10, 0)]] Texture2D<float4> r0_prev : register(t10, space0);
[[vk::binding(11, 0)]] Texture2D<float4> r1_prev : register(t11, space0);
struct PointLight {
  float4 pos_radius;       // xyz position, w influence radius
  float4 color_intensity;  // rgb color, w intensity
};
[[vk::binding(12, 0)]] StructuredBuffer<PointLight> point_lights : register(t12, space0);

static const float kPi = 3.14159265359;

uint Pcg(inout uint state) {
  state = state * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
float Rand(inout uint state) { return (Pcg(state) & 0xffffffu) / 16777216.0; }

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 DecodeN(float4 nr) { return normalize(nr.xyz * 2.0 - 1.0); }
bool InBounds(int2 p) { return all(p >= 0) && all(p < int2(pc.size)); }

float3 SunDiskDir(inout uint rng) {
  float3 l = normalize(-pc.sun_direction.xyz);
  float radius = pc.sun_color.w;
  if (radius <= 0.0) return l;
  float3 up = abs(l.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
  float3 t1 = normalize(cross(up, l));
  float3 t2 = cross(l, t1);
  float a = 2.0 * kPi * Rand(rng);
  float r = sqrt(Rand(rng)) * radius;
  return normalize(l + t1 * (cos(a) * r) + t2 * (sin(a) * r));
}

// Unshadowed target function, demodulated irradiance luminance. Shared with
// the spatial stage; any change must stay in sync.
float PHatSun(float3 n, float3 disk_dir) {
  return Luma(pc.sun_color.rgb) * pc.sun_direction.w * saturate(dot(n, disk_dir));
}
float PHatLight(float3 x, float3 n, PointLight pl) {
  float3 to_l = pl.pos_radius.xyz - x;
  float dist2 = dot(to_l, to_l);
  float lr = pl.pos_radius.w;
  if (dist2 >= lr * lr) return 0.0;
  float dist = sqrt(max(dist2, 1e-8));
  float ndl = saturate(dot(n, to_l / dist));
  // Radius-windowed falloff, matching mesh.ps exactly.
  float falloff = saturate(1.0 - dist2 / (lr * lr));
  falloff *= falloff;
  return Luma(pl.color_intensity.rgb) * pl.color_intensity.w * falloff * ndl;
}
float PHat(float3 x, float3 n, float light_id, float3 sun_dir) {
  if (light_id < -0.5) return 0.0;
  uint id = (uint)round(light_id);
  if (id == 0u) return PHatSun(n, sun_dir);
  if (id - 1u >= pc.light_count) return 0.0;  // light left the frame's buffer
  return PHatLight(x, n, point_lights[id - 1u]);
}

bool ValidateHistory(int2 cp, int2 pp) {
  if (!InBounds(pp)) return false;
  uint cm = curr_matid.Load(int3(cp, 0));
  uint pm = prev_matid.Load(int3(pp, 0));
  if (cm == 0xffffffffu || pm == 0xffffffffu || cm != pm) return false;
  float cz = curr_viewz.Load(int3(cp, 0));
  float pz = prev_viewz.Load(int3(pp, 0));
  if (abs(cz - pz) / max(cz, 1.0) > 0.05) return false;
  float3 cn = DecodeN(curr_nr.Load(int3(cp, 0)));
  float3 pn = DecodeN(prev_nr.Load(int3(pp, 0)));
  if (dot(cn, pn) < 0.9) return false;
  return true;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  int2 p = int2(tid.xy);
  if (!InBounds(p)) return;

  float4 primary = p_pos.Load(int3(p, 0));
  if (primary.w == 0.0) {  // sky
    r0_out[p] = float4(0, 0, 0, -1.0);
    r1_out[p] = 0.0.xxxx;
    return;
  }
  float3 x = primary.xyz;
  float3 n = DecodeN(curr_nr.Load(int3(p, 0)));
  uint rng = (tid.y * pc.size.x + tid.x) * 20749u + pc.frame_index * 12269u + 5u;

  // Streaming RIS over the candidate set. Candidate weight w = p_hat / pdf:
  // the sun is a deterministic proposal (pdf 1); each of the K uniform picks
  // has pdf 1/N, so w = p_hat * N. The 1/M in W averages the strategies.
  float sel_id = -1.0;
  float3 sel_dir = 0.0.xxx;
  float w_sum = 0.0;
  float M = 0.0;

  float3 sun_dir = SunDiskDir(rng);
  {
    float w = PHatSun(n, sun_dir);
    w_sum += w;
    M += 1.0;
    if (w > 0.0) {  // first candidate: always selected while it is the only mass
      sel_id = 0.0;
      sel_dir = sun_dir;
    }
  }
  for (uint k = 0; k < pc.candidates && pc.light_count > 0u; ++k) {
    uint li = min(uint(Rand(rng) * float(pc.light_count)), pc.light_count - 1u);
    float w = PHatLight(x, n, point_lights[li]) * float(pc.light_count);
    M += 1.0;
    if (!(w > 0.0)) continue;
    w_sum += w;
    if (Rand(rng) < w / w_sum) {
      sel_id = float(li + 1u);
      sel_dir = 0.0.xxx;
    }
  }

  // Temporal reuse: nearest reprojected reservoir, revalidated against the
  // CURRENT light buffer (ids re-resolve, so moving lights stay correct and
  // vanished lights age out through a zero target).
  if (pc.reset == 0.0) {
    float2 uv = (float2(p) + 0.5) / float2(pc.size);
    float2 prev_uv = uv + motion.Load(int3(p, 0));
    int2 pp = int2(floor(prev_uv * float2(pc.size)));
    if (ValidateHistory(p, pp)) {
      float4 q0 = r0_prev.Load(int3(pp, 0));
      float4 q1 = r1_prev.Load(int3(pp, 0));
      float qM = min(q1.y, pc.m_max);
      float qW = q1.z;
      if (qM > 0.0 && qW > 0.0 && q0.w > -0.5) {
        float p_hat = PHat(x, n, q0.w, q0.xyz);
        float w = p_hat * qW * qM;
        if (w > 0.0 && !(w > 1.0e12)) {
          w_sum += w;
          if (Rand(rng) < w / w_sum) {
            sel_id = q0.w;
            sel_dir = q0.xyz;
          }
        }
        M += qM;
      }
    }
  }

  float p_hat_sel = PHat(x, n, sel_id, sel_dir);
  float W = (p_hat_sel > 0.0 && M > 0.0) ? w_sum / (M * p_hat_sel) : 0.0;
  if (!(W < 1.0e12)) W = 0.0;

  r0_out[p] = float4(sel_dir, sel_id);
  r1_out[p] = float4(w_sum, M, W, 0.0);
}
