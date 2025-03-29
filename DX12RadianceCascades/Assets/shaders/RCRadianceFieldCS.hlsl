#include "Common.hlsli"
#include "RCCommon.hlsli"

struct RadianceFieldInfo
{
    uint2 radianceFieldDims;
};

ConstantBuffer<RadianceFieldInfo> radianceFieldInfo : register(b2);

RWTexture2D<float4> radianceField : register(u0);
Texture2D<float4> cascadeN : register(t0);

SamplerState cascadeSampler : register(s0);

float4 FetchCascade(ProbeInfo info, float2 texelIndex, float thetaIndex)
{
    float2 probeTexel = texelIndex * info.sideLength;
    probeTexel += float2(thetaIndex % info.sideLength, thetaIndex / info.sideLength);
    float2 cascadeTexelPosition = probeTexel / CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    
    if (OUT_OF_BOUNDS_RELATIVE(cascadeTexelPosition))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return cascadeN.SampleLevel(cascadeSampler, cascadeTexelPosition, 0);
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 pixelPos = DTid.xy;
    float2 relPos = float2(pixelPos) / radianceFieldInfo.radianceFieldDims;
    
    uint2 probePixelPos = relPos * CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    ProbeInfo probeInfo = BuildProbeInfo(probePixelPos, cascadeInfo.cascadeIndex);
    
    float4 radiance = 0.0f;
    for (float i = 0.0f; i < probeInfo.rayCount; i++)
    {
        radiance += FetchCascade(probeInfo, float2(pixelPos), i);
    }
    
    radianceField[pixelPos] = float4(radiance.rgb / probeInfo.rayCount, 1.0f);
}