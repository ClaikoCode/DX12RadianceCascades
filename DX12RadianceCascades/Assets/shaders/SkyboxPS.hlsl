
struct Interpolators
{
    float3 samplingDirection : POSITION;
    float4 vertexPos : SV_POSITION;
};

//TextureCube<float4> skyboxCubemap : register(t0);
//SamplerState cubemapSampler : register(s0);

float4 main(Interpolators i) : SV_TARGET
{
    return float4(i.samplingDirection, 1.0f);
}