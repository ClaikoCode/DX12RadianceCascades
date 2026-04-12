#pragma once

#include "Core\CommandListManager.h"
#include "Core\CommandContext.h"
#include "Core\ColorBuffer.h"
#include "Core\ReadbackBuffer.h"

struct RCGlobals;

struct ProbeDims
{
	uint32_t probesX;
	uint32_t probesY;
};

struct RC3DSettings
{
	bool useGatherFiltering = true;
	bool useDepthAwareMerging = false;
	
	float rayLength0 = 5.0f;
	
	// These parameters require a rebuild if they are changed.
	struct StaticParameters
	{
		int probeSpacing0 = 2;
		int raysPerProbe0 = 16;
		int maxCascadeCount = 8;

		// Signifies that the cascade textures will be 1 / rayscalingfactor of the original size.
		bool isUsingPreAveragedIntervals = true;
	} staticParams;
};

class RadianceCascadeManager3D
{
public:
	RadianceCascadeManager3D() = default;

	void Generate(uint32_t raysPerProbe0, uint32_t probeSpacing0, uint32_t swapchainWidth, uint32_t swapchainHeight, uint32_t maxAllowedCascadeLevels = 8u);
	// Calls Generate() using internal values with other width and height parameters.
	void Resize(uint32_t width, uint32_t height);

	void FillRCGlobalInfo(RCGlobals& rcGlobalInfo);

	void ClearBuffers(GraphicsContext& gfxContext);
	uint32_t GetRaysPerProbe(uint32_t cascadeIndex);
	uint32_t GetProbeCount(uint32_t cascadeIndex);
	ProbeDims GetProbeDims(uint32_t cascadeIndex);
	uint32_t GetTotalRays(uint32_t cascadeIndex);
	uint32_t GetProbeSpacing() { return m_rcSettings.staticParams.probeSpacing0; }

	float GetStartT(uint32_t cascadeIndex);
	float GetRayLength(uint32_t cascadeIndex);

	uint32_t GetCascadeIntervalCount() { return (uint32_t)m_cascadeIntervals.size(); }
	// Always one less than cascade interval count.
	uint32_t GetGatherFilterCount() { return (uint32_t)m_cascadeGatherFilters.size(); }
	ColorBuffer& GetCascadeIntervalBuffer(uint32_t cascadeIndex);
	ColorBuffer& GetCascadeGatherFilterBuffer(uint32_t filterIndex);
	ByteAddressBuffer& GetGatherFilterByteAddressBuffer() { return m_gatherFilterByteAddressBuffer; }
	ReadbackBuffer& GetGatherFilterReadbackBuffer() { return m_gatherFilterReadbackBuffer; }
	uint32_t GetProbeScalingFactor() const { return m_scalingFactor.probeScalingFactor; }
	uint32_t GetRayScalingFactor() const { return m_scalingFactor.rayScalingFactor; }
	bool UsesPreAveragedIntervals() const { return m_rcSettings.staticParams.isUsingPreAveragedIntervals; }
	bool UsesGatherFiltering() const { return m_rcSettings.useGatherFiltering; }

	void SetGatherFiltering(bool useGatherFiltering) { m_rcSettings.useGatherFiltering = useGatherFiltering; }

	ColorBuffer& GetCoalesceBuffer() { return m_coalescedResult; }

	uint64_t GetTotalVRAMUsage();

	// Will return the amount of rays that were filtered by a specific gather filter.
	uint32_t GetFilteredRayCount(uint32_t filterIndex);

	void DrawRCSettingsUI();

private:

	// Will update resource managers descriptors of RC resources.
	void UpdateResourceDescriptors();

private:
	struct ScalingFactor
	{
		uint32_t probeScalingFactor = 2u;
		uint32_t rayScalingFactor = 4u; // This needs to be a perfect square.
	} m_scalingFactor;

	std::vector<ColorBuffer> m_cascadeIntervals;
	// Will have same dimensions for color buffers for all but cascade level 0.
	std::vector<ColorBuffer> m_cascadeGatherFilters;
	ColorBuffer m_coalescedResult;

	// TODO: Move this to rcmanager3d as these resources heavily depend on its state. They should be re-generated if rc is re-generated.
	ByteAddressBuffer m_gatherFilterByteAddressBuffer;
	ReadbackBuffer m_gatherFilterReadbackBuffer;
	uint32_t* m_gatherFilterReadbackBufferMappedPtr = nullptr;

	uint32_t m_probeCount0X = 0u;
	uint32_t m_probeCount0Y = 0u;

	// Saved width and height since last Generate() call.
	uint32_t m_swapchainWidth;
	uint32_t m_swapchainHeight;

	RC3DSettings m_rcSettings;
};