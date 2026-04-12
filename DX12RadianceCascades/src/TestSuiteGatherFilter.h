#pragma once

#include "TestSuite.h"
#include "RadianceCascadeManager3D.h"
#include "RadianceCascades.h"
#include "Core\Camera.h"

#include <unordered_map>
#include <array>

// Constant max level cascades for all tests.
constexpr uint32_t TestSuiteGatherFilterMaxCascadeCount = 6u;

struct TestSuiteGatherFilterInputs
{
	enum Scenario
	{
		ScenarioHeavyOcclusion = 0,
		ScenarioMediumOcclusion,
		ScenarioLowOcclusion,

		ScenarioCount // Keep last!
	};

	uint32_t raysPerProbe0;
	uint32_t probeSpacing0;
	bool useGatherFilter;
	Scenario scenario;
	ResolutionTarget resolution;
};

struct TestSuiteGatherFilterOutputs
{
	std::unordered_map<const char*, std::array<float, 1024>> frameTimes;
	std::array<float, TestSuiteGatherFilterMaxCascadeCount - 1u> filteredRayProportions;
};

class TestSuiteGatherFilter : public TestSuite<TestSuiteGatherFilterInputs, TestSuiteGatherFilterOutputs>
{
public:
	// A bit messy but lets this be a static member evaluated at compile time.
	// Breakdown: 
	// decltype() deduces the type returned from the expression TestSuiteGatherFilterOutputs::frameTimes
	// ::mapped_type is given by unordered_map and exposes the type values map to (std::array<>)
	// tuple_size_v gives the value for the size of tuple like objects.
	static constexpr uint32_t sFramesToCollect =
		std::tuple_size_v<decltype(TestSuiteGatherFilterOutputs::frameTimes)::mapped_type>;

	TestSuiteGatherFilter(RadianceCascades& radianceCascades, RadianceCascadeManager3D& rcManager3D, Math::Camera& camera);
	void OutputTestSuiteToCSV() override;
	
protected:
	void OnCaseBegin(const TestCase& testCase) override;
	bool OnCaseTick(TestCase& testCase) override;
	void OnCaseCompleted(TestCase& testCase) override;


private:
	RadianceCascadeManager3D& m_rcManager3D;
	RadianceCascades& m_radianceCascades;
	Math::Camera& m_camera;
	uint32_t m_framesCollected = 0;
};