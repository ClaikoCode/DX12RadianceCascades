#include "Common.hlsli"

struct SceneInfo
{
    uint2 sceneDims;
};

ConstantBuffer<SceneInfo> sceneInfo : register(b0);
RWTexture2D<float4> scene : register(u0);

// All signed distance functions that are used are sourced from: https://iquilezles.org/articles/distfunctions2d/
float sdCircle(float2 p, float r)
{
    return length(p) - r;
}

void DrawCircle(uint2 pixelPos, int2 origin, float radius, float3 color, float curSignedDistance, float3 curColor)
{
    float2 relativePoint = float2(pixelPos) - float2(origin);
    float newSignedDistance = sdCircle(relativePoint, radius);
    
    float distToBeWritten = newSignedDistance < curSignedDistance ? newSignedDistance : curSignedDistance;
    float3 colorToBeWritten = newSignedDistance <= 0.0f ? color : curColor;
    
    //if (newSignedDistance > 0.0f)
    //{
    //    float maggedDist = newSignedDistance / 100.0f;
    //    maggedDist = frac(maggedDist);
    //    colorToBeWritten = float3(maggedDist, maggedDist, maggedDist);
    //}
    
    scene[pixelPos] = float4(colorToBeWritten, distToBeWritten);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    
    if (!OUT_OF_BOUNDS(pixelPos, sceneInfo.sceneDims))
    {
        float4 currentSceneValue = scene[pixelPos];
        float3 currentColorValue = currentSceneValue.rgb;
        float currentSignedDistance = currentSceneValue.a;
        
        float3 circleColor = float3(1.0f, 0.0f, 0.0f);
        float circleRadius = 100.0f;
        int2 circleOrigin = sceneInfo.sceneDims / 2.0f;
        DrawCircle(pixelPos, circleOrigin, circleRadius, circleColor, currentSignedDistance, currentColorValue);
    }
}