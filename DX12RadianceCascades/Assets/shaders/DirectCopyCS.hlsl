
#define OUT_OF_BOUNDS_RELATIVE(relPos) (relPos.x >= 1.0f || relPos.x < 0.0f || relPos.y >= 1.0f || relPos.x < 0.0f)
#define OUT_OF_BOUNDS(pos, bounds) (pos.x >= bounds.x || pos.x < 0 || pos.y >= bounds.y || pos.y < 0) 

#define IN_BOUNDS_RELATIVE(relPos) (!OUT_OF_BOUNDS_RELATIVE(relPos))
#define IN_BOUNDS(pos, bounds) (!OUT_OF_BOUNDS(pos, bounds))

struct DestInfo
{
    uint2 destResolution;
};

ConstantBuffer<DestInfo> destInfo : register(b0);
SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

Texture2D sourceTex : register(t0);
RWTexture2D<float4> destTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    
    if (IN_BOUNDS(pixelPos, destInfo.destResolution))
    {
        float2 relative = pixelPos / float2(destInfo.destResolution);
        float4 sampledColor = sourceTex.SampleLevel(pointSampler, relative, 0);
        destTex[pixelPos] = float4(sampledColor.rgb, 1.0f);
    }
}