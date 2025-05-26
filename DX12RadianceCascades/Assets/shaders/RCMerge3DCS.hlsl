#include "RCCommon3D.hlsli"

// Needs to be a bilinear sampler with borders set to black with alpha of 1.0
SamplerState linearSampler : register(s0);

Texture2D<float4> cascadeN1 : register(t0);
RWTexture2D<float4> cascadeN : register(u0);

ConstantBuffer<RCGlobals> rcGlobals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);

Texture2D<float2> minMaxDepthBuffer : register(t1);

struct GlobalInfo
{
    matrix viewProjMatrix;
    matrix invViewProjMatrix;
    float3 cameraPos;
    matrix invViewMatrix;
    matrix invProjMatrix;
};

ConstantBuffer<GlobalInfo> globalInfo : register(b2);

float4 ReadRadianceN1(float2 probeIndex, float2 rayIndex, float probeCountPerDimN1, float texWidthN1)
{
    float2 sampleTexel = rayIndex * probeCountPerDimN1 + probeIndex;
    float2 sampleUV = sampleTexel / texWidthN1;

    return cascadeN1.SampleLevel(linearSampler, sampleUV, 0);
}

// This function is adapted from a shadertoy project by Alexander Sannikov on deptha aware upscaling: 
// https://www.shadertoy.com/view/4XXSWS
float2 GetBilinear3dRatioIter(float3 src_points[4], float3 dst_point, float2 init_ratio, const int it_count)
{
    float2 ratio = init_ratio;
    for (int i = 0; i < it_count; i++)
    {
        ratio.x = saturate(ProjectLinePerpendicular(lerp(src_points[0], src_points[2], ratio.y), lerp(src_points[1], src_points[3], ratio.y), dst_point));
        ratio.y = saturate(ProjectLinePerpendicular(lerp(src_points[0], src_points[1], ratio.x), lerp(src_points[2], src_points[3], ratio.x), dst_point));
    }
    
    return ratio;
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
    
        float4 nearRadiance = cascadeN[pixelPos];
        float4 normalizedFarRadiance = float4(0.0f, 0.0f, 0.0f, 0.0f);

        float probeCountPerDimN1 = floor(probeInfoN.sideLength / rcGlobals.probeScalingFactor);
        
        if (rcGlobals.depthAwareMerging)
        {
            float2 cascadeN1BaseProbeIndex = float2(probeInfoN.probeIndex) / rcGlobals.probeScalingFactor;
            
            int mipLevel = 0;
            int2 depthTopMipResolution;
            GetMipDims(minMaxDepthBuffer, mipLevel, depthTopMipResolution);
        
            // Find the mip where dimensions match with 
            while (depthTopMipResolution.x != probeInfoN.sideLength && depthTopMipResolution.x != 1)
            {
                depthTopMipResolution /= 2;
                mipLevel++;
            }
        
            // This will happen if dimensions never match, which is an error.
            if (depthTopMipResolution.x == 1)
            {
                // Writes dummy value to signify something went wrong.
                cascadeN[pixelPos] = float4(1.0f, 0.0f, 0.0f, 1.0f);
                return;
            }
            
            float cascadeNDepth = minMaxDepthBuffer.Load(int3(probeInfoN.probeIndex, mipLevel)).g;
            float2 cascadeNUV = probeInfoN.probeIndex / probeInfoN.sideLength;
            float3 cascadeNWorldPos = WorldPosFromDepth(cascadeNDepth, cascadeNUV, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
            
            float3 sourcePoints[4];
            float sourceDepths[4];
            for(int i = 0; i < 4; i++)
            {
                int2 offset = TranslateCoord4x1To2x2(i);
                float2 cascadeN1PixelPos = ClampPixelPos(cascadeN1BaseProbeIndex + offset - 0.5f, probeCountPerDimN1);
                
                float cascadeN1Depth = minMaxDepthBuffer.Load(int3(cascadeN1PixelPos, mipLevel + 1)).g;
                float2 cascadeN1UV = cascadeN1PixelPos / probeCountPerDimN1;

                sourcePoints[i] = WorldPosFromDepth(cascadeN1Depth, cascadeN1UV, globalInfo.invProjMatrix, globalInfo.invViewMatrix);
                sourceDepths[i] = length(sourcePoints[i] - globalInfo.cameraPos) / 5000.0f;
            }
            
            float2 ratios = GetBilinearSampleInfo(probeInfoN.probeIndex).ratios;
            float4 weights3D = GetBilinearSampleWeights(GetBilinear3dRatioIter(sourcePoints, cascadeNWorldPos, ratios, 2));
            
            //weights3D = 0.25f;
            
            if(false)
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
            for(int i = 0; i < 4; i++)
            {
                int2 probeOffset = TranslateCoord4x1To2x2(i);
                float2 cascadeN1ProbeIndex = cascadeN1BaseProbeIndex + probeOffset - 0.5f;
                
                float4 farRadianceSum = 0.0f;
                for(int k = 0; k < raysToMerge; k++)
                {
                    int2 rayOffset = TranslateCoord4x1To2x2(k);
                    float2 cascadeN1RayIndex = float2(probeInfoN.rayIndex * 2.0f) + rayOffset;

                    float2 cascadeN1PixelPos = cascadeN1RayIndex * probeCountPerDimN1 + cascadeN1ProbeIndex;
                    cascadeN1PixelPos = ClampPixelPos(cascadeN1PixelPos, sourceDims);
                    float4 farRadiance = cascadeN1.Load(int3(cascadeN1PixelPos, 0));
                    
                    float3 radiance = nearRadiance.a * farRadiance.rgb; // Radiance is only carried if near field is visible.
                    float visibility = nearRadiance.a * farRadiance.a; // Make sure visibility is updated if near field is not visible.
            
                    farRadianceSum += float4(radiance, visibility);
                }
                
                normalizedFarRadiance += farRadianceSum * weights3D[i] * (1.0f / raysToMerge);
            }
        }
        else
        {
            float2 cascadeN1BaseProbeIndex = float2(probeInfoN.probeIndex + 0.5f) / rcGlobals.probeScalingFactor;
            float4 farRadianceSum = 0.0f;
        
            int raysToMerge = rcGlobals.rayScalingFactor;
            for (int i = 0; i < raysToMerge; i++)
            {
                int2 offset = TranslateCoord4x1To2x2(i);
                float4 farRadiance = ReadRadianceN1(cascadeN1BaseProbeIndex, float2(probeInfoN.rayIndex * 2) + offset, probeCountPerDimN1, sourceDims.x);
            
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