#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> cascadeN : register(u0);
Texture2D<float4> cascadeN1 : register(t0);

// This HAS to be a point sampler. It makes no sense in this context to use a linear sampler.
SamplerState mergeSampler : register(s0);

float4 FetchCascade(ProbeInfo info, float2 texelIndex, float thetaIndex)
{
    float2 probeTexel = texelIndex * info.sideLength;
    probeTexel += float2(thetaIndex % info.sideLength, thetaIndex / info.sideLength);
    float2 cascadeTexelPosition = probeTexel / CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    
    if (OUT_OF_BOUNDS_RELATIVE(cascadeTexelPosition))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return cascadeN1.SampleLevel(mergeSampler, cascadeTexelPosition, 0);
}

float4 FetchCascade2(ProbeInfo info, float2 probeIndex, float thetaIndex)
{
    float2 probeStartTexel = probeIndex * info.sideLength;
    float2 probeSampleTexel = probeStartTexel + float2(thetaIndex % info.sideLength, thetaIndex / info.sideLength);
    float2 cascadeSampleCoord = probeSampleTexel / CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    
    if (OUT_OF_BOUNDS_RELATIVE(cascadeSampleCoord))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    return cascadeN1.SampleLevel(mergeSampler, cascadeSampleCoord, 0);
}

float4 BilinearLerp(float4 TL, float4 TR, float4 BL, float4 BR, float2 weight)
{    
    return lerp(lerp(TL, TR, weight.x), lerp(BL, BR, weight.x), weight.y);
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint cascadeIndex = cascadeInfo.cascadeIndex;
    uint2 pixelPos = DTid.xy;
    float cascadeSideLength = CASCADE_SIDE_LENGTH(cascadeIndex);
    float2 relative = float2(pixelPos) / cascadeSideLength;

    if (!OUT_OF_BOUNDS_RELATIVE(relative))
    {
        ProbeInfo probeInfoN = BuildProbeInfo(pixelPos, cascadeIndex);
        ProbeInfo probeInfoN1 = BuildProbeInfo(pixelPos, cascadeIndex + 1);
        
        float2 probeIndexN1 = floor((float2(probeInfoN.probeIndex) - float2(1.0f, 1.0f)) / rcGlobals.probeScalingFactor);
        
        float4 radiance = cascadeN[pixelPos];
        if (radiance.a == 0.0f)
        {
            cascadeN[pixelPos] = float4(radiance.rgb, 1.0f - radiance.a);
            return;
        }
        
        float4 TL = 0.0f;
        float4 TR = 0.0f;
        float4 BL = 0.0f;
        float4 BR = 0.0f;
            
        const float branchingFactor = rcGlobals.rayScalingFactor;
        for (float i = 0.0f; i < branchingFactor; i++)
        {
            float thetaIndexN1 = probeInfoN.rayIndex * branchingFactor + i;
                
            TL += FetchCascade2(probeInfoN1, probeIndexN1 + float2(0.0f, 0.0f), thetaIndexN1);
            TR += FetchCascade2(probeInfoN1, probeIndexN1 + float2(1.0f, 0.0f), thetaIndexN1);
            BL += FetchCascade2(probeInfoN1, probeIndexN1 + float2(0.0f, 1.0f), thetaIndexN1);
            BR += FetchCascade2(probeInfoN1, probeIndexN1 + float2(1.0f, 1.0f), thetaIndexN1);
        }
            
        float2 probeIndexDistance = (probeInfoN.probeIndex - float2(1.0f, 1.0f)) - probeIndexN1 * 2.0f;
        float2 weight = 0.33f + probeIndexDistance * 0.33f;
            
        float4 interpolated = BilinearLerp(TL, TR, BL, BR, weight);
        cascadeN[pixelPos] = radiance + interpolated;
    }
}