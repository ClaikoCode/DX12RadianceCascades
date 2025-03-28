#include "RCCommon.hlsli"

RWTexture2D<float4> target : register(u0);
Texture2D<float4> sceneColor : register(t0);

SamplerState sceneSampler : register(s0);

static const float MATH_PI = 3.1415926535897932384f;
static const float MATH_TAU = MATH_PI * 2.0f;

// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
float GeometricSeriesSum(float a, float r, float n)
{
    return a * (1.0f - pow(r, n)) / (1.0f - r);
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
        
       
    }
}