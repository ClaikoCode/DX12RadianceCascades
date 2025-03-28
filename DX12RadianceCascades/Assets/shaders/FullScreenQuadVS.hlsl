struct Interpolators
{
    float4 position : SV_Position;
    float2 texcoord : UV;
};

// Outputs a full screen quad with tex-coords
// Assumes a topology of TRIANGLESTRIP
// Assumes that a regular draw call is made with 4 as vertex count
Interpolators main(uint VertexID : SV_VertexID)
{
    Interpolators Out;

    Out.texcoord = float2((VertexID << 1) & 2, VertexID & 2);
    Out.position = float4(Out.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    return Out;
}