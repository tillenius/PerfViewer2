#version 460

layout(binding = 0) uniform UniformBufferObject {
    mat4 a;
    int i;
} ubo;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 fragColor;

void main() {
    if (gl_VertexIndex == ubo.i || gl_VertexIndex == ubo.i + 1 || gl_VertexIndex == ubo.i + 2) {
        gl_Position = ubo.a * vec4(pos, 0.0, 1.0);
        fragColor = vec3(1, 0, 0);
    } else {
        gl_Position = ubo.a * vec4(pos, 0.0, 1.0);
        fragColor = color;
    }
}
