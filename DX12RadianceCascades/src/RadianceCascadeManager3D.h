#pragma once

struct RCGlobals;

struct ProbeDims
{
	uint32_t probesX;
	uint32_t probesY;
};

class RadianceCascadeManager3D
{
public:
	RadianceCascadeManager3D() = default;
	RadianceCascadeManager3D(float rayLength0, bool usePreAverage);

	void Generate(uint32_t raysPerProbe0, uint32_t probeSpacing0, uint32_t swapchainWidth, uint32_t swapchainHeight, uint32_t maxAllowedCascadeLevels = 8u);

	void FillRCGlobalInfo(RCGlobals& rcGlobalInfo);

	void ClearBuffers(GraphicsContext& gfxContext);
	uint32_t GetRaysPerProbe(uint32_t cascadeIndex);
	uint32_t GetProbeCount(uint32_t cascadeIndex);
	ProbeDims GetProbeDims(uint32_t cascadeIndex);
	uint32_t GetProbeSpacing() { return m_probeSpacing0; }

	float GetStartT(uint32_t cascadeIndex);
	float GetRayLength(uint32_t cascadeIndex);

	uint32_t GetCascadeIntervalCount() { return (uint32_t)m_cascadeIntervals.size(); }
	ColorBuffer& GetCascadeIntervalBuffer(uint32_t cascadeIndex) { ASSERT(cascadeIndex < GetCascadeIntervalCount()); return m_cascadeIntervals[cascadeIndex]; }
	ColorBuffer& GetCascadeGatherFilterBuffer(uint32_t filterIndex) { ASSERT(filterIndex < m_cascadeGatherFilters.size()); return m_cascadeGatherFilters[filterIndex]; }
	uint32_t GetProbeScalingFactor() const { return m_scalingFactor.probeScalingFactor; }
	float GetRayLength() { return m_rayLength0; }
	void SetRayLength(float rayLength) { m_rayLength0 = rayLength; }
	bool UsesPreAveragedIntervals() const { return m_preAveragedIntervals; }

	ColorBuffer& GetCoalesceBuffer() { return m_coalescedResult; }

	uint64_t GetTotalVRAMUsage();

public:
	bool useGatherFiltering = true;
	bool useDepthAwareMerging = false;

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

	float m_rayLength0;
	uint32_t m_raysPerProbe0 = 0u;

	// Signifies that the cascade textures will be 1 / rayscalingfactor of the original size.
	bool m_preAveragedIntervals;

	uint32_t m_probeSpacing0 = 0u;
	uint32_t m_probeCount0X = 0u;
	uint32_t m_probeCount0Y = 0u;
};