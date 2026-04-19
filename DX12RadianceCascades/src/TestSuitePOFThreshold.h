#pragma once

#include "TestSuite.h"
#include "Core\Math\Vector.h"
#include "Core\Math\Quaternion.h"

#include "Core\Camera.h"

#include "RadianceCascadeManager3D.h"

#include <vector>


class TestSuitePOFThreshold : public TestSuite
{
public:
	TestSuitePOFThreshold(Math::Camera& camera, RadianceCascadeManager3D& rcManager, const std::vector<Math::AxisAlignedBox>& samplingVolumes);
	void OutputTestSuiteToCSV() override;

private:
	static constexpr uint32_t sRandSeed = 128760022u;
	static constexpr size_t sNumSamplesPerCase = 10000ull;

	struct Input
	{
		bool usePOF;
	};

	struct Output
	{
		std::vector<float> frameTimes = std::vector<float>(sNumSamplesPerCase);
		std::vector<float> filteredRayRatios = std::vector<float>(sNumSamplesPerCase);
	};
	
	struct CamTransform
	{
		Math::Vector3 pos;
		Math::Quaternion rot;
	};

protected:
	void OnCaseBegin(uint32_t caseIndex) override;
	bool OnCaseTick(uint32_t caseIndex) override;
	void OnCaseCompleted(uint32_t caseIndex) override;
	size_t GetCaseCount() override;

private:
	Math::Camera& m_camera;
	RadianceCascadeManager3D& m_rcManager;

	// A single vector holding random transforms created at construction.
	// This allows POF and non-POF runs to use the exact same transform for its measurements.
	std::vector<CamTransform> m_randCameraTransforms = std::vector<CamTransform>(sNumSamplesPerCase);
	TestCaseContainer<Input, Output> m_testCases = {};

	uint32_t m_transformIndex;
};
