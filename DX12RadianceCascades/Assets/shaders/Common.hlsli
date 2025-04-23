#ifndef COMMON_H
#define COMMON_H

#define NO_SECOND_UV 1

#define EPSILON (1.0e-5)
#define MATH_PI (3.1415926535897932384f)
#define MATH_TAU (MATH_PI * 2.0f)
#define FLT_MAX (3.40282347e+38F)

#define OUT_OF_BOUNDS_RELATIVE(relPos) (relPos.x >= 1.0f || relPos.x < 0.0f || relPos.y >= 1.0f || relPos.x < 0.0f)
#define OUT_OF_BOUNDS(pos, bounds) (pos.x >= bounds.x || pos.x < 0 || pos.y >= bounds.y || pos.y < 0)

#define V2F16(v) ((v.y * float(0.0039215689)) + v.x)
#define F16V2(f) vec2(floor(f * 255.0) * float(0.0039215689), fract(f * 255.0))

#define InverseLerpClamped(a, b, val) (saturate((val - a) / (b - a)))
#define Remap(a, b, c, d, val) (lerp(c, d, InverseLerpClamped(a, b, val)))

// Depth val should be between 0 and 1 (NO LINEARIZATION REQUIRED)
float3 WorldPosFromDepth(float depthVal, float2 uv, matrix invProjMatrix, matrix invViewMatrix)
{
	// Convert the uv to clip space
    float4 clipSpacePos = float4(uv * 2.0f - 1.0f, depthVal, 1.0f);
    clipSpacePos.y = -clipSpacePos.y;
	
	// Convert the clip space to view space
    float4 viewSpacePos = mul(clipSpacePos, invProjMatrix);
    viewSpacePos /= viewSpacePos.w;
	
	// Convert the view space to world space
    float4 worldPosition = mul(viewSpacePos, invViewMatrix);
	
    return worldPosition.xyz;
}

#endif // COMMON_H