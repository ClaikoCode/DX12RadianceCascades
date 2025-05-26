#ifndef RADIANCECASCADEVIS_H
#define RADIANCECASCADEVIS_H

#include "RCCommon3D.hlsli"
#include "DebugDraw.hlsli"

struct CascadeVisInfo
{
    bool enableProbeVis;
    uint cascadeVisIndex;
    uint probeSubset;
};

ConstantBuffer<CascadeVisInfo> cascadeVisInfo : register(b127);

#define INCLUDE_IN_DRAW(probeIndex, cascadeIndex) (cascadeVisInfo.enableProbeVis == true && cascadeIndex == cascadeVisInfo.cascadeVisIndex && length(probeIndex % cascadeVisInfo.probeSubset) == 0)

void DrawCascadeRay(float3 color, float tOffset, int cascadeIndex, int2 probeIndex)
{        
    if (INCLUDE_IN_DRAW(probeIndex, cascadeIndex))
    {
        float3 cascadeRayStart = WorldRayOrigin() + WorldRayDirection() * RayTMin();
        float3 cascadeRayEnd = WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() + tOffset);
        
        DrawLine(cascadeRayStart, cascadeRayEnd, color);
    }
}

void DrawProbe(int cascadeIndex, int2 probeIndex, float3 probeOrigin, float probeRadius)
{
    if (INCLUDE_IN_DRAW(probeIndex, cascadeIndex))
    {
        if (probeRadius > EPSILON)
        {
            DrawSphere(probeOrigin, clamp(probeRadius, 0.0f, 2.0f), float3(1.0f, 1.0f, 1.0f));
        }
    }
}

#endif // RADIANCECASCADEVIS_H