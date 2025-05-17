#include "rcpch.h"
#include "Core\ColorBuffer.h"
#include "Core\GraphicsCore.h"
#include "Core\CommandListManager.h"
#include "GPUStructs.h"
#include "RadianceCascadeManager3D.h"

void RadianceCascadeManager3D::Init(float rayLength0, uint32_t raysPerProbe0)
{
	Graphics::g_CommandManager.IdleGPU();

	float log4Rays = Math::LogAB(4, raysPerProbe0);
	ASSERT(log4Rays == Math::Floor(log4Rays) && raysPerProbe0 > 8u);

	const uint32_t cascadeCount = 6;
	const uint32_t probeCountPerDim0 = 128;
	
	if (m_cascadeIntervals.size() < cascadeCount)
	{
		m_cascadeIntervals.resize(cascadeCount);
	}

	uint32_t actualCascadeCount = cascadeCount;
	uint32_t probeCount = probeCountPerDim0 * probeCountPerDim0;
	uint32_t raysPerProbe = raysPerProbe0;
	for (uint32_t i = 0; i < cascadeCount; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);

		uint32_t textureSideLength = (uint32_t)Math::Sqrt((float)(probeCount * raysPerProbe));
		m_cascadeIntervals[i].Create(cascadeName, textureSideLength, textureSideLength, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);

		// If we have reached the maximum depth that we can subdivide probes, we stop and save over the new value.
		if (probeCount == 1)
		{
			actualCascadeCount = i + 1;
			break;
		}

		probeCount /= m_scalingFactor.probeScalingFactor * m_scalingFactor.probeScalingFactor;
		raysPerProbe *= m_scalingFactor.rayScalingFactor;
	}

	if (actualCascadeCount != cascadeCount)
	{
		m_cascadeIntervals.resize(actualCascadeCount);
	}

	m_rayLength0 = rayLength0;
	m_raysPerProbe0 = raysPerProbe0;
	m_probesPerDim0 = probeCountPerDim0;
}

void RadianceCascadeManager3D::FillRCGlobalInfo(RCGlobalInfo& rcGlobalInfo)
{
	rcGlobalInfo.probeDim0 = m_probesPerDim0;
	rcGlobalInfo.rayCount0 = m_raysPerProbe0;
	rcGlobalInfo.rayLength0 = m_rayLength0;

	rcGlobalInfo.probeScalingFactor = m_scalingFactor.probeScalingFactor;
	rcGlobalInfo.rayScalingFactor = m_scalingFactor.rayScalingFactor;
	
	rcGlobalInfo.cascadeCount = GetCascadeIntervalCount();
}

uint32_t RadianceCascadeManager3D::GetRaysPerProbe(uint32_t cascadeIndex)
{
	return m_raysPerProbe0 * (uint32_t)Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCount(uint32_t cascadeIndex)
{
	uint32_t probesPerDim = m_probesPerDim0 / (uint32_t)Math::Pow((float)m_scalingFactor.probeScalingFactor, (float)cascadeIndex);
	return probesPerDim * probesPerDim;
}

float RadianceCascadeManager3D::GetStartT(uint32_t cascadeIndex)
{
	float startT = Math::GeometricSeriesSum(m_rayLength0, (float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
	return startT == -0.0f ? -startT : startT;
}

float RadianceCascadeManager3D::GetRayLength(uint32_t cascadeIndex)
{
	return m_rayLength0 * Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}
