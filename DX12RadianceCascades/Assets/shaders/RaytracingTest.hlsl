//--------------------------------------------------------------------------------------
// RaytracingLibrary.hlsl
//
// Single HLSL file containing a Ray Generation, Miss, Any-Hit and Closest Hit Shader
// Compiled as a single /Tlib_6_3 DXIL shader library.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "DebugDraw.hlsli"

#define BARYCENTRIC_NORMALIZATION(bary, val1, val2, val3) (bary.x * val1 + bary.y * val2 + bary.z * val3)

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);
SamplerState sourceSampler : register(s0);

cbuffer Params : register(b0)
{
    uint dispatchWidth;
    uint dispatchHeight;
    uint rayFlags;
    float holeSize;
};

struct GlobalInfo
{
    matrix viewProjMatrix;
    matrix invViewProjMatrix;
    float3 cameraPos;
};

ConstantBuffer<GlobalInfo> globalInfo : register(b1);

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

#define InverseLerpClamped(a, b, val) (saturate((val - a) / (b - a)))
#define Remap(a, b, c, d, val) (lerp(c, d, InverseLerpClamped(a, b, val)))

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

[shader("raygeneration")]
void RayGenerationShader()
{
    float3 rayOrigin;
    float3 rayDir;
    
    GenerateCameraRay(DispatchRaysIndex().xy, rayOrigin, rayDir);
    
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMax = 100000.0f;
    ray.TMin = 0.001f;
    
    RayPayload payload = { 0.0f };

    TraceRay(Scene, rayFlags, ~0, 0, 1, 0, ray, payload);
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
    float3 triangleCentre = 1.0f / 3.0f;

    float distanceToCentre = length(triangleCentre - barycentrics);

    if (distanceToCentre < holeSize)
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = GetBarycentrics(attr.barycentrics);
    
    // POS: 3 x 32 bits (float)
    // NORMAL: 32 bits (unorm)
    // TANGENT: 32 bits (unorm)
    // TEXCOORD: 2 x 16 bits (float)
    const uint vertexSizeInBytes = 3 * 4 + 4 + 4 + 2 * 2;
    
    const uint indexSizeInBytes = 2;
    const uint3 vertexIndices = Load3x16BitIndices(geomOffsets.indexByteOffset + PrimitiveIndex() * 3 * indexSizeInBytes);
    
    const uint3 vertexByteOffsets = vertexIndices * vertexSizeInBytes + geomOffsets.vertexByteOffset;
    const uint uvOffset = vertexSizeInBytes - (2 * 2);
    const float2 uv0 = LoadUVFromVertex(vertexByteOffsets.x, uvOffset);
    const float2 uv1 = LoadUVFromVertex(vertexByteOffsets.y, uvOffset);
    const float2 uv2 = LoadUVFromVertex(vertexByteOffsets.z, uvOffset);
    const float2 uv = BARYCENTRIC_NORMALIZATION(barycentrics, uv0, uv1, uv2);
    
    float3 intersectionPoint = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    uint2 pixelIndex = DispatchRaysIndex().xy;
    
    if(true)
    {
        uint2 pixelIndexSubset = ((pixelIndex + 15) % 40);
        
        if (pixelIndexSubset.x == 0 && pixelIndexSubset.y == 0)
        {
            float radius = Remap(0.0f, 1000.0f, 5.0f, 0.2f, RayTCurrent());
            float3 minColor = float3(1.0f, .0f, 0.0f);
            float3 maxColor = float3(1.0f, 1.0f, 1.0f);
            float3 visColor = Remap(0.0f, 1000.0f, minColor, maxColor, RayTCurrent());
            DrawSphere(intersectionPoint, 1.0f, visColor);
            //DrawAxisAlignedBox(intersectionPoint, radius, float3(1.0f, 1.0f, 1.0f));
            //DrawLine(WorldRayOrigin(), intersectionPoint, float3(1.0f, 0.0f, 0.0f));
        }
    }
    
    
    
    renderOutput[pixelIndex] = float4(albedoTex.SampleLevel(sourceSampler, uv, 0).rgb, 1);
    //renderOutput[pixelIndex] = float4(intersectionPoint, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    renderOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
}