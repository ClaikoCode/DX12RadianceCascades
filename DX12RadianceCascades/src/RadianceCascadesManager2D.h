#pragma once

__declspec(align(16)) struct CascadeInfo
{
	uint32_t cascadeIndex;
	float padding[3];
};

__declspec(align(16)) struct RC2DGlobals
{
	uint32_t probeScalingFactor;
	uint32_t rayScalingFactor;
	uint32_t probeDim0;
	uint32_t rayCount0;
	float rayLength0;
	float probeSpacing0;
	float sourceSize;
};

class RadianceCascadesManager2D
{
public:
	RadianceCascadesManager2D() : probeDim0(0), rayLength0(0.0f), raysPerProbe0(0), probeSpacing0(0) {};
	~RadianceCascadesManager2D();
	void Init(float rayLength0, float _raysPerProbe0, float _maxRayLength);
	void Shutdown();

	ColorBuffer& GetCascadeInterval(uint32_t cascadeIndex) { return m_cascadeIntervals[cascadeIndex]; }
	ColorBuffer& GetRadianceField() { return m_radianceField; }
	uint32_t GetCascadeCount() { return (uint32_t)m_cascadeIntervals.size(); }

	RC2DGlobals FillRCGlobalsData(uint32_t sourceSize);

	uint32_t GetProbePixelSize(uint32_t cascadeIndex); // Per dim.
	uint32_t GetProbeCount(uint32_t cascadeIndex); // Per dim.
	float GetProbeSpacing(uint32_t cascadeIndex);

	void ClearBuffers(GraphicsContext& gfxContext);

public:
	struct ScalingFactor
	{
		uint16_t probeScalingFactor = 2u; // Scaling factor per dim.
		uint16_t rayScalingFactor = 4u;
	} scalingFactor;

	uint16_t probeDim0; // Amount of probes in one dimension of cascade 0
	float rayLength0; // The length of a ray in cascade 0.
	uint32_t raysPerProbe0;
	float probeSpacing0;

private:
	std::vector<ColorBuffer> m_cascadeIntervals;
	ColorBuffer m_radianceField;
};