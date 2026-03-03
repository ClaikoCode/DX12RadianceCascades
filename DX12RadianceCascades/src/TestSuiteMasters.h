#pragma once

#include "TestSuite.h"
#include "RadianceCascadeManager3D.h"

struct MasterTestSuiteInputs
{
	// Inputs
	uint32_t raysPerProbe0;
	uint32_t probeSpacing0;
	uint32_t maxAllowedCascadeLevels;
};

struct MasterTestSuiteOutputs
{
	// Outputs
	uint64_t totalVRAMSize = 0; // Total VRAM size of resources relevant to the test suite.
	std::vector<std::pair<const char*, float>> averageFrametimes; // Pair of profile name and average time in ms.
};

class MastersTestSuite : public TestSuite<MasterTestSuiteInputs, MasterTestSuiteOutputs>
{
public:
	static constexpr uint32_t sFramesBetweenTests = 128u;

	MastersTestSuite(RadianceCascadeManager3D& rcManager3D, uint32_t backBufferWidth, uint32_t backBufferHeight);
	void OutputTestSuiteToCSV() override;

protected:
	void OnCaseBegin(const TestCase& testCase) override;
	bool OnCaseTick(TestCase& testCase) override;
	void OnCaseCompleted(TestCase& testCase) override;

private:
	RadianceCascadeManager3D& m_rcManager3D;
	uint32_t m_framesWaitedFor = 0u;
	uint32_t m_backBufferWidth = 0u;
	uint32_t m_backBufferHeight = 0u;
};