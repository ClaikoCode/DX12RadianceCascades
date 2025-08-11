//--------------------------------------------------------------------------------------
// RaytracingLibrary.hlsl
//
// Single HLSL file containing a Ray Generation, Miss, Any-Hit and Closest Hit Shader
// Compiled as a single /Tlib_6_3 DXIL shader library.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"
#include "DebugDraw.hlsli"
#include "RadianceCascadeVis.hlsli"
#include "RCCommon3D.hlsli"

#define BARYCENTRIC_NORMALIZATION(bary, val1, val2, val3) (bary.x * val1 + bary.y * val2 + bary.z * val3)


RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);
SamplerState sourceSampler : register(s0);

ConstantBuffer<GlobalInfo> globalInfo : register(b0);

struct GeometryOffsets
{
    uint indexByteOffset;
    uint vertexByteOffset;
};

ByteAddressBuffer geometryData : register(t0, space1);

Texture2D<float4> albedoTex : register(t1, space1);
Texture2D<float4> metalRoughTex : register(t2, space1);
Texture2D<float4> occlusionTex : register(t3, space1);
Texture2D<float4> emissiveTex : register(t4, space1);
Texture2D<float4> normalTex : register(t5, space1);

ConstantBuffer<GeometryOffsets> geomOffsets : register(b0, space1);


// RC Related Buffers
ConstantBuffer<RCGlobals> rcGlobals : register(b1);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b2);

RWTexture2D<float> depthTex : register(u1);

float3 GetBarycentrics(float2 inputBarycentrics)
{
    return float3(1.0 - inputBarycentrics.x - inputBarycentrics.y, inputBarycentrics.x, inputBarycentrics.y);
}

uint3 Load3x16BitIndices(uint offsetBytes)
{
    const uint dwordAlignedOffset = offsetBytes & ~3;

    const uint2 four16BitIndices = geometryData.Load2(dwordAlignedOffset);

    uint3 indices;

    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

float2 Load32BitIntTo16BitFloats(uint val)
{
    uint x16 = val & 0xFFFF;
    uint y16 = val >> 16;
    
    return float2(f16tof32(x16), f16tof32(y16));
}

float2 LoadUVFromVertex(uint vertexByteOffset, uint uvOffsetInBytes)
{
    uint uvs = geometryData.Load(vertexByteOffset + uvOffsetInBytes);
    return Load32BitIntTo16BitFloats(uvs);
}

// Minimum of 4 bytes required for payloads.
struct RayPayload
{
    int2 probeIndex;
    float4 result;
};

// Direction is filled outside of this function.
inline RayDesc GenerateProbeRay(ProbeInfo3D probeInfo3D)
{
    RayDesc ray;

    float3 rayOrigin = GetProbeWorldPosRWTex(probeInfo3D, depthTex, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
   
    ray.Origin = rayOrigin;
    ray.TMax = probeInfo3D.startDistance + probeInfo3D.range;
    ray.TMin = probeInfo3D.startDistance;
   
    return ray;
}

[shader("raygeneration")]
void RayGenerationShader()
{
    uint2 pixelPos = DispatchRaysIndex().xy;
    ProbeInfo3D probeInfo3D = BuildProbeInfo3DDirFirst(pixelPos, cascadeInfo.cascadeIndex, rcGlobals);
    RayDesc ray = GenerateProbeRay(probeInfo3D);
    
    RayPayload payload = { probeInfo3D.probeIndex, float4(0.0f, 0.0f, 0.0f, 0.0f) };
    
    DrawProbe(cascadeInfo.cascadeIndex, probeInfo3D.probeIndex, ray.Origin, ray.TMin);
    
    uint rayFlags = RAY_FLAG_NONE;
    
    int sqrtRayCount = sqrt(probeInfo3D.rayCount);
    float4 radianceOutput = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (rcGlobals.usePreAveraging)
    {
        int2 translationDims = sqrt(rcGlobals.rayScalingFactor);
        int2 baseRayIndex = probeInfo3D.rayIndex * translationDims;
        
        // Pre-averaging is done by sampling all upper rays at once.
        for (int i = 0; i < rcGlobals.rayScalingFactor; i++)
        {
            int2 rayIndexOffset = Translate1DTo2D(i, translationDims);

            // Scale with sqrtRayCount to get the correct ray index.
            ray.Direction = GetRCRayDir(baseRayIndex + rayIndexOffset, sqrtRayCount);
            TraceRay(Scene, rayFlags, ~0, 0, 1, 0, ray, payload);
        }
        
        // Normalized sum.
        radianceOutput = payload.result / rcGlobals.rayScalingFactor;
    }
    else
    {
        ray.Direction = GetRCRayDir(probeInfo3D.rayIndex, sqrtRayCount);
        TraceRay(Scene, rayFlags, ~0, 0, 1, 0, ray, payload);
        
        radianceOutput = payload.result;
    }
    
    renderOutput[DispatchRaysIndex().xy] = radianceOutput;
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Empty
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    DrawCascadeRay(float3(1.0f, 0.0f, 0.0f), 0.5f, cascadeInfo.cascadeIndex, payload.probeIndex);
    
    float3 barycentrics = GetBarycentrics(attr.barycentrics);
    
    // POS: 3 x 32 bits (float)
    // NORMAL: 32 bits (unorm)
    // TANGENT: 32 bits (unorm)
    // TEXCOORD: 2 x 16 bits (float)
    const uint vertexSizeInBytes = (3 * 4) + (4) + (4) + (2 * 2);
    
    const uint indexSizeInBytes = 2;
    const uint3 vertexIndices = Load3x16BitIndices(geomOffsets.indexByteOffset + PrimitiveIndex() * 3 * indexSizeInBytes);
    
    const uint3 vertexByteOffsets = vertexIndices * vertexSizeInBytes + geomOffsets.vertexByteOffset;
    const uint uvOffset = vertexSizeInBytes - (2 * 2);
    const float2 uv0 = LoadUVFromVertex(vertexByteOffsets.x, uvOffset);
    const float2 uv1 = LoadUVFromVertex(vertexByteOffsets.y, uvOffset);
    const float2 uv2 = LoadUVFromVertex(vertexByteOffsets.z, uvOffset);
    const float2 uv = BARYCENTRIC_NORMALIZATION(barycentrics, uv0, uv1, uv2);
    
    float3 hitColor = emissiveTex.SampleLevel(sourceSampler, uv, 0).rgb * 4.0f;
    payload.result += float4(hitColor, 0.0f);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    //DrawCascadeRay(float3(0.0f, 1.0f, 0.0f), 0.0f, cascadeInfo.cascadeIndex, payload.probeIndex);
    
    float3 missColor;
    if (cascadeInfo.cascadeIndex == (rcGlobals.cascadeCount - 1))
    {
        float3 sunDir = normalize(float3(1.0, 1.0, 0.0));
        missColor = SimpleSunsetSky(WorldRayDirection(), sunDir);
    }
    else
    {
        missColor = float3(0.0f, 0.0f, 0.0f);
    }
    
    payload.result += float4(missColor, 1.0f);
}