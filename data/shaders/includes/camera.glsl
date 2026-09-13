#ifndef ENGINE_CAMERA_GLSL
#define ENGINE_CAMERA_GLSL

layout(std140, set = 0, binding = 0) uniform CameraUniform
{
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
} ubo;

#endif
