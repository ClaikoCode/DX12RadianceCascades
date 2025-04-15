struct Interpolators
{
    float4 pos : SV_Position;
    float3 color : COLOR;
};

float4 main(Interpolators i) : SV_TARGET
{
    return float4(i.color, 1.0f);
}