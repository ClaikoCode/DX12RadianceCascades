struct Interpolators
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

Texture2D sourceTex : register(t0);
SamplerState defaultSampler : register(s0);

float4 main(Interpolators i) : SV_Target0
{
    return float4(sourceTex.Sample(defaultSampler, i.texcoord).rgb, 1.0f);
}