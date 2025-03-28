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
    uint probePixelSize; // Per dim.
    uint cascadeIndex;
};


#define RAYS_PER_PROBE(cascadeIndex, scalingFactor, rayCount0) rayCount0 * pow(scalingFactor, cascadeIndex)
#define PROBES_PER_DIM(cascadeIndex, scalingFactor, probeDim0) probeDim0 / pow(scalingFactor, cascadeIndex)
#define DIRECTION_SPACE_BIAS(dims) float3(1.0f / ((float)dims.x / dims.y), 1.0f, 1.0f) // TODO: Figure out why this is needed.

#define THIS_PROBES_PER_DIM(cascadeIndex) PROBES_PER_DIM(cascadeIndex, globals.probeScalingFactor, globals.probeDim0)
#define THIS_RAYS_PER_PROBE(cascadeIndex) RAYS_PER_PROBE(cascadeIndex, globals.rayScalingFactor, globals.rayCount0)

#define IS_OUT_OF_BOUNDS_RELATIVE(relPos) (relPos.x >= 1.0f || relPos.x < 0.0f || relPos.y >= 1.0f || relPos.x < 0.0f)

// Remember to use these for the visibility term. They are reversed to normal intuition.
#define OPAQUE 0.0f // (hit)
#define TRANSPARENT 1.0f // (miss)

ConstantBuffer<RCGlobals> globals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);

// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
float GeometricSeriesSum(float a, float r, float n)
{
    return a * (1.0f - pow(r, n)) / (1.0f - r);
}