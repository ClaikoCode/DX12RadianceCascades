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
};

__declspec(align(16)) struct RTParams
{
	uint32_t dispatchWidth;
	uint32_t dispatchHeight;
	uint32_t rayFlags;
	float holeSize;
};