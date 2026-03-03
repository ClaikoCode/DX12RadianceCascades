#include "rcpch.h"

#include "TestSuiteGatherFilter.h"
#include "Profiling\GPUProfiler.h"
#include "Logger.h"
#include "Core\Math\Vector.h"

using MeasurementScenario = TestSuiteGatherFilterInputs::Scenario;

struct CameraTransform
{
	Math::Vector3 pos;
	Math::Vector3 lookDir;
};

// These positions and directions were gathered at different parts of the scene and then rounded.
static const std::array<CameraTransform, MeasurementScenario::ScenarioCount> sScenarioTransforms = {{
	{ Math::Vector3(-1280.0f, -435.0f, -345.0f), Math::Vector3(0.85f, 0.0f, 0.5f) },
	{ Math::Vector3(-650.0f, -500.0f, 0.0f), Math::Vector3(1.0f, 0.0f, 0.0f) },
	{ Math::Vector3(-2180.0f, 2100.0f, -1300.0f), Math::Vector3(0.8f, -0.35f, 0.50f) }
}};

static const std::array<const wchar_t*, MeasurementScenario::ScenarioCount> sScenarioNames = {{
	L"Heavy Occlusion",
	L"Medium Occlusion",
	L"Low Occlusion"
}};

static const std::array<const wchar_t*, ResolutionTargetCount> sResolutionNames = {{
	L"Invalid",
	L"1080p",
	L"1440p",
	L"2160p"
}};


TestSuiteGatherFilter::TestSuiteGatherFilter(RadianceCascades& radianceCascades, RadianceCascadeManager3D& rcManager3D, Math::Camera& camera)
	: m_rcManager3D(rcManager3D), m_radianceCascades(radianceCascades), m_camera(camera)
{
	std::vector<uint32_t> raysPerProbe0Vals = { 16, 64 };
	std::vector<uint32_t> probeSpacing0Vals = { 1, 2 };
	std::vector<bool> useGatherFilterVals = { false, true };
	std::vector<MeasurementScenario> scenarios = {
		MeasurementScenario::ScenarioLowOcclusion,
		MeasurementScenario::ScenarioMediumOcclusion,
		MeasurementScenario::ScenarioHeavyOcclusion
	};
	std::vector<ResolutionTarget> resolutions = {
		ResolutionTarget1080p,
		ResolutionTarget1440p,
		ResolutionTarget2160p
	};

	for (ResolutionTarget resolutionTarget : resolutions)
	{
		for (uint32_t raysPerProbe : raysPerProbe0Vals)
		{
			for (uint32_t probeSpacing : probeSpacing0Vals)
			{
				for (bool useGatherFilter : useGatherFilterVals)
				{
					for (MeasurementScenario scenario : scenarios)
					{
						AddCase({
							.raysPerProbe0 = raysPerProbe,
							.probeSpacing0 = probeSpacing,
							.useGatherFilter = useGatherFilter,
							.scenario = scenario,
							.resolution = resolutionTarget
						});
					}
				}
			}
		}
	}
	
}

void TestSuiteGatherFilter::OutputTestSuiteToCSV()
{
	std::wstring fileName = L"RadianceCascadesTestSuiteGatherFilter.csv";
	std::wofstream fileStream(fileName);
	if (!fileStream.is_open())
	{
		throw std::runtime_error("Failed to open file for writing test suite data.");
	}

	LOG_INFO(L"Writing test suite data to file: {}", fileName);

	// Headers
	{
		// Add names for each column in the CSV file.
		fileStream << L"RaysPerProbe,ProbeSpacing,UseGatherFilter,Scenario,Resolution";

		// Add names of each average frame time as headers.
		for (const auto& [profileName, _] : m_testCases[0].outputs.frameTimes)
		{
			fileStream << L"," << profileName;
		}

		// Add column for frame number for the measurements
		fileStream << ",FrameNumber";

		fileStream << L"\n";
	}

	// Data
	for (size_t i = 0; i < m_testCases.size(); i++)
	{
		TestCase& testCase = m_testCases[i];

		uint32_t raysPerProbeValue = testCase.inputs.raysPerProbe0;
		uint32_t probeSpacingValue = testCase.inputs.probeSpacing0;
		bool useGatherFiltering = testCase.inputs.useGatherFilter;
		const wchar_t* scenario = sScenarioNames[testCase.inputs.scenario];
		const wchar_t* resolution = sResolutionNames[testCase.inputs.resolution];

		for (uint32_t frameIndex = 0; frameIndex < sFramesToCollect; frameIndex++)
		{
			fileStream
				<< raysPerProbeValue << L","
				<< probeSpacingValue << L","
				<< useGatherFiltering << L","
				<< scenario << L","
				<< resolution << L",";

			for (const auto& [_, frameTimes] : testCase.outputs.frameTimes)
			{
				fileStream << frameTimes[frameIndex] << L",";
			}

			fileStream << frameIndex;

			fileStream << L"\n";
		}
	}

	fileStream.close();
}

void TestSuiteGatherFilter::OnCaseBegin(const TestCase& testCase)
{
	const TestSuiteGatherFilterInputs& inputs = testCase.inputs;

	uint32_t width, height;
	ResolutionTargetToDimensions(inputs.resolution, width, height);

	// Constant max level cascades for all tests.
	const uint32_t maxCascadeLevel = 5u;
	m_rcManager3D.Generate(
		inputs.raysPerProbe0,
		inputs.probeSpacing0,
		width,
		height,
		maxCascadeLevel
	);

	m_rcManager3D.useGatherFiltering = inputs.useGatherFilter;

	const CameraTransform& camTransform = sScenarioTransforms[inputs.scenario];
	m_camera.SetEyeAtUp(camTransform.pos, camTransform.pos + camTransform.lookDir, Math::Vector3(Math::kYUnitVector));
	m_camera.Update();

	LOG_INFO(
		L"Params: RaysPerProbe0 = {}, ProbeSpacing0 = {}, UseGatherFilter = {}, Scenario = {}",
		inputs.raysPerProbe0,
		inputs.probeSpacing0,
		inputs.useGatherFilter,
		sScenarioNames[inputs.scenario]
	);

	m_radianceCascades.ResizeToResolutionTarget(inputs.resolution);

	// Reset frames waited for.
	m_framesCollected = 0;
}

bool TestSuiteGatherFilter::OnCaseTick(TestCase& testCase)
{
	auto& profiles = GPUProfiler::Get().GetProfiles();

	for (const auto& profile : profiles)
	{
		if (profile.name == nullptr)
		{
			continue;
		}

		testCase.outputs.frameTimes[profile.name][m_framesCollected] = profile.GetLastSample();
	}

	m_framesCollected++;

	LOG_DEBUG(L"Collected frame {}/{} ({}%)", m_framesCollected, sFramesToCollect, 100 * m_framesCollected / sFramesToCollect);
	return m_framesCollected == sFramesToCollect;
}

void TestSuiteGatherFilter::OnCaseCompleted(TestCase& testCase)
{
	
}
