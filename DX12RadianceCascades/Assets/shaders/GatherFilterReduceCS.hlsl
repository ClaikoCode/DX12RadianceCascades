#include "Common.hlsli"

// The purpose of this shader is to calculate how many texels are to be sampled by the merge step (value of 1). With this information,
// it is possible to calculate how many texels were NOT sampled (value of 0) by subtracting the sum from the total texel count.
// Because each texel corresponds to one CPU ray dispatch (4 GPU ray dispatches if pre-averaging is enabled), the result 
// gives the number of rays that were not processed during the gather step.

// While gather filters are set to have the same dimensions and specific memory layout that cascade intervals do,
// the aggregation of each texel value is calculated irrespective of this fact. In other words: 
// this shader will read across probe group borders but the sum will be the same.

// INTERESTING NOTE: 
// I tried finding what I thought was a bug for 2 hours because the NVIDIA Nsight histogram of pixel values gave a different answer
// on how many pixels were colored than this shader calculated. It turns out that NSIGHT is the one that reports inexact values.
// I downloaded the images (from NSIGHT itself) and put them into them into a PNG inspector online (https://onlinepngtools.com/find-png-color-count). 
// It gave the exact answer as this sum reduction shader!

struct FilterInfo
{
    uint width;
    uint height;
    uint filterIndex;
};

#define GROUP_SIZE_1D 16 // 16x16 = 256 threads per group
#define GROUP_SIZE (GROUP_SIZE_1D * GROUP_SIZE_1D)
#define TILE_SIZE_1D 4 // Each thread samples a 4x4 = 16 texel tile
#define WAVES_PER_GROUP (GROUP_SIZE / WaveGetLaneCount())

Texture2D<float> gatherFilter : register(t0);

ConstantBuffer<FilterInfo> filterInfoBuffer : register(b0);

RWByteAddressBuffer resultBuffer : register(u0);

// Reserves the max amount of registers to comply with worst case scenario.
// Although, 1kb of shared memory is well below the maximum allowed for most GPUs that support DX11+.
groupshared float gsGroupSharedMem[GROUP_SIZE];

[numthreads(GROUP_SIZE_1D, GROUP_SIZE_1D, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint GroupIndex : SV_GroupIndex)
{
    uint2 baseSamplePos = DTid.xy * TILE_SIZE_1D;
    
    float threadSum = 0.0f;
    
    [unroll]
    for (int i = 0; i < TILE_SIZE_1D; i++)
    {
        [unroll]
        for (int k = 0; k < TILE_SIZE_1D; k++)
        {
            uint2 samplePos = baseSamplePos + int2(i, k);
            if (!OUT_OF_BOUNDS(samplePos, uint2(filterInfoBuffer.width, filterInfoBuffer.height)))
            {
                threadSum += gatherFilter.Load(int3(samplePos, 0));
            }
        }
    }

    // Sum of the current wave.
    float waveSum = WaveActiveSum(threadSum);
    
    // Gets what wave a thread inside a group would land in.
    int waveIdx = GroupIndex / WaveGetLaneCount();
    
    // First thread of each wave writes to shared mem.
    if (WaveIsFirstLane())
    {
        gsGroupSharedMem[waveIdx] = waveSum;
    }
    
    // Wait for all threads in the group to write to shared memory.
    GroupMemoryBarrierWithGroupSync();
    
    // Will filter out all threads in the same wave that can index within the shared memory.
    if (GroupIndex < WAVES_PER_GROUP)
    {
        float groupVal = gsGroupSharedMem[GroupIndex];
        
        // Sums the previously calculated result from each wave into a single group sum.
        float groupSum = WaveActiveSum(groupVal);
        
        // Let the first thread in each group write to global memory. 
        if (GroupIndex == 0)
        {
            // Writes 4 bytes.
            resultBuffer.InterlockedAdd(filterInfoBuffer.filterIndex * 4, (uint)groupSum);
        }
    }
}