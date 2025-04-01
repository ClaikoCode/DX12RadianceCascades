#pragma once

struct RCGlobals
{
    uint probeScalingFactor; // Per dim.
    uint rayScalingFactor;
    uint probeDim0;
    uint rayCount0;
    float rayLength0;
    float probeSpacing0;
    float sourceSize;
};

struct CascadeInfo
{
    uint cascadeIndex;
};

#define RAYS_PER_PROBE(cascadeIndex, scalingFactor, rayCount0) (rayCount0 * pow(scalingFactor, cascadeIndex))
#define PROBES_PER_DIM(cascadeIndex, scalingFactor, probeDim0) (probeDim0 / pow(scalingFactor, cascadeIndex))

#define THIS_PROBES_PER_DIM(cascadeIndex) PROBES_PER_DIM(cascadeIndex, rcGlobals.probeScalingFactor, rcGlobals.probeDim0)
#define THIS_RAYS_PER_PROBE(cascadeIndex) RAYS_PER_PROBE(cascadeIndex, rcGlobals.rayScalingFactor, rcGlobals.rayCount0)

// In pixels.
#define CASCADE_SIDE_LENGTH(cascadeIndex) (THIS_PROBES_PER_DIM(cascadeIndex) * sqrt(THIS_RAYS_PER_PROBE(cascadeIndex)))

// Remember to use these for the visibility term. They are reversed to normal intuition.
#define OPAQUE 1.0f // (hit)
#define TRANSPARENT 0.0f // (miss)

#define USE_LIGHT_LEAK_FIX 1

ConstantBuffer<RCGlobals> rcGlobals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);

// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
float GeometricSeriesSum(float a, float r, float n)
{
    return a * (1.0f - pow(r, n)) / (1.0f - r);
}

struct ProbeInfo
{
    float rayCount;
    float sideLength; // In pixels;
    uint2 probeIndex;
    float2 probeSpacing;
    float rayIndex;
    float startDistance;
    float range;
    float texelSize;
};

ProbeInfo BuildProbeInfo(uint2 pixelPos, uint cascadeIndex)
{
    ProbeInfo probeInfo;
    
    probeInfo.rayCount = THIS_RAYS_PER_PROBE(cascadeIndex);
    probeInfo.sideLength = sqrt(probeInfo.rayCount);
    
    probeInfo.probeIndex = floor(pixelPos / probeInfo.sideLength);
    probeInfo.probeSpacing = rcGlobals.probeSpacing0 * pow(rcGlobals.probeScalingFactor, cascadeIndex);
    uint2 rayIndex2D = pixelPos % probeInfo.sideLength;
    probeInfo.rayIndex = rayIndex2D.x + rayIndex2D.y * probeInfo.sideLength;
    probeInfo.startDistance = sign(cascadeIndex) * GeometricSeriesSum(rcGlobals.rayLength0, rcGlobals.rayScalingFactor, cascadeIndex);
    probeInfo.range = rcGlobals.rayLength0 * pow(rcGlobals.rayScalingFactor, cascadeIndex);
    
#if USE_LIGHT_LEAK_FIX
    float d = rcGlobals.probeSpacing0 * pow(2.0, cascadeIndex + 1);
    probeInfo.range += sign(cascadeIndex) * length(float2(d, d));
#endif
    
    probeInfo.texelSize = 1.0f / rcGlobals.sourceSize;
    
    return probeInfo;
}

