#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> target : register(u0);
Texture2D<float4> sceneColor : register(t0);

SamplerState sceneSampler : register(s0);

float4 RayMarch(float2 origin, float2 direction, float range, float2 texelSize)
{
    int iterationCount = 0;
    float distanceTraveled = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        float2 samplingPoint = origin + direction * min(distanceTraveled, range) * texelSize;
        if (OUT_OF_BOUNDS_RELATIVE(samplingPoint))
        {
            return float4(0.0f, 0.0f, 0.0f, TRANSPARENT);
        }
        
        float4 sceneSample = sceneColor.SampleLevel(sceneSampler, samplingPoint, 0);
        float3 sceneColor = sceneSample.rgb;
        float sdfDistance = sceneSample.a;
        
        if (sdfDistance <= EPSILON)
        {
            return float4(sceneColor.rgb, OPAQUE);
        }
        
        distanceTraveled += sdfDistance;
        iterationCount++;
    }
    
    return float4(0.0f, 0.0f, 0.0f, TRANSPARENT);
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    float sideLen = CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    float2 relative = float2(pixelPos) / sideLen;

    if (!OUT_OF_BOUNDS_RELATIVE(relative))
    {
        ProbeInfo probeInfo = BuildProbeInfo(pixelPos, cascadeInfo.cascadeIndex);
        
        {
            float2 probeOrigin = (float2(probeInfo.probeIndex) + 0.5f) * probeInfo.probeSpacing * probeInfo.texelSize;
            float angle = MATH_TAU * (probeInfo.rayIndex + 0.5f) / probeInfo.rayCount;
            float2 direction = float2(cos(angle), -sin(angle));
            float2 rayOrigin = probeOrigin + (direction * probeInfo.startDistance * probeInfo.texelSize);

            float4 retColor = RayMarch(rayOrigin, direction, probeInfo.range, probeInfo.texelSize);
            target[pixelPos] = retColor;
        }
    }
}