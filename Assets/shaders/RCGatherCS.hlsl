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
        if (IS_OUT_OF_BOUNDS_RELATIVE(samplingPoint))
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

struct ProbeInfo
{
    float rayCount;
    uint2 probeIndex;
    float2 probeSpacing;
    float rayIndex;
    float startDistance;
    float range;
    float2 texelSize;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    
    uint probesPerDim = THIS_PROBES_PER_DIM(cascadeInfo.cascadeIndex);
    float2 relative = (float2) pixelPos / (cascadeInfo.probePixelSize * probesPerDim);

    if (!IS_OUT_OF_BOUNDS_RELATIVE(relative))
    {
        ProbeInfo probeInfo;
        
        probeInfo.rayCount = THIS_RAYS_PER_PROBE(cascadeInfo.cascadeIndex);
        probeInfo.probeIndex = floor(pixelPos / cascadeInfo.probePixelSize);
        probeInfo.probeSpacing = globals.probeSpacing0 * pow(globals.probeScalingFactor, cascadeInfo.cascadeIndex);
        uint2 rayIndex2D = pixelPos % cascadeInfo.probePixelSize;
        probeInfo.rayIndex = rayIndex2D.x + rayIndex2D.y * cascadeInfo.probePixelSize;
        probeInfo.startDistance = sign(cascadeInfo.cascadeIndex) * GeometricSeriesSum(globals.rayLength0, globals.rayScalingFactor, cascadeInfo.cascadeIndex);
        probeInfo.range = globals.rayLength0 * pow(globals.rayScalingFactor, cascadeInfo.cascadeIndex);
        float d = globals.probeSpacing0 * pow(2.0, cascadeInfo.cascadeIndex + 1);
        probeInfo.range += sign(cascadeInfo.cascadeIndex) * length(float2(d, d));
        probeInfo.texelSize = 1.0f / (probesPerDim * cascadeInfo.probePixelSize);
        
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