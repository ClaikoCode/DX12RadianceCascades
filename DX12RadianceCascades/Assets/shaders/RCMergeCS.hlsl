#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> cascadeN : register(u0);
Texture2D<float4> cascadeN1 : register(t0);

SamplerState mergeSampler : register(s0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint cascadeIndex = cascadeInfo.cascadeIndex;
    uint2 pixelPos = DTid.xy;
    float2 relative = float2(pixelPos) / CASCADE_SIDE_LENGTH(cascadeIndex);

    if (!OUT_OF_BOUNDS_RELATIVE(relative))
    {
        ProbeInfo probeInfo = BuildProbeInfo(pixelPos, cascadeIndex);
        ProbeInfo probeInfoN1 = BuildProbeInfo(pixelPos, cascadeIndex + 1);

        float4 radiance = cascadeN[pixelPos];
        
        if (radiance.a != TRANSPARENT)
        {
            float4 TL = 0.0f;
        }
        
        cascadeN[pixelPos] = float4(radiance.rgb, 1.0f);
    }
}