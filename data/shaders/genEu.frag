#version 450

layout (location = 0) in vec2 inUV;
layout (location = 0) out float EuResult;
layout (constant_id = 0) const uint NUM_SAMPLES = 1024u;


const float PI = 3.1415926536;

const uint sampleCount = 1024;

vec2 hammersley2d(uint i, uint N) 
{
	// Radical inverse based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
	uint bits = (i << 16u) | (i >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	float rdi = float(bits) * 2.3283064365386963e-10;
	return vec2(float(i) /float(N), rdi);
}

vec3 importanceSampleCos(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float theta = 0.5 * acos(1 - 2 * Xi.y);
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);

    vec3 wi = vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);

    return wi;
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float geometrySmith(float roughness, float NdotV, float NdotL)
{
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001f);
}

float integrateEU(float cosThetaO, float roughness)
{
    vec3 N = vec3(0.0f, 0.0f, 1.0f);
    float sinThetaO = sqrt(1 - cosThetaO * cosThetaO);
    vec3 V = vec3(sinThetaO, 0.0, cosThetaO);
    float total = 0.0;
    for (uint i = 0; i < sampleCount; i++)
    {
        vec2 Xi = hammersley2d(i, sampleCount);
        vec3 L = importanceSampleCos(Xi, N, roughness);
        vec3 H = normalize(L + V);

        float NdotL = dot(N, L);
        float NdotV = dot(N, V);
        float NdotH = dot(N, H);
        float VdotH = dot(V, H);

        if (NdotL > 0.0 && NdotV > 0.0)
        {
            float G = geometrySmith(roughness, NdotV, NdotL);
            float D = DistributionGGX(N, H, roughness);
            float weight = PI * (D * G) / (4 * NdotV);
            total += weight;
        } 
    }
    return total / float(sampleCount);
}

void main()
{
    float NdotV = inUV.x;
    float roughness = inUV.y;
    EuResult = integrateEU(NdotV, roughness);
}


