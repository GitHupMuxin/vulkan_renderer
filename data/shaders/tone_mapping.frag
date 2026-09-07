#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D sceneColorHdr;

layout (push_constant) uniform ToneMappingParams
{
    float exposure;
} params;

vec3 Uncharted2Tonemap(vec3 color)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((color * (A * color + C * B) + D * E) /
        (color * (A * color + B) + D * F)) - E / F;
}

void main()
{
    vec3 hdrColor = texture(sceneColorHdr, inUV).rgb * params.exposure;
    vec3 mappedColor = Uncharted2Tonemap(hdrColor);
    mappedColor /= Uncharted2Tonemap(vec3(11.2));

    // Swapchain 使用 sRGB 格式时会把这里的线性结果编码为 sRGB。
    outColor = vec4(mappedColor, 1.0);
}
