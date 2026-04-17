#pragma once

#include "TestSuite.h"
#include "RadianceCascadeManager3D.h"



class MastersTestSuite : public TestSuite
{
public:
	struct Inputs
	{
		// Inputs
		uint32_t raysPerProbe0;
		uint32_t probeSpacing0;
		uint32_t maxAllowedCascadeLevels;
	};

	struct Outputs
	{
		// Outputs
		uint64_t totalVRAMSize = 0; // Total VRAM size of resources relevant to the test suite.
		std::vector<std::pair<const char*, float>> averageFrametimes; // Pair of profile name and average time in ms.
	};

	static constexpr uint32_t sFramesBetweenTests = 128u;

	MastersTestSuite(RadianceCascadeManager3D& rcManager3D, uint32_t backBufferWidth, uint32_t backBufferHeight);
	void OutputTestSuiteToCSV() override;

protected:
	void OnCaseBegin(uint32_t testIndex) override;
	bool OnCaseTick(uint32_t testIndex) override;
	void OnCaseCompleted(uint32_t testIndex) override;
	size_t GetCaseCount() override;

private:
	RadianceCascadeManager3D& m_rcManager3D;
	uint32_t m_framesWaitedFor = 0u;
	uint32_t m_backBufferWidth = 0u;
	uint32_t m_backBufferHeight = 0u;
	TestCaseContainer<Inputs, Outputs> m_testCases = {};
};