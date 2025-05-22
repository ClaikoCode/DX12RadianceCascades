//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):	James Stanard
// Modified by: Jonathan Dell'Ova

#include "Common.hlsli"

Texture2D<float4> baseColorTexture : register(t0);
Texture2D<float3> metallicRoughnessTexture : register(t1);
Texture2D<float1> occlusionTexture : register(t2);
Texture2D<float3> emissiveTexture : register(t3);
Texture2D<float3> normalTexture : register(t4);

SamplerState baseColorSampler : register(s0);
SamplerState metallicRoughnessSampler : register(s1);
SamplerState occlusionSampler : register(s2);
SamplerState emissiveSampler : register(s3);
SamplerState normalSampler : register(s4);

TextureCube<float3> radianceIBLTexture : register(t10);
TextureCube<float3> irradianceIBLTexture : register(t11);
Texture2D<float> texSSAO : register(t12);
Texture2D<float> texSunShadow : register(t13);

cbuffer MaterialConstants : register(b0)
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float normalTextureScale;
    float2 metallicRoughnessFactor;
    uint flags;
}

cbuffer GlobalConstants : register(b1)
{
    float4x4 ViewProj;
    float4x4 SunShadowMatrix;
    float3 ViewerPos;
    float3 SunDirection;
    float3 SunIntensity;
    float _pad;
    float IBLRange;
    float IBLBias;
}

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
#ifndef NO_TANGENT_FRAME
    float4 tangent : TANGENT;
#endif
    float2 uv0 : TEXCOORD0;
#ifndef NO_SECOND_UV
    float2 uv1 : TEXCOORD1;
#endif
    float3 worldPos : TEXCOORD2;
    float3 sunShadowCoord : TEXCOORD3;
};

float3 ComputeNormal(VSOutput vsOutput)
{
    float3 normal = normalize(vsOutput.normal);

#ifdef NO_TANGENT_FRAME
    return normal;
#else
    // Construct tangent frame
    float3 tangent = normalize(vsOutput.tangent.xyz);
    float3 bitangent = normalize(cross(normal, tangent)) * vsOutput.tangent.w;
    float3x3 tangentFrame = float3x3(tangent, bitangent, normal);

    // Read normal map and convert to SNORM (TODO:  convert all normal maps to R8G8B8A8_SNORM?)
    normal = normalTexture.Sample(normalSampler, vsOutput.uv0) * 2.0 - 1.0;

    // glTF spec says to normalize N before and after scaling, but that's excessive
    normal = normalize(normal * float3(normalTextureScale, normalTextureScale, 1));

    // Multiply by transpose (reverse order)
    return mul(normal, tangentFrame);
#endif
}

struct PSOutput
{
    float4 albedoBuffer : SV_Target0;
    float4 normalBuffer : SV_Target1;
};

PSOutput main(VSOutput vsOutput)
{
    float2 samplingCoords = vsOutput.uv0;
    float4 baseColor = baseColorFactor * baseColorTexture.Sample(baseColorSampler, samplingCoords);
    float3 emissiveColor = emissiveFactor * emissiveTexture.Sample(emissiveSampler, samplingCoords);
    float3 normal = ComputeNormal(vsOutput);
    
    float attenuation = dot(normal, normalize(SunDirection));
    //attenuation = 1.0f;
    
    float3 finalColor = baseColor.rgb * attenuation * SunIntensity + emissiveColor + baseColor.rgb * 0.1f;
    
    PSOutput psOutput;
    
    psOutput.albedoBuffer = float4(baseColor.rgb, 1.0f);
    //psOutput.albedoBuffer = float4(finalColor, 1.0f);
    psOutput.normalBuffer = float4(normal, 1.0f);
    return psOutput;
}