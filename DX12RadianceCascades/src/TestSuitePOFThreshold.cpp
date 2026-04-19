#include "rcpch.h"

#include "TestSuitePOFThreshold.h"
#include "Core\Utility.h"
#include "Core\Math\Boundingbox.h"

#include "Profiling/GPUProfiler.h"

#include <iostream>
#include <random>

constexpr float cRatioNaNValue = FLT_MAX;

float GetAABBVolume(const Math::AxisAlignedBox& aabb)
{
	Math::Vector3 dims = aabb.GetDimensions();
	return dims.GetX() * dims.GetY() * dims.GetZ();
}

// Generates a quaternion Q that is uniformly distributed in quaternion space. 
// Uses the algorithm presented by Ken Shoemake from Graphics Gems III (1992)
// DOI: https://doi.org/10.1016/B978-0-08-050755-2.50036-1
Math::Quaternion GenerateRandomQuaternion(std::mt19937& rng)
{
	std::uniform_real_distribution<float> dis(0.0f, 1.0f);

	float X0 = dis(rng);
	float X1 = dis(rng);
	float X2 = dis(rng);

	float theta1 = Math::XM_2PI * X1;
	float theta2 = Math::XM_2PI * X2;

	float sin1 = Math::Sin(theta1);
	float cos1 = Math::Cos(theta1);
	float sin2 = Math::Sin(theta2);
	float cos2 = Math::Cos(theta2);

	float r1 = Math::Sqrt(1 - X0);
	float r2 = Math::Sqrt(X0);

	return Math::Quaternion({ sin1 * r1, cos1 * r1, sin2 * r2, cos2 * r2 });
}

TestSuitePOFThreshold::TestSuitePOFThreshold(Math::Camera& camera, RadianceCascadeManager3D& rcManager, const std::vector<Math::AxisAlignedBox>& samplingVolumes)
	: m_camera(camera), m_rcManager(rcManager), m_transformIndex(0u)
{
	ASSERT(!samplingVolumes.empty());
	
	// Sampling point setup
	{
		float totalVolume = 0.0f;
		std::vector<float> volumes(samplingVolumes.size());
		for (size_t i = 0u; i < samplingVolumes.size(); i++)
		{
			float volume = GetAABBVolume(samplingVolumes[i]);
			volumes[i] = volume;
			totalVolume += volume;
		}

		// Cumulative distribution function.
		std::vector<float> cdf(samplingVolumes.size());
		for (size_t i = 0; i < cdf.size(); i++)
		{
			cdf[i] = volumes[i] / totalVolume;

			if (i > 0)
			{
				cdf[i] += cdf[i - 1];
			}
		}

		std::mt19937 rng = std::mt19937(sRandSeed);
		std::uniform_real_distribution<float> dis(0.0f, 1.0f);

		for (size_t i = 0; i < m_randCameraTransforms.size(); i++)
		{
			float r = dis(rng);
			size_t volumeIndex;
			for (volumeIndex = 0; volumeIndex < cdf.size(); volumeIndex++)
			{
				if (r <= cdf[volumeIndex])
				{
					break;
				}
			}

			const Math::AxisAlignedBox& aabb = samplingVolumes[volumeIndex];

			// Construct number in [0, 1]^3
			Math::Vector3 randomRelativePos = Math::Vector3(dis(rng), dis(rng), dis(rng));

			
			// Creates a random point in world space within the aabb volume.
			Math::Vector3 randWorldPos = aabb.GetMin() + aabb.GetDimensions() * randomRelativePos;
			Math::Quaternion randomRot = GenerateRandomQuaternion(rng);
			m_randCameraTransforms[i] = { randWorldPos, randomRot };
		}
	}
	

	std::vector<bool> usePOFCases = { true, false };
	for (bool usePOFCase : usePOFCases)
	{
		m_testCases.EmplaceTestCase({ usePOFCase });
	}
}

void TestSuitePOFThreshold::OutputTestSuiteToCSV()
{
	std::wstring csvName = L"POFThresholdTests(" + std::to_wstring(sRandSeed) + L").csv";
	std::wofstream outputFile(csvName);

	if (outputFile.is_open())
	{
		outputFile << "Use POF, RC Frame Time, Filtered Ray Ratio, Transform Index\n";

		for (size_t testCaseIndex = 0; testCaseIndex < m_testCases.size(); testCaseIndex++)
		{
			const auto& testCase = m_testCases[testCaseIndex];

			std::wstring usesPOFStr = testCase.inputs.usePOF ? L"true" : L"false";

			for (size_t i = 0; i < sNumSamplesPerCase; i++)
			{
				float rayRatio = testCase.outputs.filteredRayRatios[i];
				std::wstring rayRatioStr = rayRatio == cRatioNaNValue ? L"NaN" : std::to_wstring(rayRatio);

				outputFile << usesPOFStr << ",";
				outputFile << testCase.outputs.frameTimes[i] << ",";
				outputFile << rayRatioStr << ",";
				outputFile << i << "\n";
			}
		}
	}

	outputFile.close();
}

void TestSuitePOFThreshold::OnCaseBegin(uint32_t caseIndex)
{
	m_transformIndex = 0;

	const CamTransform& camTransform = m_randCameraTransforms[m_transformIndex];
	m_camera.SetPosition(camTransform.pos);
	m_camera.SetRotation(camTransform.rot);
	m_camera.Update();

	const auto& testCase = m_testCases[caseIndex];
	m_rcManager.SetGatherFiltering(m_testCases[caseIndex].inputs.usePOF);
}

bool TestSuitePOFThreshold::OnCaseTick(uint32_t caseIndex)
{
	const auto& profiles = GPUProfiler::Get().GetProfiles();

	float rcRenderTime = 0.0f;
	for (const auto& profile : profiles)
	{
		if (profile.name == nullptr)
		{
			continue;
		}

		std::string_view profileNameStr = profile.name;
		if (profileNameStr == "RC Gather")
		{
			rcRenderTime += profile.GetLastSample();
		}
	}

	auto& testCase = m_testCases[caseIndex];
	
	float rayRatio = cRatioNaNValue;
	if (testCase.inputs.usePOF)
	{
		// Get what percentage of cascade 1 rays were filtered by cascade 0.
		rayRatio = m_rcManager.GetFilteredRayCount(0) / (float)m_rcManager.GetTotalRays(1);
	}

	testCase.outputs.filteredRayRatios[m_transformIndex] = rayRatio;
	testCase.outputs.frameTimes[m_transformIndex] = rcRenderTime;

	LOG_DEBUG(L"Finished sample {}/{} ({}%)", m_transformIndex + 1, sNumSamplesPerCase, 100.0f * (m_transformIndex + 1) / (float)sNumSamplesPerCase);

	m_transformIndex++;

	if (m_transformIndex >= sNumSamplesPerCase)
	{
		return true; // All cases are done.
	}

	const CamTransform& newCamTransform = m_randCameraTransforms[m_transformIndex];
	m_camera.SetPosition(newCamTransform.pos);
	m_camera.SetRotation(newCamTransform.rot);
	m_camera.Update();

	return false;
}

void TestSuitePOFThreshold::OnCaseCompleted(uint32_t caseIndex)
{
	LOG_DEBUG(L"Finished case {}/{}", caseIndex, GetCaseCount());
}

size_t TestSuitePOFThreshold::GetCaseCount()
{
	return m_testCases.size();
}
