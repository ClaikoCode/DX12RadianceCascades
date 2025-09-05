#include "RCCommon3D.hlsli"

// Needs to be a bilinear sampler with borders set to black with alpha of 1.0
SamplerState linearSampler : register(s0);

Texture2D<float4> cascadeN1 : register(t0);
RWTexture2D<float4> cascadeN : register(u0);

ConstantBuffer<RCGlobals> rcGlobals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);

Texture2D<float> depthBuffer : register(t1);

ConstantBuffer<GlobalInfo> globalInfo : register(b2);

float4 ReadRadianceN1(float2 probeIndex, float2 rayIndex, float2 probeCountPerDimN1, float2 texDims)
{
    float2 sampleTexel = rayIndex * probeCountPerDimN1 + probeIndex + 1.0f;
    float2 sampleUV = sampleTexel / texDims;

    return cascadeN1.SampleLevel(linearSampler, sampleUV, 0);
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    int2 targetDims = 0;
    GetDims(cascadeN, targetDims);
    int2 sourceDims = 0;
    GetDims(cascadeN1, sourceDims);
    
    uint2 pixelPos = DTid.xy;
    float2 uv = pixelPos / float2(sourceDims);
    
    if (!OUT_OF_BOUNDS(pixelPos, targetDims))
    {
        ProbeInfo3D probeInfoN = BuildProbeInfo3DDirFirst(pixelPos, cascadeInfo.cascadeIndex, rcGlobals);
        ProbeInfo3D probeInfoN1 = BuildProbeInfo3DDirFirst(pixelPos, cascadeInfo.cascadeIndex + 1, rcGlobals);
        
        float4 nearRadiance = cascadeN[pixelPos];
        
        // If this ray is obscured (a == 0), the higher cascades should not carry over any information.
        if(IsZero(nearRadiance.a))
        {
            // TODO: Remove this line. No reason at all and only adds ms (albiet very small amounts).
            cascadeN[pixelPos] = nearRadiance;
            return;
        }
        
        float4 normalizedFarRadiance = float4(0.0f, 0.0f, 0.0f, 0.0f);
        
        float2 probeN1Index = probeInfoN.probeIndex * 0.5f;
        // Clamp so sampling doesnt happen between probe groups.
        float2 clampedProbeN1Index = clamp(probeN1Index, 1.0f, probeInfoN1.probesPerDim - 1);
        float2 ratios = frac(clampedProbeN1Index);

        if (rcGlobals.depthAwareMerging)
        {
            int2 depthResolution;
            GetDims(depthBuffer, depthResolution);
            float2 depthTexelSize = 1.0f / depthResolution;
           
            float3 cascadeNWorldPos = GetProbeWorldPos(probeInfoN, depthBuffer, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
            
            DepthTile depthTileN1 = GetDepthTile(probeInfoN1.probeSpacing);
            uint2 cascadeN1SamplePosOrigin = GetDepthSamplePos(depthTileN1, clampedProbeN1Index, depthResolution);
            
            float3 sourcePoints[4];
            float sourceDepths[4]; // Only for debugging.
            for (int i = 0; i < 4; i++)
            {
                int2 offset = TranslateCoord4x1To2x2(i);
                uint2 depthSamplePosN1 = ClampPixelPos(cascadeN1SamplePosOrigin + offset, depthResolution);
                
                float cascadeN1Depth = depthBuffer.Load(int3(depthSamplePosN1, 0));

                sourcePoints[i] = WorldPosFromDepth(cascadeN1Depth, (float2(depthSamplePosN1)) * depthTexelSize, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
                
                // Only for debugging.
                sourceDepths[i] = length(sourcePoints[i] - globalInfo.cameraPos) / 5000.0f;
            }
            
            float4 weights3D = GetBilinearSampleWeights(GetBilinear3dRatioIter(sourcePoints, cascadeNWorldPos, ratios, 2));
            
            //weights3D = 0.25f;
            
            if (false)
            {
                float finalDepth = 0.0f;
                finalDepth += sourceDepths[0] * weights3D[0];
                finalDepth += sourceDepths[1] * weights3D[1];
                finalDepth += sourceDepths[2] * weights3D[2];
                finalDepth += sourceDepths[3] * weights3D[3];
            
                float3 depthCol = finalDepth;
                cascadeN[pixelPos] = float4(depthCol, 1.0f);
                return;
            }
            
            int raysToMerge = rcGlobals.rayScalingFactor;
            for (int i = 0; i < 4; i++)
            {
                int2 probeOffset = TranslateCoord4x1To2x2(i);
                float2 cascadeN1ProbeIndex = probeN1Index + probeOffset;
                cascadeN1ProbeIndex = clamp(cascadeN1ProbeIndex, 1.0f, probeInfoN1.probesPerDim - 1);
                
                float4 farRadianceSum = 0.0f;
                for (int k = 0; k < raysToMerge; k++)
                {
                    int2 rayOffset = TranslateCoord4x1To2x2(k);
                    float2 cascadeN1RayIndex = probeInfoN.rayIndex * 2.0f + rayOffset;

                    float2 cascadeN1PixelPos = cascadeN1RayIndex * probeInfoN1.probesPerDim + cascadeN1ProbeIndex;
                    cascadeN1PixelPos = ClampPixelPos(cascadeN1PixelPos, sourceDims);
                    float4 farRadiance = cascadeN1.Load(int3(cascadeN1PixelPos - 0.5, 0));
                
                    float3 radiance = nearRadiance.a * farRadiance.rgb; // Radiance is only carried if near field is visible.
                    float visibility = nearRadiance.a * farRadiance.a; // Make sure visibility is updated if near field is not visible.
            
                    farRadianceSum += float4(radiance, visibility);
                }
                
                normalizedFarRadiance += farRadianceSum * weights3D[i] / raysToMerge;
            }
        }
        else
        {
            float4 farRadianceSum = 0.0f;
        
            int raysToMerge = rcGlobals.rayScalingFactor;
            for (int i = 0; i < raysToMerge; i++)
            {
                int2 offset = TranslateCoord4x1To2x2(i);
                
                // This is the texel position of the 0th probe of our base probe group.
                uint2 cascadeN1BaseProbeIndex = (probeInfoN.rayIndex * 2 + offset) * probeInfoN1.probesPerDim;
                float2 sampleTexelPos = cascadeN1BaseProbeIndex + clampedProbeN1Index + 0.25f;
                float2 sampleUV = sampleTexelPos / sourceDims;
                float4 farRadiance = cascadeN1.SampleLevel(linearSampler, sampleUV, 0.0f);
                
                float3 radiance = nearRadiance.a * farRadiance.rgb; // Radiance is only carried if near field is visible.
                float visibility = nearRadiance.a * farRadiance.a; // Make sure visibility is updated if near field is not visible.
            
                farRadianceSum += float4(radiance, visibility);
            }
        
            normalizedFarRadiance = farRadianceSum / raysToMerge;
        }

        float4 radianceSum = nearRadiance + normalizedFarRadiance;

        // Write radiance.
        cascadeN[pixelPos] = radianceSum;
    }
    
}