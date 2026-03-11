#include "Common.hlsli"

struct Interpolators
{
    float3 samplingDirection : POSITION;
    float4 vertexPos : SV_POSITION;
};

Texture2D<float4> equilateralSkybox : register(t0);
//TextureCube<float4> skyboxCubemap : register(t0);

SamplerState skyboxSampler : register(s0);

float4 main(Interpolators i) : SV_TARGET
{
    float3 samplingDir = normalize(i.samplingDirection);
    float2 uv = DirectionToRectilinear(samplingDir);
    uv = DirectionToEquirectangularUV(samplingDir);
    
    // Sample level with lod 0 to avoid seam when sampling edges of texture.
    return float4(equilateralSkybox.SampleLevel(skyboxSampler, uv, 0).rgb, 1.0f);
}