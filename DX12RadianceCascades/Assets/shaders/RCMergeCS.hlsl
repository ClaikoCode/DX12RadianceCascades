#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> cascadeN : register(u0);
Texture2D<float4> cascadeN1 : register(t0);

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
        ProbeInfo probeInfo = BuildProbeInfo(pixelPos, cascadeIndex);
        ProbeInfo probeInfoN1 = BuildProbeInfo(pixelPos, cascadeIndex + 1);
        
        float2 texelIndexN1 = floor((float2(probeInfo.probeIndex) - 1.0f) / 2.0f);
        float2 texelIndexN1_N = floor((texelIndexN1 * 2.0f) + 1.0f);

        float4 radiance = cascadeN[pixelPos];
        radiance.a = 1.0f - radiance.a;
        
        if (radiance.a != 0.0f)
        {
            float4 TL = 0.0f;
            float4 TR = 0.0f;
            float4 BL = 0.0f;
            float4 BR = 0.0f;
            
            const float branchingFactor = rcGlobals.rayScalingFactor;
            for (float i = 0.0f; i < branchingFactor; i++)
            {
                float thetaIndexN1 = probeInfo.rayIndex * branchingFactor + i;
                
                TL += FetchCascade(probeInfoN1, texelIndexN1 + float2(0.0f, 0.0f), thetaIndexN1);
                TR += FetchCascade(probeInfoN1, texelIndexN1 + float2(1.0f, 0.0f), thetaIndexN1);
                BL += FetchCascade(probeInfoN1, texelIndexN1 + float2(0.0f, 1.0f), thetaIndexN1);
                BR += FetchCascade(probeInfoN1, texelIndexN1 + float2(1.0f, 1.0f), thetaIndexN1);
            }
            
            float2 weight = 0.33f + (probeInfo.probeIndex - texelIndexN1_N) * 0.33f;
            
            float4 interpolated = BilinearLerp(TL, TR, BL, BR, weight);
            interpolated.a = 1.0f - interpolated.a;
            radiance += radiance.a * interpolated;
        }
        
        cascadeN[pixelPos] = float4(radiance.rgb, 1.0f);
    }
}