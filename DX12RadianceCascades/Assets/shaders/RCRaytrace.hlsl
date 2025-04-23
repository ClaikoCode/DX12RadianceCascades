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
#include "RCCommon3D.hlsli"

#define BARYCENTRIC_NORMALIZATION(bary, val1, val2, val3) (bary.x * val1 + bary.y * val2 + bary.z * val3)

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);
SamplerState sourceSampler : register(s0);

struct GlobalInfo
{
    matrix viewProjMatrix;
    matrix invViewProjMatrix;
    float3 cameraPos;
    matrix invViewMatrix;
    matrix invProjMatrix;
};

ConstantBuffer<GlobalInfo> globalInfo : register(b0);

struct GeometryOffsets
{
    uint indexByteOffset;
    uint vertexByteOffset;
};

ByteAddressBuffer geometryData : register(t0, space1);

Texture2D<float4> albedoTex     : register(t1, space1);
Texture2D<float4> metalRoughTex : register(t2, space1);
Texture2D<float4> occlusionTex  : register(t3, space1);
Texture2D<float4> emissiveTex   : register(t4, space1);
Texture2D<float4> normalTex     : register(t5, space1);

ConstantBuffer<GeometryOffsets> geomOffsets : register(b0, space1);


// RC Related Buffers
ConstantBuffer<RCGlobals> rcGlobals : register(b1);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b2);

RWTexture2D<float4> depthTex : register(u1);

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

struct RayPayload
{
    float dummy; // Minimum of 4 bytes required for payloads.
};

inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0f;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0.0f, 1.0f), globalInfo.invViewProjMatrix);

    world.xyz /= world.w;
    origin = globalInfo.cameraPos.xyz;
    direction = normalize(world.xyz - origin);
}

inline RayDesc GenerateProbeRay(uint2 index)
{
    RayDesc ray;
    
    ProbeInfo3D probeInfo3D = BuildProbeInfo3DDirFirst(index, cascadeInfo.cascadeIndex, rcGlobals);

    float3 rayDir = GetRCRayDir(probeInfo3D.rayIndex, sqrt(probeInfo3D.rayCount));

    float3 rayOrigin;
    {
        uint cascadeIndex = cascadeInfo.cascadeIndex;
        int2 pixelPos = probeInfo3D.probeIndex;
        float depthVal = depthTex[pixelPos].g; // R is min, G is max.
        // Add small distance towards the camera to avoid wall clipping.
        // Otherwise, probes will spawn inside a wall and its rays wont interect any geometry.
        depthVal += 0.00000001f;
        
        uint width;
        uint height;
        depthTex.GetDimensions(width, height);
        
        float2 texelSize = 1.0f / float2(width, height);
        float3 worldPos = WorldPosFromDepth(depthVal, (float2(pixelPos) + 0.5f) * texelSize,  globalInfo.invProjMatrix, globalInfo.invViewMatrix);
        
        rayOrigin = worldPos;
    }
    
    // FOR SOME REASON SETTING THE VALUES MANUALLY GIVES OK BUT 
    // IF SET WITH PROBE INFO, IT JUST DIES. THIS IS EVEN THOUGH
    // THE VALUES FOR TMIN AND TMAX ARE CONSISTENT UP UNTIL TRACE RAY CALL.
    
    ray.Direction = rayDir;
    ray.Origin = rayOrigin;
    ray.TMax = 10.0f;
    ray.TMin = probeInfo3D.startDistance;
    
    //DrawSphere(rayOrigin, 3.0f, float3(1.0f, 0.0f, 0.0f));
    //DrawLine(rayOrigin, rayOrigin + rayDir * ray.TMax, float3(1.0f, 0.0f, 0.0f));

    return ray;
}

[shader("raygeneration")]
void RayGenerationShader()
{
    RayDesc ray = GenerateProbeRay(DispatchRaysIndex().xy);
    
    RayPayload payload = { 0.0f };

    //uint2 res = (DispatchRaysIndex().xy % 4);
    //if (length(res) == 0)
    //{
    //    //DrawLine(ray.Origin, ray.Origin + ray.Direction, float3(1.0f, 0.0f, 0.0f));
    //}
    
    //ray.TMin = 0.0f;
    TraceRay(Scene, 0, ~0, 0, 1, 0, ray, payload);
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    //float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
    //float3 triangleCentre = 1.0f / 3.0f;
    //
    //float distanceToCentre = length(triangleCentre - barycentrics);
    //
    //if (distanceToCentre < holeSize)
    //    IgnoreHit();
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
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
    
    uint2 pixelIndex = DispatchRaysIndex().xy;
    DrawLine(WorldRayOrigin(), WorldRayOrigin() + WorldRayDirection() * 2.0f, float3(1.0f, 0.0f, 0.0f));
    renderOutput[pixelIndex] = float4(albedoTex.SampleLevel(sourceSampler, uv, 0).rgb, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    //DrawLine(WorldRayOrigin(), WorldRayOrigin() + WorldRayDirection() * 2.0f, float3(0.0f, 1.0f, 0.0f));
    renderOutput[DispatchRaysIndex().xy] = float4(1, 1, 0, 1);
}