#include "rcpch.h"
#include "TestSuiteMasters.h"

#include <fstream>

#include "Profiling\GPUProfiler.h"

#if defined(RUN_TESTS)
	static_assert(MaxFrametimeSampleCount == MastersTestSuite::sFramesBetweenTests);
#endif

MastersTestSuite::MastersTestSuite(RadianceCascadeManager3D& rcManager3D, uint32_t backBufferWidth, uint32_t backBufferHeight) 
	: m_rcManager3D(rcManager3D), m_backBufferWidth(backBufferWidth), m_backBufferHeight(backBufferHeight)
{
	//std::vector<uint32_t> raysPerProbe0Vals = { 16, 36, 64 };
	//std::vector<uint32_t> probeSpacing0Vals = { 1, 2, 3, 4 };
	//std::vector<uint32_t> maxAllowedCascadeLevelsVals = { 5, 6, 7, 8 };

	std::vector<uint32_t> raysPerProbe0Vals = { 16, 64 };
	std::vector<uint32_t> probeSpacing0Vals = { 1, 2, 3, 4 };
	std::vector<uint32_t> maxAllowedCascadeLevelsVals = { 5 };

	for (uint32_t raysPerProbe : raysPerProbe0Vals)
	{
		for (uint32_t probeSpacing : probeSpacing0Vals)
		{
			for (uint32_t maxCascadeLevel : maxAllowedCascadeLevelsVals)
			{
				AddCase({
					.raysPerProbe0 = raysPerProbe,
					.probeSpacing0 = probeSpacing,
					.maxAllowedCascadeLevels = maxCascadeLevel
				});
			}
		}
	}
}

void MastersTestSuite::OnCaseBegin(const TestCase& testCase)
{
	const MasterTestSuiteInputs& inputs = testCase.inputs;

	m_rcManager3D.Generate(
		inputs.raysPerProbe0,
		inputs.probeSpacing0,
		m_backBufferWidth,
		m_backBufferHeight,
		inputs.maxAllowedCascadeLevels
	);

	LOG_INFO(
		L"Params: RaysPerProbe0 = {}, ProbeSpacing0 = {}, MaxAllowedCascadeLevels = {}",
		inputs.raysPerProbe0,
		inputs.probeSpacing0,
		inputs.maxAllowedCascadeLevels
	);

	// Reset frames waited for.
	m_framesWaitedFor = 0;
}

bool MastersTestSuite::OnCaseTick(TestCase& testCase)
{
	LOG_DEBUG(L"Current frame count: {} ({}%)", m_framesWaitedFor, 100 * m_framesWaitedFor / sFramesBetweenTests);

	return ++m_framesWaitedFor >= sFramesBetweenTests;
}

void MastersTestSuite::OnCaseCompleted(TestCase& testCase)
{
	auto& profiles = GPUProfiler::Get().GetProfiles();
	for (const auto& profile : profiles)
	{
		if (profile.name == nullptr)
		{
			continue; // Skip null profiles.
		}

		float frametimeSum = 0.0f;
		for (float frameTime : profile.timeSamples)
		{
			frametimeSum += frameTime;
		}

		const float frametimeAverage = frametimeSum / profile.timeSamples.size();
		testCase.outputs.averageFrametimes.push_back({ profile.name, frametimeAverage });
	}

	// Save VRAM usage before generating new RC resources.
	testCase.outputs.totalVRAMSize = m_rcManager3D.GetTotalVRAMUsage();
}

void MastersTestSuite::OutputTestSuiteToCSV()
{
	std::wstring fileName = L"RadianceCascadesTestSuiteMasters.csv";
	std::wofstream fileStream(fileName);
	if (!fileStream.is_open())
	{
		throw std::runtime_error("Failed to open file for writing test suite data.");
	}

	LOG_INFO(L"Writing test suite data to file: {}", fileName);

	// Headers
	{
		// Add names for each column in the CSV file.
		fileStream << L"RaysPerProbe,ProbeSpacing,MaxCascadeLevels";

		// Add names of each average frame time as headers.
		for (const auto& averageFrameTime : m_testCases[0].outputs.averageFrametimes)
		{
			fileStream << L"," << averageFrameTime.first;
		}

		// Add VRAM size column header.
		fileStream << L",VRAM";

		fileStream << L"\n";
	}

	// Data
	for (size_t i = 0; i < m_testCases.size(); i++)
	{
		TestCase& testCase = m_testCases[i];

		uint32_t raysPerProbeValue = testCase.inputs.raysPerProbe0;
		uint32_t probeSpacingValue = testCase.inputs.probeSpacing0;
		uint32_t cascadeLevelValue = testCase.inputs.maxAllowedCascadeLevels;

		fileStream
			<< raysPerProbeValue << L","
			<< probeSpacingValue << L","
			<< cascadeLevelValue << L",";

		for (const auto& averageFrameTime : testCase.outputs.averageFrametimes)
		{
			fileStream << averageFrameTime.second << L",";
		}

		fileStream << testCase.outputs.totalVRAMSize;

		fileStream << L"\n";
	}

	fileStream.close();
}

