// Depth aware 5x5 blur of the accumulated ao, the cheap half of a denoiser.
// Temporal accumulation upstream does the heavy lifting.

[[vk::image_format("r8")]] [[vk::binding(0, 0)]] RWTexture2D<float> ao_out;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2D accum_map;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState accum_sampler;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] Texture2D depth_map;
[[vk::combinedImageSampler]] [[vk::binding(2, 0)]] SamplerState depth_sampler;

struct PushData {
  float2 inv_size;
  float intensity;
  float pad;
};
[[vk::push_constant]] PushData push;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width, height;
  ao_out.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  int2 p = int2(id.xy);
  int2 size = int2(width, height);

  float center_depth = depth_map.Load(int3(p, 0)).r;
  if (center_depth <= 0.0) {
    ao_out[id.xy] = 1.0;
    return;
  }
  // Linearize-ish comparison scale for reversed-z without matrix math:
  // nearby depths in reversed z stay relatively close.
  float sum = 0.0;
  float weight_sum = 0.0;
  [unroll]
  for (int y = -2; y <= 2; ++y) {
    [unroll]
    for (int x = -2; x <= 2; ++x) {
      int2 q = clamp(p + int2(x, y), int2(0, 0), size - 1);
      float depth = depth_map.Load(int3(q, 0)).r;
      float depth_weight = exp(-abs(depth - center_depth) / max(center_depth * 0.1, 1e-6));
      float ao = accum_map.Load(int3(q, 0)).r;
      sum += ao * depth_weight;
      weight_sum += depth_weight;
    }
  }
  float ao = sum / max(weight_sum, 1e-4);
  ao_out[id.xy] = pow(saturate(ao), push.intensity);
}
