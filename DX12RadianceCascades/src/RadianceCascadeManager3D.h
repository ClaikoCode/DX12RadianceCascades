#pragma once

struct RCGlobals;

class RadianceCascadeManager3D
{
public:
	RadianceCascadeManager3D() = default;

	// Rays per probe needs to be power of 2 and larger than 8.
	void Init(float rayLength0, uint32_t raysPerProbe0, uint32_t probesPerDim0, uint32_t cascadeIntervalCount, bool usePreAverage, bool useDepthAwareMerging);

	void FillRCGlobalInfo(RCGlobals& rcGlobalInfo);

	void ClearBuffers(GraphicsContext& gfxContext);
	uint32_t GetRaysPerProbe(uint32_t cascadeIndex);
	uint32_t GetProbeCountPerDim(uint32_t cascadeIndex);
	uint32_t GetProbeCount(uint32_t cascadeIndex);
	float GetStartT(uint32_t cascadeIndex);
	float GetRayLength(uint32_t cascadeIndex);
	uint32_t GetCascadeIntervalCount() { return (uint32_t)m_cascadeIntervals.size(); }
	ColorBuffer& GetCascadeIntervalBuffer(uint32_t cascadeIndex) { return m_cascadeIntervals[cascadeIndex]; }
	uint32_t GetProbeScalingFactor() const { return m_scalingFactor.probeScalingFactor; }
	ColorBuffer& GetCoalesceBuffer() { return m_coalescedResult; }
	float GetRayLength() { return m_rayLength0; }
	void SetRayLength(float rayLength) { m_rayLength0 = rayLength; }
	void SetDepthAwareMerging(bool depthAwareMerging) { m_depthAwareMerging = depthAwareMerging; }

	bool UsesPreAveragedIntervals() const { return m_preAveragedIntervals; }

private:
	struct ScalingFactor
	{
		uint32_t probeScalingFactor = 2u;
		uint32_t rayScalingFactor = 4u; // This needs to be a perfect square.
	} m_scalingFactor;

	std::vector<ColorBuffer> m_cascadeIntervals;
	ColorBuffer m_coalescedResult;

	float m_rayLength0;
	uint32_t m_raysPerProbe0;
	uint32_t m_probesPerDim0;

	// Signifies that the cascade textures will be 1 / rayscalingfactor of the original size.
	bool m_preAveragedIntervals;
	bool m_depthAwareMerging;
};