#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform Push {
  mat4 mvp;
  mat4 model;
} push;

layout(location = 0) out vec3 out_normal;

void main() {
  gl_Position = push.mvp * vec4(in_position, 1.0);
  out_normal = mat3(push.model) * in_normal;
}
