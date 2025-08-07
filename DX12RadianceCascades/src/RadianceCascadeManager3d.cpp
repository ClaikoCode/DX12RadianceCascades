#include "rcpch.h"
#include "Core\ColorBuffer.h"
#include "Core\GraphicsCore.h"
#include "Core\CommandListManager.h"
#include "Core\CommandContext.h"
#include "GPUStructs.h"
#include "RadianceCascadeManager3D.h"

constexpr DXGI_FORMAT DefaultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

RadianceCascadeManager3D::RadianceCascadeManager3D(float rayLength0, bool usePreAverage, bool useDepthAwareMerging)
	: m_rayLength0(rayLength0), 
	m_raysPerProbe0(0), 
	m_probesPerDim0(0), 
	m_preAveragedIntervals(usePreAverage), 
	m_depthAwareMerging(useDepthAwareMerging),
	m_probeSpacing0(0),
	m_probeCount0X(0),
	m_probeCount0Y(0)
{
	// Empty
}

void RadianceCascadeManager3D::Generate(uint32_t raysPerProbe0, uint32_t probeSpacing0, uint32_t swapchainWidth, uint32_t swapchainHeight)
{
	// Make sure no work is on the GPU before generating.
	Graphics::g_CommandManager.IdleGPU();

	const uint32_t probeCount0X = uint32_t(Math::Ceiling(swapchainWidth / (float)probeSpacing0));
	const uint32_t probeCount0Y = uint32_t(Math::Ceiling(swapchainHeight / (float)probeSpacing0));

	const uint32_t minCount = probeCount0Y < probeCount0X ? probeCount0Y : probeCount0X;
	const uint32_t maxCascadeLevels = uint32_t(Math::Floor(Math::LogAB((float)m_scalingFactor.probeScalingFactor, (float)minCount)));
	m_cascadeIntervals.resize(maxCascadeLevels);

	// If we are using pre-averaged intervals it can be seen as collapsing all rays of an upper cascade into a single ray in the lower cascade.
	// What this effectivly means that if we have 4 as a ray scaling factor, every 4 texels that would store those rays in a higher cascade is now 1 texel instead.
	// We can just bake in this factor from the beginning, which will yield the same result.
	if (m_preAveragedIntervals)
	{
		raysPerProbe0 /= m_scalingFactor.rayScalingFactor;
	}

	const uint32_t probeBufferWidth = raysPerProbe0 * probeCount0X;
	const uint32_t probeBufferHeight = raysPerProbe0 * probeCount0Y;
	for (uint32_t i = 0; i < maxCascadeLevels; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);

		m_cascadeIntervals[i].Create(cascadeName, probeBufferWidth, probeBufferHeight, 1, DefaultFormat);
	}

	// Coalesced result has one pixel per probe0.
	m_coalescedResult.Create(
		L"Coalesced Result",
		probeCount0X,
		probeCount0Y,
		1,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	// Each depth level corresponds to a cascade interval. Each probe samples a pixel (no interpolation) and is placed at that depth.
	// Probes outside the dims are clamped.
	m_depthMips.Create(
		L"Depth mips",
		swapchainWidth,
		swapchainHeight,
		0, // Mips will be generated later.
		DXGI_FORMAT_R32_FLOAT
	);
	
	m_probeSpacing0 = probeSpacing0;
	m_probeCount0X = probeCount0X;
	m_probeCount0Y = probeCount0Y;
}

void RadianceCascadeManager3D::Init(float rayLength0, uint32_t raysPerProbe0, uint32_t probesPerDim0, uint32_t cascadeIntervalCount, bool usePreAverage, bool useDepthAwareMerging)
{
	Graphics::g_CommandManager.IdleGPU();

	float log4Rays = Math::LogAB(4.0f, (float)raysPerProbe0);
	ASSERT(log4Rays == Math::Floor(log4Rays) && raysPerProbe0 > 8u);

	const uint32_t probeCountPerDim0 = probesPerDim0;
	const uint32_t cascadeCount = cascadeIntervalCount;
	m_cascadeIntervals.resize(cascadeCount);

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

		m_cascadeIntervals[i].Create(cascadeName, textureSideLength, textureSideLength, 1, DefaultFormat);

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

	rcGlobalInfo.probeCount0X = m_probeCount0X;
	rcGlobalInfo.probeCount0Y = m_probeCount0Y;
	rcGlobalInfo.probeSpacing0 = m_probeSpacing0;
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

	gfxContext.TransitionResource(m_depthMips, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.ClearColor(m_depthMips);
}

uint32_t RadianceCascadeManager3D::GetRaysPerProbe(uint32_t cascadeIndex)
{
	return m_raysPerProbe0 * (uint32_t)Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCountPerDim(uint32_t cascadeIndex)
{
	return m_probesPerDim0 / (uint32_t)Math::Pow((float)m_scalingFactor.probeScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCountOld(uint32_t cascadeIndex)
{
	uint32_t probesPerDim = GetProbeCountPerDim(cascadeIndex);
	return probesPerDim * probesPerDim;
}

uint32_t RadianceCascadeManager3D::GetProbeCount(uint32_t cascadeIndex)
{
	ProbeDims probeDims = GetProbeDims(cascadeIndex);
	return probeDims.probesX * probeDims.probesY;
}

ProbeDims RadianceCascadeManager3D::GetProbeDims(uint32_t cascadeIndex)
{
	const uint32_t dividend = uint32_t(Math::Pow((float)m_scalingFactor.probeScalingFactor, (float)cascadeIndex));

	ProbeDims probeDims;
	probeDims.probesX = m_probesPerDim0 / dividend;
	probeDims.probesY = m_probesPerDim0 / dividend;

	return probeDims;
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
