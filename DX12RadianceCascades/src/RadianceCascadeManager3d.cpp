#include "rcpch.h"
#include "Core\ColorBuffer.h"
#include "Core\DepthBuffer.h"
#include "Core\GraphicsCore.h"
#include "Core\CommandListManager.h"
#include "Core\CommandContext.h"
#include "GPUStructs.h"
#include "RadianceCascadeManager3D.h"
#include "RuntimeResourceManager.h"

constexpr DXGI_FORMAT DefaultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;


RadianceCascadeManager3D::RadianceCascadeManager3D(float rayLength0, bool usePreAverage, bool useDepthAwareMerging)
	: m_rayLength0(rayLength0), 
	m_raysPerProbe0(0), 
	m_preAveragedIntervals(usePreAverage), 
	m_depthAwareMerging(useDepthAwareMerging),
	m_probeSpacing0(0),
	m_probeCount0X(0),
	m_probeCount0Y(0)
{
	// Empty
}

void RadianceCascadeManager3D::Generate(uint32_t raysPerProbe0, uint32_t probeSpacing0, uint32_t swapchainWidth, uint32_t swapchainHeight, uint32_t maxAllowedCascadeLevels /*= 8*/)
{
	// Make sure no work is on the GPU before generating.
	Graphics::g_CommandManager.IdleGPU();

	// Probe counts are rounded down to the nearest integer.
	// If they are rounded up, probes in higher cascades would be placed far outside the screen.
	// Probe indecies are clamped to the nearest edge either way but flooring this count makes it more manageable and there will 
	// only be slight stretching on screen edges that should not be very noticeable unless pointed out.
	const uint32_t probeCount0X = uint32_t(Math::Floor(swapchainWidth / (float)probeSpacing0));
	const uint32_t probeCount0Y = uint32_t(Math::Floor(swapchainHeight / (float)probeSpacing0));

	const uint32_t minCount = probeCount0Y < probeCount0X ? probeCount0Y : probeCount0X;
	uint32_t maxCalculatedCascadeLevels = uint32_t(Math::Floor(Math::LogAB((float)m_scalingFactor.probeScalingFactor, (float)minCount)));
	if (maxCalculatedCascadeLevels > maxAllowedCascadeLevels)
	{
		maxCalculatedCascadeLevels = maxAllowedCascadeLevels;
	}

	m_cascadeIntervals.resize(maxCalculatedCascadeLevels);

	uint32_t raysPerProbe = raysPerProbe0;
	ProbeDims probeDims = { probeCount0X, probeCount0Y };
	for (uint32_t i = 0; i < maxCalculatedCascadeLevels; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);

		// If pre averaged intervals are used, each original ray will average the rays that would be averaged into the lower cascades.
		// This means that each dispatched pixel, representing a probe and a direciton, will already account for the information of rays equal to rayScalingFactor.
		uint32_t raysPerProbeDim = m_preAveragedIntervals ? (uint32_t)sqrt(raysPerProbe / m_scalingFactor.rayScalingFactor) : (uint32_t)sqrt(raysPerProbe);
		
		const uint32_t probeBufferWidth = probeDims.probesX * raysPerProbeDim;
		const uint32_t probeBufferHeight = probeDims.probesY * raysPerProbeDim;

		m_cascadeIntervals[i].Create(cascadeName, probeBufferWidth, probeBufferHeight, 1, DefaultFormat);

		probeDims.probesX /= m_scalingFactor.probeScalingFactor;
		probeDims.probesY /= m_scalingFactor.probeScalingFactor;
		raysPerProbe *= m_scalingFactor.rayScalingFactor;
	}

	// Coalesced result has one pixel per probe0.
	m_coalescedResult.Create(
		L"Coalesced Result",
		probeCount0X,
		probeCount0Y,
		1,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	UpdateResourceDescriptors();

	m_probeSpacing0 = probeSpacing0;
	m_probeCount0X = probeCount0X;
	m_probeCount0Y = probeCount0Y;
	m_raysPerProbe0 = raysPerProbe0;
}

void RadianceCascadeManager3D::FillRCGlobalInfo(RCGlobals& rcGlobalInfo)
{
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
}

uint32_t RadianceCascadeManager3D::GetRaysPerProbe(uint32_t cascadeIndex)
{
	return m_raysPerProbe0 * (uint32_t)Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
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
	probeDims.probesX = m_probeCount0X / dividend;
	probeDims.probesY = m_probeCount0Y / dividend;

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

void RadianceCascadeManager3D::UpdateResourceDescriptors()
{
	for (size_t i = 0; i < m_cascadeIntervals.size(); i++)
	{
		RuntimeResourceManager::UpdateDescriptor(m_cascadeIntervals[i].GetSRV());
		RuntimeResourceManager::UpdateDescriptor(m_cascadeIntervals[i].GetUAV());
	}

	RuntimeResourceManager::UpdateDescriptor(m_coalescedResult.GetSRV());
	RuntimeResourceManager::UpdateDescriptor(m_coalescedResult.GetUAV());
}
