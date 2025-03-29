#include "rcpch.h"
#include "Core\ColorBuffer.h"
#include "Core\CommandContext.h"
#include "RadianceCascadesManager.h"

RadianceCascadesManager::~RadianceCascadesManager()
{
	Shutdown();
}

void RadianceCascadesManager::Init(float _rayLength0, float _maxRayLength)
{
	const uint16_t rayScalingFactor = scalingFactor.rayScalingFactor;
	const uint16_t probeScalingFactor = scalingFactor.probeScalingFactor;
	const float rayScalingFactorFloat = (float)rayScalingFactor;
	const float probeScalingFactorFloat = (float)probeScalingFactor;

	// This is calculated by solving for factorCount in the following equation: rayLength0 * scalingFactor^(factorCount) = maxLength;
	// It calculates how many times the factor has to be applied to an initial ray length until the max length has been reached.
	float factorCount = Math::Ceiling(Math::LogAB(rayScalingFactorFloat, _maxRayLength / _rayLength0));
	float finalCascadeInvervalStart = Math::GeometricSeriesSum(_rayLength0, rayScalingFactorFloat, factorCount);
	uint32_t cascadeCount = (uint32_t)Math::Ceiling(Math::LogAB(rayScalingFactorFloat, finalCascadeInvervalStart)) - 1;

	const uint32_t probeCountPerDim0 = (uint32_t)Math::Pow(probeScalingFactorFloat, (float)cascadeCount);

	if (m_cascadeIntervals.size() < cascadeCount)
	{
		m_cascadeIntervals.resize((size_t)cascadeCount);
	}

	uint32_t probeCount = probeCountPerDim0 * probeCountPerDim0;
	uint32_t raysPerProbe = s_RaysPerProbe0;
	for (uint32_t i = 0; i < (uint32_t)cascadeCount; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);

		uint32_t probePixelLength = (uint32_t)Math::Sqrt((float)(probeCount * raysPerProbe));
		m_cascadeIntervals[i].Create(cascadeName, probePixelLength, probePixelLength, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);

		probeCount /= (uint32_t)Math::Pow(probeScalingFactorFloat, 2.0f);
		raysPerProbe *= rayScalingFactor;
	}

	// Set internal variables.
	rayLength0 = _rayLength0;
	probeDim0 = probeCountPerDim0;

	m_radianceField.Create(L"Radiance Field", probeDim0, probeDim0, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void RadianceCascadesManager::Shutdown()
{
	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		cascadeInterval.Destroy();
	}

	m_radianceField.Destroy();
}

RCGlobals RadianceCascadesManager::FillRCGlobalsData()
{
	RCGlobals rcGlobals = {};
	rcGlobals.probeDim0 = probeDim0;
	rcGlobals.rayCount0 = s_RaysPerProbe0;
	rcGlobals.rayLength0 = rayLength0;
	rcGlobals.probeSpacing0 = GetProbeSpacing(0);
	rcGlobals.probeScalingFactor = scalingFactor.probeScalingFactor;
	rcGlobals.rayScalingFactor = scalingFactor.rayScalingFactor;

	return rcGlobals;
}

uint32_t RadianceCascadesManager::GetProbePixelSize(uint32_t cascadeIndex)
{
	return GetCascadeInterval(cascadeIndex).GetWidth() / GetProbeCount(cascadeIndex);
}

uint32_t RadianceCascadesManager::GetProbeCount(uint32_t cascadeIndex)
{
	return (uint32_t)(probeDim0 / Math::Pow(scalingFactor.probeScalingFactor, (float)cascadeIndex));
}

float RadianceCascadesManager::GetProbeSpacing(uint32_t cascadeIndex)
{
	float probeSpacing0 = GetCascadeInterval(0).GetWidth() / (float)(GetProbeCount(0));
	return probeSpacing0 * Math::Pow(scalingFactor.probeScalingFactor, (float)cascadeIndex);
}

void RadianceCascadesManager::ClearBuffers(GraphicsContext& gfxContext)
{
	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		gfxContext.TransitionResource(cascadeInterval, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.ClearColor(cascadeInterval);
	}

	gfxContext.TransitionResource(m_radianceField, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.ClearColor(m_radianceField);
}
