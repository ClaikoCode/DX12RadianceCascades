#include "Common.hlsli"
#include "RCCommon3D.hlsli"

SamplerState linearSampler : register(s0);

Texture2D<float4> albedoBuffer : register(t0);
Texture2D<float4> normalBuffer : register(t1);
Texture2D<float4> diffuseRadianceBuffer : register(t2);

Texture2D<float2> cascade0MinMaxDepth : register(t3);
Texture2D<float> depthBuffer : register(t4);

Texture2D<float4> cascade0RadianceBuffer : register(t5);

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
    
    int2 outputDims;
    GetDims(albedoBuffer, outputDims);
    float2 texelSize = 1.0f / outputDims;
    
    float4 albedo = albedoBuffer.Sample(linearSampler, uv);
    float4 normals = normalBuffer[uv * outputDims];
    float4 diffuseRadiance = diffuseRadianceBuffer.Sample(linearSampler, uv);
    
    // Yellow if values was never changed (useful for debugging).
    float4 finalColor = float4(1.0f, 1.0f, 0.0f, 1.0f);
    
    // If normals match the buffers clear value, the pixel has not been written to. 
    if(IsZero(normals.rgb))
    {
       // Apply the background color.
       // TODO: When a proper skybox is added (which doesnt write normal info), this step should return early, keeping the color of the skybox.
       finalColor = float4(diffuseRadiance.rgb, 1.0f);
    }
    // Alpha value informs if this pixel was emissive or not. 
    else if(normals.a == 1.0f) 
    {
        // If emissive, write the albedo color (which contains emissive light info) directly. 
        // This ensures crisp emissives as RC can produce discontinues emissive values that look strange/wrong for emissives with lower strength.
        finalColor = float4(albedo.rgb, 1.0f);
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
            if (true)
            {
                normals = float4(normals.rgb, 1.0f);
                
                float3 radiance = 0.0f;
                
                uint rayCount = rcGlobals.rayCount0;
                // If the merge has been pre-averaged it means that each original ray has 
                // casted a ray equal to its ray scaling factor. 
                if (rcGlobals.usePreAveraging)
                {
                    rayCount /= rcGlobals.rayScalingFactor;
                }
                
                uint rayCount0Sqrt = sqrt(rayCount);
                int2 probeCounts = int2(rcGlobals.probeCount0X, rcGlobals.probeCount0Y);
                
                float2 probeUV = uv / rayCount0Sqrt;
                float2 probeUVSize = 1.0f / (outputDims / rayCount0Sqrt);
                probeUV = clamp(probeUV, probeUVSize, 1.0f / rayCount0Sqrt);
                
                float4 summedRadiance = 0.0f;
                for (int i = 0; i < rayCount0Sqrt; i++)
                {
                    for (int k = 0; k < rayCount0Sqrt; k++)
                    {
                        int2 rayIndex = int2(i, k);
      
                        float3 rayDir = GetRCRayDir(rayIndex, rayCount0Sqrt);
                        float normalAttenuation = clamp(dot(normals.rgb, rayDir), 0.0f, 1.0f);
                        
                        int2 probeIndex = uv * probeCounts;
                        float4 radianceSample = cascade0RadianceBuffer[probeIndex + rayIndex * probeCounts];
                        
                        summedRadiance += radianceSample * normalAttenuation;
                    }
                }
                
                radiance = summedRadiance.rgb / rayCount;
                finalColor = float4(albedo.rgb * radiance, 1.0f);
            }
            else
            {
                float4 radiance = diffuseRadianceBuffer.Sample(linearSampler, uv);
                finalColor = float4(albedo.rgb, 1.0f) * radiance;
            }
        }
    }
    
    return finalColor;
}