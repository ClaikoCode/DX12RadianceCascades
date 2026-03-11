#include "Common.hlsli"

// IMPORTANT NOTES WHICH THIS SHADER ASSUMES:
// A non-indexed drawcall has to be made with 36 as the VERTEX COUNT parameter.
// The primitive topology is set to TRIANGE LIST.
// BACKFACES are not culled by rasterizer.
// Depth compare function is set to EQUALS.

// Set to 1 if far plane has a depth value of 0 (inverted depth values).
#define DEPTH_IS_INVERTED 1

// Cube vertex positions
static const float3 cubeVertices[8] =
{
    float3(-0.5, -0.5, -0.5),   // 0
    float3(-0.5, -0.5, 0.5),    // 1
    float3(-0.5, 0.5, -0.5),    // 2
    float3(-0.5, 0.5, 0.5),     // 3
    float3(0.5, -0.5, -0.5),    // 4
    float3(0.5, -0.5, 0.5),     // 5
    float3(0.5, 0.5, -0.5),     // 6
    float3(0.5, 0.5, 0.5)       // 7
};

// Cube indices. Assumed to be in triangle list format.
static const uint cubeIndices[36] =
{
    // Front face
    1, 5, 7,
    7, 3, 1,

    // Back face
    4, 0, 2,
    2, 6, 4,

    // Left face
    0, 1, 3,
    3, 2, 0,

    // Right face
    5, 4, 6,
    6, 7, 5,

    // Top face
    2, 3, 7,
    7, 6, 2,

    // Bottom face
    0, 4, 5,
    5, 1, 0
};

struct Interpolators
{
    float3 samplingDirection : POSITION;
    float4 vertexPos : SV_POSITION;
};

ConstantBuffer<GlobalInfo> globalInfo : register(b0);

Interpolators main(uint vertexID : SV_VertexID)
{
    Interpolators i;
    
    // Get the cube vertex from input vertex ID.
    float3 cubeVertex = cubeVertices[cubeIndices[vertexID]];
    i.samplingDirection = cubeVertex;
    
    // Transform the vertex 
    float4 vertexPos = mul(float4(cubeVertex, 0.0f), globalInfo.viewProjMatrix);
    
    // During the perspective divide, x y and z are divided by the w component. 
    // If depth is inverted then:   (z / w) = (0 / w) = 0. 
    // If not then:                 (z / w) = (w / w) = 1.
    // This results in verticies being seen as infinitely far away (existing at the far plane).
    vertexPos.z = DEPTH_IS_INVERTED ? 0.0f : vertexPos.w;
    i.vertexPos = vertexPos;
    
	return i;
}