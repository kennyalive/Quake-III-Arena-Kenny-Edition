#version 450

// WebGPU keeps the MVP in immediates and supplies clipping data through a uniform buffer
layout(push_constant) uniform Transform {
    mat4 clip_space_xform;
#ifndef WEBGPU
    mat3x4 eye_space_xform;
    vec4 clipping_plane; // in eye space
#endif
};

#ifdef WEBGPU
layout(set = 2, binding = 0) uniform Clipping {
    mat3x4 eye_space_xform;
    vec4 clipping_plane; // in eye space
};
#endif

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_tex_coord0;
layout(location = 3) in vec2 in_tex_coord1;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_tex_coord0;
layout(location = 2) out vec2 frag_tex_coord1;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

void main() {
    vec4 p = vec4(in_position, 1.0);

    gl_Position = clip_space_xform *  p;
    gl_ClipDistance[0] = dot(clipping_plane, vec4(p * eye_space_xform, 1.0));

    frag_color = in_color;
    frag_tex_coord0 = in_tex_coord0;
    frag_tex_coord1 = in_tex_coord1;
}
