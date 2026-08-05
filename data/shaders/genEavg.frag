#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out float EuResult;
layout (constant_id = 0) const uint NUM_SAMPLES = 1024u;

layout (set = 0, binding = 0) uniform sampler2D Eu;

const float PI = 3.1415926536;

ivec2 size = textureSize(Eu, 0);

float integrateEavg(float roughness)
{
    float total = 0.0f;
    float samplerCount = size.x;
    int r = int(roughness * (size.y - 1));
    for (int i = 0; i < samplerCount; i++)
    {
        total += texelFetch(Eu, ivec2(i, r), 0).x;
    }
    return total / float(samplerCount);
}

void main()
{
    float NdotV = inUV.x;
    float roughness = inUV.y;
    EuResult = integrateEavg(roughness);
}
