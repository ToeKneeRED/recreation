// Screen-space precipitation: layered procedural rain streaks or snow flakes,
// composited over the lit scene. Anchored in world (azimuth, altitude) of the
// view ray so it scrolls correctly with the camera instead of swimming. Driven
// by the weather system's precipitation amount + snow flag. Cheap; no particle
// simulation.

[[vk::binding(0, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> out_image;
[[vk::binding(1, 0)]] Texture2D color_in;

struct PushData {
  column_major float4x4 inv_view_proj;
  float4 camera_pos;  // xyz eye, w time (s)
  float4 params;      // x intensity 0..1, y snow (0 rain / 1 snow), zw unused
  uint2 size;
  uint2 pad;
};
[[vk::push_constant]] PushData pc;

float Hash21(float2 p) {
  p = frac(p * float2(123.34, 345.45));
  p += dot(p, p + 34.345);
  return frac(p.x * p.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= pc.size.x || id.y >= pc.size.y) return;
  int2 px = int2(id.xy);
  float3 scene = color_in.Load(int3(px, 0)).rgb;
  float intensity = pc.params.x;
  float snow = pc.params.y;
  float time = pc.camera_pos.w;
  if (intensity <= 0.0) {
    out_image[px] = float4(scene, 1.0);
    return;
  }

  // World-space view ray -> spherical (azimuth, altitude) for a stable anchor.
  float2 ndc = (float2(px) + 0.5) / float2(pc.size) * 2.0 - 1.0;
  float4 nh = mul(pc.inv_view_proj, float4(ndc, 1.0, 1.0));
  float3 dir = normalize(nh.xyz / nh.w - pc.camera_pos.xyz);
  float az = atan2(dir.x, dir.z);
  float alt = asin(clamp(dir.y, -1.0, 1.0));

  float3 add = 0.0.xxx;
  const int kLayers = 4;
  if (snow < 0.5) {
    // Rain: sparse thin streaks in tall cells (dense across, sparse down) so each
    // reads as a falling, wind-sheared streak rather than a dot.
    const float tilt = 0.14;  // wind lean (couples x into y)
    float total = 0.0;
    for (int l = 0; l < kLayers; ++l) {
      float fl = float(l);
      float scale_x = 120.0 + fl * 55.0;  // dense across
      float scale_y = 24.0 + fl * 10.0;   // sparse down -> tall cells = long streaks
      float speed = 14.0 + fl * 7.0;      // fall speed
      float2 p;
      p.x = az * scale_x;
      p.y = -alt * scale_y + time * speed + p.x * tilt;
      float2 cell = floor(p);
      float2 f = frac(p);
      float h = Hash21(cell + fl * 19.7);
      float present = step(0.80, h);
      float thin = smoothstep(0.10, 0.0, abs(f.x - 0.5));                  // narrow streak
      float along = smoothstep(0.0, 0.06, f.y) * smoothstep(0.95, 0.55, f.y);  // long
      total += present * thin * along * (0.5 + 0.5 * fl / float(kLayers));
    }
    add = float3(0.62, 0.70, 0.82) * total * intensity * 0.9;
  } else {
    // Snow: slow drifting soft round flakes with a gentle sway.
    float total = 0.0;
    for (int l = 0; l < kLayers; ++l) {
      float fl = float(l);
      float scale = 26.0 + fl * 16.0;
      float speed = 0.8 + fl * 0.6;
      float sway = sin(time * 0.6 + fl * 2.1) * 0.25;
      float2 p;
      p.x = (az + sway) * scale;
      p.y = -alt * scale + time * speed;
      float2 cell = floor(p);
      float2 f = frac(p) - 0.5;
      float h = Hash21(cell + fl * 11.3);
      float present = step(0.93, h);
      total += present * smoothstep(0.28, 0.0, length(f));
    }
    add = float3(1.0, 1.0, 1.0) * total * intensity * 0.85;
  }

  out_image[px] = float4(scene + add, 1.0);
}
