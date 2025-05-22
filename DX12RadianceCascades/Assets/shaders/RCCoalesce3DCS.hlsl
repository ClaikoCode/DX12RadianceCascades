#include "RCCommon3D.hlsli"

// Output tex should have a resolution where each pixel corresponds to one probe in cascade 0, i.e. ProbeCountPerDim0 x ProbeCountPerDim0 
// The dispatch should be made with this resolution.

Texture2D<float4> cascade0Tex : register(t0);
RWTexture2D<float4> outputTex : register(u0);

ConstantBuffer<RCGlobals> rcGlobals : register(b0);

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint2 pixelPos = DTid.xy;
    int2 outputDims = GetDims(outputTex);
    
    if (!OUT_OF_BOUNDS(pixelPos, outputDims))
    {
        int2 probePos = pixelPos;
        float3 radiance = 0.0f;

        uint rayCount = rcGlobals.rayCount0;
        // If the merge has been pre-averaged it means that each original ray has 
        // casted a ray equal to its ray scaling factor. 
        if(rcGlobals.usePreAveraging)
        {
            rayCount /= rcGlobals.rayScalingFactor; 
        }
        
        uint rayCount0Sqrt = sqrt(rayCount);
        float4 summedRadiance = 0.0f;        
        for (int i = 0; i < rayCount0Sqrt; i++)
        {
            for (int k = 0; k < rayCount0Sqrt; k++)
            {
                int2 rayOffset = rcGlobals.probeDim0 * int2(i, k);
                int2 samplePoint = probePos + rayOffset;
                
                summedRadiance += cascade0Tex[samplePoint];
            }
        }

        radiance = summedRadiance.rgb / rayCount; 
        outputTex[pixelPos] = float4(radiance, 1.0f);
    }
}