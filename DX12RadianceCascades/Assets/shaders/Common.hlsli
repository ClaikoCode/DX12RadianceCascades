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

struct GlobalInfo
{
    matrix viewProjMatrix;
    matrix invViewProjMatrix;
    float3 cameraPos;
    matrix invViewMatrix;
    matrix invProjMatrix;
};

struct BilinearSampleInfo
{
    int2 basePixel;
    float2 ratios;
};

BilinearSampleInfo GetBilinearSampleInfo(float2 pixelPos)
{
    BilinearSampleInfo sampleInfo;
    
    sampleInfo.basePixel = floor(pixelPos);
    sampleInfo.ratios = frac(pixelPos);
    
    return sampleInfo;
}

// Weights taken from: https://en.wikipedia.org/wiki/Bilinear_interpolation#On_the_unit_square
float4 GetBilinearSampleWeights(float2 ratios)
{
    return float4
    (
        (1.0f - ratios.x) * (1.0f - ratios.y), // Top left.
        ratios.x * (1.0f - ratios.y), // Top right.
        (1.0f - ratios.x) * ratios.y, // Bottom left.
        ratios.x * ratios.y                     // Bottom right.
    );
}
// Depth val should be between 0 and 1 (NO LINEARIZATION REQUIRED)
float3 WorldPosFromDepth(float depthVal, float2 uv, matrix invProjMatrix, matrix invViewMatrix)
{
    if (IsZero(depthVal))
    {
        // Infinitely far away if furthest away.
        return float3(FLT_MAX, FLT_MAX, FLT_MAX);
    }
    
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
        float3(0.8, 0.4, 0.2) * 2.0, // Warm orange at horizon
        float3(0.1, 0.2, 0.4) * 1.0, // Deep blue at zenith
        pow(height, 0.5)
    );
    
    // Sun calculation
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    float sunDisc = smoothstep(0.96, 0.9997, sunDot);
    float sunGlow = pow(sunDot, 8.0);
    
    // Add sun and glow
    float3 sunColor = float3(1.0, 0.6, 0.3) * 50.0; // HDR sun
    skyBaseColor += sunDisc * sunColor;
    skyBaseColor += sunGlow * float3(0.8, 0.5, 0.3) * (1.0 - height) * 0.8;
    
    return skyBaseColor;
}

#define GetDims(tex, dimsOut) \
    { \
        uint texWidth; \
        uint texHeight; \
        tex.GetDimensions(texWidth, texHeight); \
        dimsOut.x = texWidth; \
        dimsOut.y = texHeight; \
    }


#define GetMipDims(tex, mipLevel, dimsOut) \
    { \
        uint texWidth; \
        uint texHeight; \
        uint numMips; \
        tex.GetDimensions(mipLevel, texWidth, texHeight, numMips); \
        dimsOut.x = texWidth; \
        dimsOut.y = texHeight; \
    }

int2 Translate1DTo2D(int coord1D, int2 dims)
{
    return int2(coord1D % dims.x, coord1D / dims.x);
}


// Order: (0, 0), (1, 0), (0, 1), (1, 1)
int2 TranslateCoord4x1To2x2(int coord4x1)
{    
    static int2 coordOffsets[4] = {
        int2(0, 0),
        int2(1, 0),
        int2(0, 1),
        int2(1, 1)
    };
    
    return coordOffsets[coord4x1];
}

float ProjectLinePerpendicular(float3 A, float3 B, float3 p)
{
    float3 BA = B - A;
    return dot(p - A, BA) / dot(BA, BA);
}

// This function is adapted from a shadertoy project by Alexander Sannikov on deptha aware upscaling: 
// https://www.shadertoy.com/view/4XXSWS
float2 GetBilinear3dRatioIter(float3 src_points[4], float3 dst_point, float2 init_ratio, const int it_count)
{
    float2 ratio = init_ratio;
    for (int i = 0; i < it_count; i++)
    {
        ratio.x = saturate(ProjectLinePerpendicular(lerp(src_points[0], src_points[2], ratio.y), lerp(src_points[1], src_points[3], ratio.y), dst_point));
        ratio.y = saturate(ProjectLinePerpendicular(lerp(src_points[0], src_points[1], ratio.x), lerp(src_points[2], src_points[3], ratio.x), dst_point));
    }
    
    return ratio;
}

float2 ClampPixelPos(float2 pixelPos, int2 dims)
{
    return clamp(pixelPos, int2(0, 0), dims);
}



#endif // COMMON_H