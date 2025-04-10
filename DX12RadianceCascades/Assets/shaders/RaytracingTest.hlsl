//--------------------------------------------------------------------------------------
// RaytracingLibrary.hlsl
//
// Single HLSL file containing a Ray Generation, Miss, Any-Hit and Closest Hit Shader
// Compiled as a single /Tlib_6_3 DXIL shader library.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);

Texture2D<float4> sourceTex : register(t1);
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
    float4 world = mul(float4(screenPos, 0, 1), globalInfo.invViewProjMatrix);

    world.xyz /= world.w;
    origin = globalInfo.cameraPos.xyz;
    direction = normalize(world.xyz - origin);
}

[shader("raygeneration")]
void RayGenerationShader()
{
	// Orthographic projection, just as if we were already in NDC.
    //float2 vpos = DispatchRaysIndex().xy;
    //float3 rayOrigin = float3(-1, 1, -5);
    //
    //rayOrigin.xy += float2(2, -2) * (vpos / float2(dispatchWidth, dispatchHeight));
    //
    //float3 rayDir = float3(0, 0, 1);
    //
    //RayDesc myRay = { rayOrigin, 0.0f, rayDir, 100.0f };
    //RayPayload payload = { 0.0f };
    //
    //uint missShaderIndex = 1;
    //TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, myRay, payload);
    
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
    float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
    renderOutput[DispatchRaysIndex().xy] = float4(barycentrics, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    renderOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
}