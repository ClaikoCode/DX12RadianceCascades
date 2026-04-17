#pragma once

#include "TestSuite.h"
#include "RadianceCascadeManager3D.h"
#include "RadianceCascades.h"
#include "Core\Camera.h"

#include <unordered_map>
#include <array>

class TestSuiteGatherFilter : public TestSuite
{
public:
	enum Scenario
	{
		ScenarioHeavyOcclusion = 0,
		ScenarioMediumOcclusion,
		ScenarioLowOcclusion,

		ScenarioCount // Keep last!
	};

	TestSuiteGatherFilter(RadianceCascades& radianceCascades, RadianceCascadeManager3D& rcManager3D, Math::Camera& camera);
	void OutputTestSuiteToCSV() override;

protected:
	void OnCaseBegin(uint32_t caseIndex) override;
	bool OnCaseTick(uint32_t caseIndex) override;
	void OnCaseCompleted(uint32_t caseIndex) override;
	size_t GetCaseCount() override;

private:
	static constexpr uint32_t sFramesToCollect = 64;
	// Constant max level cascades for all tests.
	static constexpr uint32_t sGatherFilterMaxCascadeCount = 6u;

	struct Inputs
	{
		uint32_t raysPerProbe0;
		uint32_t probeSpacing0;
		bool useGatherFilter;
		Scenario scenario;
		ResolutionTarget resolution;
	};

	struct Outputs
	{
		std::unordered_map<const char*, std::array<float, sFramesToCollect>> frameTimes;
		std::array<float, sGatherFilterMaxCascadeCount - 1u> filteredRayProportions;
	};

private:
	RadianceCascadeManager3D& m_rcManager3D;
	RadianceCascades& m_radianceCascades;
	Math::Camera& m_camera;
	uint32_t m_framesCollected = 0;
	TestCaseContainer<Inputs, Outputs> m_testCases = {};
};