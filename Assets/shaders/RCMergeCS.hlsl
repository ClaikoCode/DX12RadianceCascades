#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> target : register(u0);
Texture2D<float4> sceneColor : register(t0);

SamplerState sceneSampler : register(s0);

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    float2 relative = float2(pixelPos) / CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);

    if (!OUT_OF_BOUNDS_RELATIVE(relative))
    {
        ProbeInfo probeInfo = BuildProbeInfo(pixelPos, cascadeInfo.cascadeIndex);

    }
}