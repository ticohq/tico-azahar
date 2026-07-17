// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 vert_position;
layout (location = 1) in vec4 vert_color;

layout (location = 0) out vec4 frag_color;

void main() {
    frag_color = vert_color;
    gl_Position = vec4(vert_position, 0.0, 1.0);
}
