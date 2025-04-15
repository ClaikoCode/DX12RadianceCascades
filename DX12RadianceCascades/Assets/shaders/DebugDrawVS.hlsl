struct VertexData
{
    float3 pos : POSITION;
    float3 color : COLOR;
};

struct Interpolators
{
    float4 pos : SV_Position;
    float3 color : COLOR;
};

struct CameraBuffer
{
    matrix viewProjMatrix;
};

ConstantBuffer<CameraBuffer> cameraBuffer : register(b0);

Interpolators main(VertexData v)
{
    Interpolators i;
    
    i.pos = mul(float4(v.pos, 1.0f), cameraBuffer.viewProjMatrix);
    i.color = v.color;
    
    return i;
}