#ifndef RCCOMMON_H
#define RCCOMMON_H

#include "Common.hlsli"

#define RAYS_PER_PROBE(cascadeIndex, scalingFactor, rayCount0) (rayCount0 * pow(scalingFactor, cascadeIndex))
#define PROBES_PER_DIM(cascadeIndex, scalingFactor, probeDim0) (probeDim0 / pow(scalingFactor, cascadeIndex))

#define USE_LIGHT_LEAK_FIX 0

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

struct ProbeInfo3D
{
    float rayCount;
    float sideLength; // In pixels.
    int2 probeIndex; // Also relative pixel position inside a direction group.
    int2 rayIndex;
    float startDistance; // In world-units.
    float range; // In world-units.
};

// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
float GeometricSeriesSum(float a, float r, float n)
{
    return a * (1.0f - pow(r, n)) / (1.0f - r);
}

// Octahedral projection of a vector inside a sphere.
// Formulas adapted from: A Survey of Efficient Representations for Independent Unit Vectors
// URL: https://jcgt.org/published/0003/02/01/paper.pdf 
float2 Float3ToOct(in float3 v)
{
    float2 p = v.xy * (1.0f / (abs(v.x) + abs(v.y) + abs(v.z)));
    return (v.z <= 0.0f) ? ((1.0f - abs(p.yx)) * sign(p)) : p;
}

float3 OctToFloat3(in float2 e)
{
    float3 v = float3(e.xy, 1.0f - abs(e.x) - abs(e.y));
    if(v.z < 0.0f)
    {
        v.xy = (1.0f - abs(v.yx)) * sign(v.xy);
    }
    
    return normalize(v);
}

/* Decode UV coordinates with interval [-1, 1] into a direction (point on a sphere). */
float3 OctToFloat3EqualArea(float2 e)
{
    /* Equal-Area Mapping <https://pbr-book.org/4ed/Geometry_and_Transformations/Spherical_Geometry#x3-Equal-AreaMapping> */
    float2 v = abs(e);
    float sdist = 1.0f - (v.x + v.y);
    float r = 1.0f - abs(sdist);
    float phi = ((v.y - v.x) / r + 1.0) * 0.785398f; // Magic number is PI / 4
    float r_sqr = r * r;
    float z = sign(sdist) * (1.0 - r_sqr);
    float cos_phi = sign(e.x) * cos(phi);
    float sin_phi = sign(e.y) * sin(phi);
    float r_scl = r * sqrt(2.0 - r_sqr);
    
    return float3(cos_phi * r_scl, sin_phi * r_scl, z);
}

ProbeInfo BuildProbeInfo(uint2 pixelPos, uint cascadeIndex, RCGlobals rcGlobals)
{
    ProbeInfo probeInfo;
    
    probeInfo.rayCount = RAYS_PER_PROBE(cascadeIndex, rcGlobals.rayScalingFactor, rcGlobals.rayCount0);
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

ProbeInfo3D BuildProbeInfo3DDirFirst(uint2 pixelPos, uint cascadeIndex, RCGlobals rcGlobals)
{
    ProbeInfo3D probeInfo3D;
    
    probeInfo3D.rayCount = RAYS_PER_PROBE(cascadeIndex, rcGlobals.rayScalingFactor, rcGlobals.rayCount0);
    // Side length is determined by the probe count per dim instead of ray count per dim.
    probeInfo3D.sideLength = PROBES_PER_DIM(cascadeIndex, rcGlobals.probeScalingFactor, rcGlobals.probeDim0);
    
    probeInfo3D.probeIndex = pixelPos % probeInfo3D.sideLength;
    probeInfo3D.rayIndex = floor(pixelPos / probeInfo3D.sideLength);
    
    probeInfo3D.startDistance = abs(GeometricSeriesSum(rcGlobals.rayLength0, rcGlobals.rayScalingFactor, cascadeIndex));
    probeInfo3D.range = rcGlobals.rayLength0 * pow(rcGlobals.rayScalingFactor, cascadeIndex);
    
    return probeInfo3D;
}

float3 GetRCRayDir(int2 rayIndex, int raysPerDim)
{
    float2 rayIndexFloat = rayIndex;
    rayIndexFloat += 0.5f;
    
    float2 uvCoord = Remap(int2(0, 0), int2(raysPerDim, raysPerDim), -1.0f, 1.0f, rayIndexFloat);
    
    return normalize(OctToFloat3EqualArea(uvCoord));
}

#endif // RCCOMMON_H