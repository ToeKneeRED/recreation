#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 0) out vec4 out_color;

void main() {
  vec3 n = normalize(in_normal);
  vec3 l = normalize(vec3(0.4, 0.8, 0.3));
  float ndl = max(dot(n, l), 0.0);
  vec3 base = vec3(0.55, 0.6, 0.7);
  out_color = vec4(base * (0.15 + 0.85 * ndl), 1.0);
}
