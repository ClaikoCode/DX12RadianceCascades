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

	// Each entry is a 64 bit timestamp value.
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

uint64_t GPUProfiler::GetCurrentVRAMUsageBytes()
{
	DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfo = {};
	ThrowIfFailed(m_vramAdapter->QueryVideoMemoryInfo(
		0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
		&videoMemoryInfo
	));

	return videoMemoryInfo.CurrentUsage;
}

uint32_t GPUProfiler::StartPerformanceProfile(ID3D12GraphicsCommandList* commandList, const char* name)
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

void GPUProfiler::EndPerformanceProfile(ID3D12GraphicsCommandList* commandList, uint32_t profileIndex)
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

void GPUProfiler::PushMemoryEntry(const char* name)
{
	m_memoryEntries.push_back({
		name,
		GetCurrentVRAMUsageBytes()
	});
}

void GPUProfiler::PopMemoryEntry()
{
	m_memoryEntries.pop_back();
}

float GPUProfiler::GetCurrentVRAMUsage(MemoryUnit memoryUnit /* = MegaByte */)
{
	return GetCurrentVRAMUsageBytes() / (float)memoryUnit;
}

void GPUProfiler::UpdatePerformanceProfiles(uint64_t timestampFrequency)
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

void GPUProfiler::UpdateMemoryEntries()
{
	for (int i = (int)m_memoryEntries.size() - 1; i >= 0; i--)
	{
		MemoryEntry& memEntry = m_memoryEntries[i];
		
		// Calculates the diff between the current mem entry and the previous.
		uint64_t memDiff = i > 0 ? memEntry.currentVRAMUsage - m_memoryEntries[i - 1].currentVRAMUsage : memEntry.currentVRAMUsage;

		// The mem diff is the mem usage of that section.
		memEntry.thisVRAMUsage = memDiff;
	}
}

void GPUProfiler::UpdateData(uint64_t timestampFrequency)
{
	UpdatePerformanceProfiles(timestampFrequency);
	UpdateMemoryEntries();
}

void GPUProfiler::DrawProfilerUI()
{
	ImGui::Begin("Performance Statistics");

	if(ImGui::CollapsingHeader("Frametime"))
	{
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
	}

	if (ImGui::CollapsingHeader("Memory"))
	{
		const MemoryUnit defaultMemoryUnit = MemoryUnit::MegaByte;

		if (ImGui::BeginTable("Memory table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoHostExtendX))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("VRAM Usage (MB)", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			// First table entry is current mem usage.
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Current usage");

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.1f", GetCurrentVRAMUsage(defaultMemoryUnit));
			}

			for (size_t i = 0; i < m_memoryEntries.size(); i++)
			{
				const MemoryEntry& memEntry = m_memoryEntries[i];

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", memEntry.name);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.2f", memEntry.thisVRAMUsage / (float)defaultMemoryUnit);
			}

			ImGui::EndTable();
		}
	}

	ImGui::End();
}

ProfileBlock::ProfileBlock(CommandContext& commandContext, const char* name)
{
	commandList = commandContext.GetCommandList();
	profileIndex = GPUProfiler::Get().StartPerformanceProfile(commandList, name);
}

ProfileBlock::~ProfileBlock()
{
	ASSERT(commandList != nullptr && profileIndex != uint32_t(-1));

	GPUProfiler::Get().EndPerformanceProfile(commandList, profileIndex);
}
