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
	bool isFirstDepth;
	uint32_t sourceWidth;
	uint32_t sourceHeight;
};

__declspec(align(16)) struct RCGlobalInfo
{
	uint32_t probeScalingFactor; // Per dim.
	uint32_t rayScalingFactor;
	uint32_t probeDim0;
	uint32_t rayCount0;
	float rayLength0;
	uint32_t cascadeCount;
};

__declspec(align(16)) struct CascadeVisInfo
{
	bool enableProbeVis;
	uint32_t cascadeVisIndex;
	uint32_t probeSubset;
};