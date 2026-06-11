#version 460
#extension GL_EXT_ray_query : require

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec4 in_curr_clip;
layout(location = 2) in vec4 in_prev_clip;
layout(location = 3) in vec3 in_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_motion;

layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;

void main() {
  vec3 n = normalize(in_normal);
  vec3 l = normalize(vec3(0.4, 0.8, 0.3));
  float ndl = max(dot(n, l), 0.0);

  // Hard raytraced shadow toward the directional light. Soft shadows and a
  // denoiser come once there is a dedicated rt shadow pass.
  float shadow = 1.0;
  if (ndl > 0.0) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, tlas,
                          gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xff,
                          in_world_pos + n * 0.01, 0.001, l, 1000.0);
    rayQueryProceedEXT(rq);
    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
      shadow = 0.0;
    }
  }

  vec3 base = vec3(0.55, 0.6, 0.7);
  out_color = vec4(base * (0.15 + 0.85 * ndl * shadow), 1.0);

  vec2 curr = in_curr_clip.xy / in_curr_clip.w;
  vec2 prev = in_prev_clip.xy / in_prev_clip.w;
  out_motion = (prev - curr) * 0.5;
}
