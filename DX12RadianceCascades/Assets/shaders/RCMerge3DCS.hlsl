#include "RCCommon3D.hlsli"

// Needs to be a bilinear sampler with borders set to black with alpha of 1.0
SamplerState linearSampler : register(s0);

Texture2D<float4> cascadeN1 : register(t0);
RWTexture2D<float4> cascadeN : register(u0);

ConstantBuffer<RCGlobals> rcGlobals : register(b0);
ConstantBuffer<CascadeInfo> cascadeInfo : register(b1);


float4 ReadRadianceN1(float2 probeIndex, float2 rayIndex, float probeCountPerDimN1, float texWidthN1)
{
    float2 sampleTexel = rayIndex * probeCountPerDimN1 + probeIndex;
    float2 sampleUV = sampleTexel / texWidthN1;
        
    return cascadeN1.SampleLevel(linearSampler, sampleUV, 0);
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    int2 targetDims = GetDims(cascadeN);
    int2 sourceDims = GetDims(cascadeN1);
    
    uint2 pixelPos = DTid.xy;
    
    if (!OUT_OF_BOUNDS(pixelPos, targetDims))
    {
        ProbeInfo3D probeInfoN = BuildProbeInfo3DDirFirst(pixelPos, cascadeInfo.cascadeIndex, rcGlobals);
        float4 nearRadiance = cascadeN[pixelPos];
     
        float2 cascadeN1ProbeIndex = float2(probeInfoN.probeIndex + 0.5f) / rcGlobals.probeScalingFactor;

        float probeCountPerDimN1 = floor(probeInfoN.sideLength / rcGlobals.probeScalingFactor);
        float4 farRadianceSum = 0.0f;
        for (int i = 0; i < 4; i++)
        {
            int2 offset = TranslateCoord4x1To2x2(i);
            float4 farRadiance = ReadRadianceN1(cascadeN1ProbeIndex, float2(probeInfoN.rayIndex * 2) + offset, probeCountPerDimN1, sourceDims.x);
            
            float3 radiance = nearRadiance.a * farRadiance.rgb; // Radiance is only carried if near field is visible.
            float visibility = nearRadiance.a * farRadiance.a; // Make sure visibility is updated if near field is not visible.
            
            // Because of the interpolation when sampling the alpha, if some rays are visible and some are not, the colors should be weighted accordingly.
            farRadianceSum += float4(radiance, visibility) * 0.25f; // Normalize value as each sample takes 4 probes into account.
        }
        
        // TODO: This should be weighted by the depth as well.
        float4 radianceSum = nearRadiance + farRadianceSum;

        // Write radiance.
        cascadeN[pixelPos] = radianceSum;
    }
}