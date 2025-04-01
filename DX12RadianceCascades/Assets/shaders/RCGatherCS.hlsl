#include "Common.hlsli"
#include "RCCommon.hlsli"

RWTexture2D<float4> target : register(u0);
Texture2D<float4> sceneColor : register(t0);

SamplerState sceneSampler : register(s0);

float4 RayMarch(float2 origin, float2 direction, float range, float texelSize)
{
    float distanceTraveled = 0.0f;
    float maxRange = range * texelSize;
    for (float i = 0.0f; i < range; i++)
    {
        float2 ray = origin + direction * min(distanceTraveled, maxRange);
        
        float4 sceneSample = sceneColor.SampleLevel(sceneSampler, ray, 0);
        float3 sceneColor = sceneSample.rgb;
        float sdfDistance = sceneSample.a;
        distanceTraveled += sdfDistance * texelSize;
        
        if (distanceTraveled >= maxRange || OUT_OF_BOUNDS_RELATIVE(ray))
        {
            break;
        }
        
        // If first sample is inside the object, return nothing.
        //if(sdfDistance <= EPSILON && distanceTraveled <= EPSILON)
        //{
        //    return 0.0f;
        //}
        
        // If we hit the object, return the color of the object.
        if (sdfDistance <= EPSILON)
        {
            return float4(sceneColor.rgb, 0.0f);
        }
    }
    
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixelPos = DTid.xy;
    float sideLen = CASCADE_SIDE_LENGTH(cascadeInfo.cascadeIndex);
    float2 relative = float2(pixelPos) / sideLen;

    if (!OUT_OF_BOUNDS_RELATIVE(relative))
    {
        uint2 sceneDims;
        sceneColor.GetDimensions(sceneDims.x, sceneDims.y);
        
        ProbeInfo probeInfo = BuildProbeInfo(pixelPos, cascadeInfo.cascadeIndex);
        {
            // Origin is correct and verified.
            float2 probeOrigin = (float2(probeInfo.probeIndex) + 0.5f) * probeInfo.probeSpacing * probeInfo.texelSize;
            
            // Direction is correct and verified
            float angle = MATH_TAU * (probeInfo.rayIndex + 0.5f) / probeInfo.rayCount;
            float2 direction = float2(cos(angle), -sin(angle));
            
            // This should be correct.
            float2 rayOrigin = probeOrigin + (direction * probeInfo.startDistance * probeInfo.texelSize);
            //int2 rayOriginPixel = rayOrigin * sideLen;
            //target[rayOriginPixel] = float4(1.0f, 0.0f, 0.0f, 1.0f);
            //return;

            float4 retColor = RayMarch(rayOrigin, direction, probeInfo.range, probeInfo.texelSize);
            target[pixelPos] = retColor;
        }
    }
}