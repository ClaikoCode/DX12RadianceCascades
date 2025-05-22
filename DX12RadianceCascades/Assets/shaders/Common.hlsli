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

#define IsZero(x) (length(x) < EPSILON)

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

float3 SimpleSunsetSky(float3 viewDir, float3 sunDir)
{
    // Normalize inputs
    viewDir = normalize(viewDir);
    sunDir = normalize(sunDir);
    
    // Calculate height factor (up direction)
    float height = viewDir.y * 0.5 + 0.5;
    
    // Create sky gradient
    float3 skyBaseColor = lerp(
        float3(0.8, 0.4, 0.2) * 3.0, // Warm orange at horizon
        float3(0.1, 0.2, 0.4) * 1.5, // Deep blue at zenith
        pow(height, 0.5)
    );
    
    // Sun calculation
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    float sunDisc = smoothstep(0.96, 0.9997, sunDot);
    float sunGlow = pow(sunDot, 8.0);
    
    // Add sun and glow
    float3 sunColor = float3(1.0, 0.6, 0.3) * 20.0; // HDR sun
    skyBaseColor += sunDisc * sunColor;
    skyBaseColor += sunGlow * float3(0.8, 0.5, 0.3) * (1.0 - height) * 0.8;
    
    return skyBaseColor;
}

int2 GetDims(RWTexture2D<float4> tex)
{
    uint texWidth;
    uint texHeight;
    tex.GetDimensions(texWidth, texHeight);
    
    return int2(texWidth, texHeight);
}

int2 GetDims(Texture2D tex)
{
    uint texWidth;
    uint texHeight;
    tex.GetDimensions(texWidth, texHeight);
    
    return int2(texWidth, texHeight);
}

int2 Translate1DTo2D(int coord1D, int2 dims)
{
    return int2(coord1D % dims.x, coord1D / dims.x);
}


// Order: (0, 0), (1, 0), (0, 1), (1, 1)
int2 TranslateCoord4x1To2x2(int coord4x1)
{    
    return Translate1DTo2D(coord4x1, int2(2, 2));
}


#endif // COMMON_H