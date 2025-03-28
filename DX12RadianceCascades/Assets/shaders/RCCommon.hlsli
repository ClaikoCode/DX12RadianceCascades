#pragma once

struct RCGlobals
{
    uint probeScalingFactor; // Per dim.
    uint rayScalingFactor;
    uint probeDim0;
    uint rayCount0;
    float rayLength0;
    float probeSpacing0;
    uint2 sourceColorResolution;
};

struct CascadeInfo
{
    uint cascadeIndex;
};

#define RAYS_PER_PROBE(cascadeIndex, scalingFactor, rayCount0) (rayCount0 * pow(scalingFactor, cascadeIndex))
#define PROBES_PER_DIM(cascadeIndex, scalingFactor, probeDim0) (probeDim0 / pow(scalingFactor, cascadeIndex))

#define THIS_PROBES_PER_DIM(cascadeIndex) PROBES_PER_DIM(cascadeIndex, globals.probeScalingFactor, globals.probeDim0)
#define THIS_RAYS_PER_PROBE(cascadeIndex) RAYS_PER_PROBE(cascadeIndex, globals.rayScalingFactor, globals.rayCount0)

// In pixels.
#define CASCADE_SIDE_LENGTH(cascadeIndex) THIS_PROBES_PER_DIM(cascadeIndex) * sqrt(THIS_RAYS_PER_PROBE(cascadeIndex))

// Remember to use these for the visibility term. They are reversed to normal intuition.
#define OPAQUE 0.0f // (hit)
#define TRANSPARENT 1.0f // (miss)

#define USE_LIGHT_LEAK_FIX 1

ConstantBuffer<RCGlobals> globals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);

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

ProbeInfo BuildProbeInfo(uint2 pixelPos, uint cascadeIndex)
{
    ProbeInfo probeInfo;
    
    probeInfo.rayCount = THIS_RAYS_PER_PROBE(cascadeIndex);
    float probePixelSideLength = sqrt(probeInfo.rayCount);
    
    probeInfo.probeIndex = floor(pixelPos / probePixelSideLength);
    probeInfo.probeSpacing = globals.probeSpacing0 * pow(globals.probeScalingFactor, cascadeIndex);
    uint2 rayIndex2D = pixelPos % probePixelSideLength;
    probeInfo.rayIndex = rayIndex2D.x + rayIndex2D.y * probePixelSideLength;
    probeInfo.startDistance = sign(cascadeIndex) * GeometricSeriesSum(globals.rayLength0, globals.rayScalingFactor, cascadeIndex);
    probeInfo.range = globals.rayLength0 * pow(globals.rayScalingFactor, cascadeIndex);
    
#if USE_LIGHT_LEAK_FIX
    float d = globals.probeSpacing0 * pow(2.0, cascadeIndex + 1);
    probeInfo.range += sign(cascadeIndex) * length(float2(d, d));
#endif
    
    probeInfo.texelSize = 1.0f / (THIS_PROBES_PER_DIM(cascadeIndex) * probePixelSideLength);
    
    return probeInfo;
}