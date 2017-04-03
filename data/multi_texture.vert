#version 450

layout(binding = 0) uniform Uniform_Buffer_Object {
  mat4 model;
  mat4 view;
  mat4 proj;
} ubo;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 3) in vec2 in_tex_coord2;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_tex_coord;
layout(location = 2) out vec2 frag_tex_coord2;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(in_position, 1.0);
    frag_color = in_color;
    frag_tex_coord = in_tex_coord;
    frag_tex_coord2 = in_tex_coord2;
}
