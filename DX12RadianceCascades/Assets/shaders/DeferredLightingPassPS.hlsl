#include "Common.hlsli"
#include "RCCommon3D.hlsli"

SamplerState linearSampler : register(s0);

Texture2D<float3> albedoBuffer : register(t0);
Texture2D<float4> normalBuffer : register(t1);
Texture2D<float4> diffuseRadianceBuffer : register(t2);

Texture2D<float2> cascade0MinMaxDepth : register(t3);
Texture2D<float> depthBuffer : register(t4);

ConstantBuffer<GlobalInfo> globalInfo : register(b0);
ConstantBuffer<RCGlobals> rcGlobals : register(b1);

struct Interpolators
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

float4 main(Interpolators i) : SV_TARGET
{
    float2 uv = i.texcoord;
    
    float4 albedo = float4(albedoBuffer.Sample(linearSampler, uv), 1.0f);
    float4 normals = normalBuffer.Sample(linearSampler, uv);
    float4 diffuseRadiance = diffuseRadianceBuffer.Sample(linearSampler, uv);

    float4 finalColor;
    if(IsZero(normals))
    {
       finalColor = float4(diffuseRadiance.rgb, 1.0f);
    }
    else
    {
        // Completely broken for now.
        if (rcGlobals.depthAwareMerging)
        {
            int2 depthBufferDims;
            GetDims(depthBuffer, depthBufferDims);
            int2 depthTexel = floor(uv * depthBufferDims);
        
            float targetDepth = depthBuffer.Load(int3(depthTexel, 0));
            float3 targetPos = WorldPosFromDepth(targetDepth, uv, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
        
            int2 cascade0MinMaxDepthDims;
            GetDims(cascade0MinMaxDepth, cascade0MinMaxDepthDims);
            int2 cascade0DepthTexel = floor(uv * cascade0MinMaxDepthDims);
        
            float3 cascade0SourcePoints[4];
            float4 cascade0SourceRadiance[4];
            for (int i = 0; i < 4; i++)
            {
                int2 offset = TranslateCoord4x1To2x2(i);
                int2 samplePoint = cascade0DepthTexel + offset;
            
                float sourceDepth = cascade0MinMaxDepth.Load(int3(samplePoint, 0)).g;
                cascade0SourcePoints[i] = WorldPosFromDepth(sourceDepth, float2(samplePoint + 0.5f) / cascade0MinMaxDepthDims, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
                cascade0SourceRadiance[i] = diffuseRadianceBuffer.Load(int3(samplePoint, 0));
            }
        
            BilinearSampleInfo bilinearSample = GetBilinearSampleInfo(depthTexel - 0.5f);
            float4 weights3D = GetBilinearSampleWeights(GetBilinear3dRatioIter(cascade0SourcePoints, targetPos, bilinearSample.ratios, 2));
        
            float4 radianceWeightedSum = 0.0f;
            for (int i = 0; i < 4; i++)
            {
                radianceWeightedSum += cascade0SourceRadiance[i] * weights3D[i];
            }
        
            finalColor = albedo * radianceWeightedSum;
        }
        else
        {
            float4 radiance = diffuseRadianceBuffer.Sample(linearSampler, uv);
            finalColor = albedo * radiance;
        }
       
    }
    
    return finalColor;
}