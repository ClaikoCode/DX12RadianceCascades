#include "Common.hlsli"

SamplerState linearSampler : register(s0);

Texture2D<float3> albedoBuffer : register(t0);
Texture2D<float4> normalBuffer : register(t1);
Texture2D<float4> diffuseRadianceBuffer : register(t2);

struct Interpolators
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

float4 main(Interpolators i) : SV_TARGET
{
    float2 uv = i.texcoord;
    
    float4 albedo = float4(albedoBuffer.Sample(linearSampler, uv), 1.0f);
    float4 normals = normalBuffer.Sample(linearSampler, uv);
    float4 diffuseRadiance = diffuseRadianceBuffer.Sample(linearSampler, uv);
	
    float4 finalColor;
    if(IsZero(normals))
    {
       finalColor = float4(diffuseRadiance.rgb, 1.0f);
    }
    else
    {
        finalColor = albedo * diffuseRadiance;
    }
    
    return finalColor;
}