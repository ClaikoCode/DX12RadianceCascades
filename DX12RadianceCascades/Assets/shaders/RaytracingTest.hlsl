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

uint2 Load2x16BitValues(uint offsetBytes)
{
    // Find the 4-byte aligned address
    uint dwordAlignedOffset = offsetBytes & ~3;
    
    // Load a single 32-bit value
    uint dwordValue = geometryData.Load(dwordAlignedOffset);
    
    // Extract the 16-bit values based on alignment
    uint2 result;
    
    if (dwordAlignedOffset == offsetBytes)
    {
        // Aligned case: first two 16-bit values in the dword
        result.x = dwordValue & 0xffff;
        result.y = (dwordValue >> 16) & 0xffff;
    }
    else if (offsetBytes - dwordAlignedOffset == 2)
    {
        // Offset by 2 bytes: second 16-bit value in this dword and 
        // first 16-bit value in next dword
        result.x = (dwordValue >> 16) & 0xffff;
        
        // Need to load next dword for the second 16-bit value
        uint nextDword = geometryData.Load(dwordAlignedOffset + 4);
        result.y = nextDword & 0xffff;
    }
    else
    {
        // Load next dword for values that cross boundaries
        uint nextDword = geometryData.Load(dwordAlignedOffset + 4);
        
        if (offsetBytes - dwordAlignedOffset == 1)
        {
            // Offset by 1 byte - first value spans bytes 1-2 of first dword
            result.x = ((dwordValue >> 8) & 0xffff);
            // Second value spans byte 3 of first dword and byte 0 of second dword
            result.y = ((dwordValue >> 24) & 0xff) | ((nextDword & 0xff) << 8);
        }
        else // offset by 3 bytes
        {
            // First value spans byte 3 of first dword and byte 0 of second dword
            result.x = ((dwordValue >> 24) & 0xff) | ((nextDword & 0xff) << 8);
            // Second value spans bytes 1-2 of second dword
            result.y = (nextDword >> 8) & 0xffff;
        }
    }
    
    return result;
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
    //uint2 uvs = Load2x16BitValues(vertexByteOffset + uvOffsetInBytes);
    //return float2(f16tof32(uvs.x), f16tof32(uvs.y));
    
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
    
    //DrawLine(WorldRayOrigin(), intersectionPoint, float3(1.0f, 0.0f, 0.0f));
    
    uint2 pixelIndex = DispatchRaysIndex().xy;
    uint2 test = pixelIndex % 25;
        
    if (test.x == 0 && test.y == 0)
    {
        float radius = Remap(0.0f, 2000.0f, 5.0f, 0.2f, RayTCurrent());
        //DrawSphere(intersectionPoint, radius, float3(1.0f, 0.0f, 0.0f));
        DrawAxisAlignedBox(intersectionPoint, radius, float3(1.0f, 1.0f, 1.0f));
        //DrawLine(WorldRayOrigin(), intersectionPoint, float3(1.0f, 0.0f, 0.0f));
    }
    
    
    renderOutput[DispatchRaysIndex().xy] = float4(albedoTex.SampleLevel(sourceSampler, uv, 0).rgb, 1);
    //renderOutput[DispatchRaysIndex().xy] = float4(intersectionPoint, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    renderOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
}