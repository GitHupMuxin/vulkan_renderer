#ifndef ENGINE_SCENE_PARAMS_GLSL
#define ENGINE_SCENE_PARAMS_GLSL

layout(std140, set = 0, binding = 1) uniform SceneParamUniform
{
    vec4 lightDir;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient;
    float debugViewInputs;
    float debugViewEquation;
    float debugBSDFType;
} uboParams;

#endif
