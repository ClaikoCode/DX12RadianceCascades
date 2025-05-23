#ifndef DEBUGDRAW_H
#define DEBUGDRAW_H

// THIS FILE WAS ADAPTED FROM: 
// https://github.com/krupitskas/Yasno/blob/0e14e793807aa0115543a572ad95485b86ac6647/shaders/include/debug_renderer.hlsl 

#include "Common.hlsli"

#if defined(_DEBUGDRAWING)

struct DebugRenderVertex
{
    float3 position;
    float3 color;
};

RWStructuredBuffer<DebugRenderVertex> debugDrawVertexData : register(u126);
RWByteAddressBuffer debugDrawVertexCounter : register(u127);

void DrawLine(float3 position0, float3 position1, float3 color)
{
    const uint MAX_VERTICES = 2048 * 2048 * 2;
    
    // Simple atomic add with bounds check - no retry logic
    uint offset_in_vertex_buffer;
    debugDrawVertexCounter.InterlockedAdd(0, 1, offset_in_vertex_buffer);
    
    // Convert to vertex index (each line needs 2 vertices)
    offset_in_vertex_buffer = offset_in_vertex_buffer * 2;
    
    // Check if we're still within bounds
    if (offset_in_vertex_buffer >= MAX_VERTICES)
    {
        // Buffer is full, discard this line
        return;
    }
    
    // Draw the line
    DebugRenderVertex v;
    v.position = position0;
    v.color = float3(1.0f, 1.0f, 1.0f);
    debugDrawVertexData[offset_in_vertex_buffer + 0] = v;
    
    v.position = position1;
    v.color = color;
    debugDrawVertexData[offset_in_vertex_buffer + 1] = v;
}

void DrawAxisAlignedBox(float3 center, float3 extent, float3 color)
{
	// Calculate the 8 corners of the AABB
    float3 corners[8];

    corners[0] = center + float3(-extent.x, -extent.y, -extent.z); // min corner
    corners[1] = center + float3(extent.x, -extent.y, -extent.z);
    corners[2] = center + float3(-extent.x, extent.y, -extent.z);
    corners[3] = center + float3(extent.x, extent.y, -extent.z);
    corners[4] = center + float3(-extent.x, -extent.y, extent.z);
    corners[5] = center + float3(extent.x, -extent.y, extent.z);
    corners[6] = center + float3(-extent.x, extent.y, extent.z);
    corners[7] = center + float3(extent.x, extent.y, extent.z);

    // Draw edges of the AABB
    // Bottom face
    DrawLine(corners[0], corners[1], color);
    DrawLine(corners[1], corners[3], color);
    DrawLine(corners[3], corners[2], color);
    DrawLine(corners[2], corners[0], color);

    // Top face
    DrawLine(corners[4], corners[5], color);
    DrawLine(corners[5], corners[7], color);
    DrawLine(corners[7], corners[6], color);
    DrawLine(corners[6], corners[4], color);

    // Vertical edges
    DrawLine(corners[0], corners[4], color);
    DrawLine(corners[1], corners[5], color);
    DrawLine(corners[2], corners[6], color);
    DrawLine(corners[3], corners[7], color);
}

void DrawSphere(float3 center, float radius, float3 color)
{
    const int slices = 8; // Number of slices along latitude
    const int stacks = 8; // Number of stacks along longitude

    for (int i = 0; i < stacks; ++i)
    {
        float theta1 = (i / (float) stacks) * 3.14159265; // Current stack angle
        float theta2 = ((i + 1) / (float) stacks) * 3.14159265; // Next stack angle

        for (int j = 0; j < slices; ++j)
        {
            float phi1 = (j / (float) slices) * 2.0 * 3.14159265; // Current slice angle
            float phi2 = ((j + 1) / (float) slices) * 2.0 * 3.14159265; // Next slice angle

            // Calculate vertices of the current quad
            float3 p1 = center + radius * float3(sin(theta1) * cos(phi1), cos(theta1), sin(theta1) * sin(phi1));
            float3 p2 = center + radius * float3(sin(theta2) * cos(phi1), cos(theta2), sin(theta2) * sin(phi1));
            float3 p3 = center + radius * float3(sin(theta2) * cos(phi2), cos(theta2), sin(theta2) * sin(phi2));
            float3 p4 = center + radius * float3(sin(theta1) * cos(phi2), cos(theta1), sin(theta1) * sin(phi2));

            // Draw lines for the current quad
            DrawLine(p1, p2, color);
            DrawLine(p2, p3, color);
            DrawLine(p3, p4, color);
            DrawLine(p4, p1, color);
        }
    }
}
#else

void DrawLine(float3 position0, float3 position1, float3 color) {}
void DrawAxisAlignedBox(float3 center, float3 extent, float3 color) {}
void DrawSphere(float3 center, float radius, float3 color) {}

#endif // defined(_DEBUGDRAWING)

#endif // DEBUGDRAW_H