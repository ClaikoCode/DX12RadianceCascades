#include "Common.hlsli"

struct SourceInfo
{
    bool isFirstDepth;
    uint2 dims;
};

ConstantBuffer<SourceInfo> sourceInfo : register(b0);

RWTexture2D<float4> sourceDepth : register(u0);
RWTexture2D<float2> targetDepth : register(u1);


[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 targetPixelIndex = DTid.xy;
    
    if (sourceInfo.isFirstDepth)
    {
        if(!OUT_OF_BOUNDS(targetPixelIndex, sourceInfo.dims))
        {
            float sourceDepthVal = sourceDepth.Load(int3(targetPixelIndex, 0)).r;
            
            // Start with min and max equal.
            targetDepth[targetPixelIndex] = float2(sourceDepthVal, sourceDepthVal);
        }
    }
    else
    {
        uint2 sourcePixelIndex = targetPixelIndex * 2; // Each thread looks at a 2x2 area.
        
        if (!OUT_OF_BOUNDS(sourcePixelIndex, sourceInfo.dims))
        {
            float min = FLT_MAX;
            float max = 0.0f;
            
            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 2; k++)
                {
                    int2 pixelOffset = int2(i, k);
                
                    int2 sourceSamplingPixel = int2(sourcePixelIndex) + pixelOffset;
                    float2 sourceMinMax = sourceDepth.Load(int3(sourceSamplingPixel, 0)).rg;

                     // Red channel is min.
                    if (sourceMinMax.r < min)
                    {
                        min = sourceMinMax.r;
                    }
                
                    // Green channel is max.
                    if (sourceMinMax.g > max)
                    {
                        max = sourceMinMax.g;
                    }
                }
            }
        
            targetDepth[targetPixelIndex] = float2(min, max);
        }
    }
    
    
}