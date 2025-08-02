#include "rcpch.h"
#include "GPUProfiler.h"

#include "AppGUI\AppGUI.h"

using namespace Microsoft::WRL;

GPUProfiler::GPUProfiler()
{
	ComPtr<IDXGIFactory4> dxgiFactory = nullptr;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())));
	ThrowIfFailed(dxgiFactory->EnumAdapters(0, reinterpret_cast<IDXGIAdapter**>(m_vramAdapter.GetAddressOf())));

	D3D12_QUERY_HEAP_DESC queryDesc;
	queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryDesc.Count = MaxQueries;
	queryDesc.NodeMask = 0;

	ThrowIfFailed(Graphics::g_Device->CreateQueryHeap(
		&queryDesc,
		IID_PPV_ARGS(m_queryHeap.GetAddressOf())
	));

	// Each entry is a 64 byte timestamp value.
	m_queryResultBuffer.Create(L"GPUProfiler Readback buffer", MaxQueries, sizeof(uint64_t));
	m_queryHeapMemory = reinterpret_cast<uint64_t*>(m_queryResultBuffer.Map());
}

void GPUProfiler::DestroyImpl()
{
	m_vramAdapter = nullptr;
	m_queryHeap = nullptr;
	m_queryResultBuffer.Unmap();
	m_queryResultBuffer.Destroy();
}

uint32_t GPUProfiler::StartProfile(ID3D12GraphicsCommandList* commandList, const char* name)
{
	ASSERT(commandList != nullptr && name != nullptr);

	uint32_t profileIndex = uint32_t(-1);
	for (uint32_t i = 0; i < m_profiles.size(); i++)
	{
		if (m_profiles[i].name == name)
		{
			profileIndex = i;
			break;
		}
	}

	// Find free space for profile if none were found.
	if (profileIndex == uint32_t(-1))
	{
		ASSERT(m_profileCount < m_profiles.size())

		profileIndex = m_profileCount++;

		m_profiles[profileIndex].name = name;
	}

	commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, profileIndex * 2);

	m_profiles[profileIndex].isQuerying = true;
	return profileIndex;
}

void GPUProfiler::EndProfile(ID3D12GraphicsCommandList* commandList, uint32_t profileIndex)
{
	ASSERT(commandList != nullptr && profileIndex < m_profiles.size());

	ASSERT(m_profiles[profileIndex].isQuerying == true);

	const uint32_t startQueryIndex = profileIndex * 2;
	commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQueryIndex + 1);

	// Resolve data into readback buffer.
	commandList->ResolveQueryData(
		m_queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		startQueryIndex,
		2,
		m_queryResultBuffer.GetResource(),
		startQueryIndex * sizeof(uint64_t) // Offset in bytes
	);

	m_profiles[profileIndex].isQuerying = false;
}

float GPUProfiler::GetCurrentMemory(MemoryUnit memoryUnit /* = MegaByte */)
{
	DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo;
	ThrowIfFailed(m_vramAdapter->QueryVideoMemoryInfo(
		0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
		&videoMemoryInfo
	));

	return videoMemoryInfo.CurrentUsage / (float)memoryUnit;
}

void GPUProfiler::UpdateProfiles(uint64_t timestampFrequency)
{
	ASSERT(m_queryHeapMemory != nullptr);

	for (uint32_t i = 0u; i < m_profileCount; i++)
	{
		const uint32_t queryStartIndex = i * 2;
		PerfProfile& perfProfile = m_profiles[i];

		ASSERT(perfProfile.isQuerying == false);

		uint64_t startTime = m_queryHeapMemory[queryStartIndex];
		uint64_t endTime = m_queryHeapMemory[queryStartIndex + 1];

		double frameTimeMS = double(endTime - startTime) / timestampFrequency * 1000.0;
		perfProfile.timeSamples[perfProfile.currentSampleCount++] = (float)frameTimeMS;

		// Circlular buffer adjustment.
		perfProfile.currentSampleCount = perfProfile.currentSampleCount % MaxSampleCount;
	}
}

void GPUProfiler::DrawProfileUI()
{
	ImGui::Begin("Performance Statistics");

	for (PerfProfile& perfProfile : m_profiles)
	{
		if (perfProfile.name == nullptr)
		{
			continue;
		}
		
		// Moved by MaxSampleCount to avoid underflow if current sample count is 0.
		const uint32_t lastSampleIndex = (perfProfile.currentSampleCount + MaxSampleCount - 1) % MaxSampleCount;
		float lastSampleTime = perfProfile.timeSamples[lastSampleIndex];

		float timeSum = 0.0f;
		for (float timeSample : perfProfile.timeSamples)
		{
			timeSum += timeSample;
		}
		float averageTime = timeSum / perfProfile.timeSamples.size();

		char msValue[64] = { 0 };
		sprintf_s(msValue, " (%.3f ms | Avg: %.3f ms)", lastSampleTime, averageTime);
		std::string plotName = std::string(perfProfile.name) + msValue;
		ImGui::PlotLines(
			plotName.c_str(),
			perfProfile.timeSamples.data(),
			(int)perfProfile.timeSamples.size(),
			perfProfile.currentSampleCount,
			"",
			0.0f,
			15.0f,
			ImVec2(400.0f, 60.0f)
		);
	}

	ImGui::End();
}

ProfileBlock::ProfileBlock(CommandContext& commandContext, const char* name)
{
	commandList = commandContext.GetCommandList();
	profileIndex = GPUProfiler::Get().StartProfile(commandList, name);
}

ProfileBlock::~ProfileBlock()
{
	ASSERT(commandList != nullptr && profileIndex != uint32_t(-1));

	GPUProfiler::Get().EndProfile(commandList, profileIndex);
}
