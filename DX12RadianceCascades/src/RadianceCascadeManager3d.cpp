#include "rcpch.h"
#include "Core\ColorBuffer.h"
#include "Core\GraphicsCore.h"
#include "Core\CommandListManager.h"
#include "Core\CommandContext.h"
#include "GPUStructs.h"
#include "RadianceCascadeManager3D.h"

void RadianceCascadeManager3D::Init(float rayLength0, uint32_t raysPerProbe0, uint32_t probesPerDim0, uint32_t cascadeIntervalCount, bool usePreAverage, bool useDepthAwareMerging)
{
	Graphics::g_CommandManager.IdleGPU();

	float log4Rays = Math::LogAB(4.0f, (float)raysPerProbe0);
	ASSERT(log4Rays == Math::Floor(log4Rays) && raysPerProbe0 > 8u);

	const uint32_t cascadeCount = cascadeIntervalCount;
	const uint32_t probeCountPerDim0 = probesPerDim0;
	
	if (m_cascadeIntervals.size() < cascadeCount)
	{
		m_cascadeIntervals.resize(cascadeCount);
	}

	uint32_t actualCascadeCount = cascadeCount;
	uint32_t probeCount = probeCountPerDim0 * probeCountPerDim0;
	uint32_t raysPerProbe = raysPerProbe0;
	
	// If we are using pre-averaged intervals it can be seen as collapsing all rays of an upper cascade into a single ray in the lower cascade.
	// What this effectivly means that if we have 4 as a ray scaling factor, every 4 texels that would store those rays in a higher cascade is now 1 texel instead.
	// We can just bake in this factor from the beginning, which will yield the same result.
	if (usePreAverage)
	{
		raysPerProbe /= m_scalingFactor.rayScalingFactor;
	}

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

	m_coalescedResult.Create(
		L"Coalesced Result",
		probeCountPerDim0,
		probeCountPerDim0,
		1,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_rayLength0 = rayLength0;
	m_raysPerProbe0 = raysPerProbe0;
	m_probesPerDim0 = probeCountPerDim0;
	m_preAveragedIntervals = usePreAverage;
	m_depthAwareMerging = useDepthAwareMerging;
}

void RadianceCascadeManager3D::FillRCGlobalInfo(RCGlobals& rcGlobalInfo)
{
	rcGlobalInfo.probeDim0 = m_probesPerDim0;
	rcGlobalInfo.rayCount0 = m_raysPerProbe0;
	rcGlobalInfo.rayLength0 = m_rayLength0;

	rcGlobalInfo.probeScalingFactor = m_scalingFactor.probeScalingFactor;
	rcGlobalInfo.rayScalingFactor = m_scalingFactor.rayScalingFactor;
	
	rcGlobalInfo.cascadeCount = GetCascadeIntervalCount();

	rcGlobalInfo.usePreAveraging = m_preAveragedIntervals;
	rcGlobalInfo.depthAwareMerging = m_depthAwareMerging;
}

void RadianceCascadeManager3D::ClearBuffers(GraphicsContext& gfxContext)
{
	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		gfxContext.TransitionResource(cascadeInterval, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.ClearColor(cascadeInterval);
	}

	gfxContext.TransitionResource(m_coalescedResult, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.ClearColor(m_coalescedResult);
}

uint32_t RadianceCascadeManager3D::GetRaysPerProbe(uint32_t cascadeIndex)
{
	return m_raysPerProbe0 * (uint32_t)Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCountPerDim(uint32_t cascadeIndex)
{
	return m_probesPerDim0 / (uint32_t)Math::Pow((float)m_scalingFactor.probeScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCount(uint32_t cascadeIndex)
{
	uint32_t probesPerDim = GetProbeCountPerDim(cascadeIndex);
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
