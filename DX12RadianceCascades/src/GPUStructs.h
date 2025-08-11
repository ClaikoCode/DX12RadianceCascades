#pragma once

#include "Utils.h"

__declspec(align(16)) struct DebugRenderCameraInfo
{
	Utils::GPUMatrix viewProjMatrix;
};

__declspec(align(16)) struct GlobalInfo
{
	Utils::GPUMatrix viewProjMatrix;
	Utils::GPUMatrix invViewProjMatrix;
	Math::Vector3 cameraPos;
	Utils::GPUMatrix invViewMatrix;
	Utils::GPUMatrix invProjMatrix;
};

__declspec(align(16)) struct RTParams
{
	uint32_t dispatchWidth;
	uint32_t dispatchHeight;
	uint32_t rayFlags;
	float holeSize;
};

__declspec(align(16)) struct SourceInfo
{
	BOOL isFirstDepth;
	uint32_t sourceWidth;
	uint32_t sourceHeight;
};

__declspec(align(16)) struct RCGlobals
{
	uint32_t probeScalingFactor; // Per dim.
	uint32_t rayScalingFactor;
	uint32_t rayCount0;
	float rayLength0;
	uint32_t cascadeCount;
	BOOL usePreAveraging;
	BOOL depthAwareMerging;
	uint32_t probeCount0X; 
	uint32_t probeCount0Y;
	uint32_t probeSpacing0; // Spacing between probes in pixels.
};

__declspec(align(16)) struct CascadeVisInfo
{
	BOOL enableProbeVis;
	uint32_t cascadeVisIndex;
	uint32_t probeSubset;
};