#include "rcpch.h"
#include "GPUProfiler.h"

#include <unordered_map>
#include "AppGUI\AppGUI.h"

using namespace Microsoft::WRL;

static std::unordered_map<MemoryUnit, const char*> sMemoryUnitNames = {
	{Byte, "Bytes"},
	{KiloByte, "KB"},
	{MegaByte, "MB"},
	{GigaByte, "GB"}
};

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
	MemoryProfile memProfile = {};
	memProfile.name = name;
	memProfile.treeDepth = uint16_t(m_memoryFrames.size());
	m_memoryProfiles.push_back(memProfile);

	m_memoryFrames.push_back({
		GetCurrentVRAMUsageBytes(),
		uint32_t(m_memoryProfiles.size() - 1)
	});
}

void GPUProfiler::PopMemoryEntry()
{
	const MemoryFrame& memFrame = m_memoryFrames.back();

	// Update how much VRAM has been allocated between push and pop.
	m_memoryProfiles[memFrame.profileIndex].totalVRAM = GetCurrentVRAMUsageBytes() - memFrame.currentVRAMUsage;

	m_memoryFrames.pop_back();
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

void GPUProfiler::UpdateData(uint64_t timestampFrequency)
{
	UpdatePerformanceProfiles(timestampFrequency);
}

void GPUProfiler::DrawProfilerUI()
{
	ImGui::Begin("Performance Statistics");

	if(ImGui::CollapsingHeader("Frametime", ImGuiTreeNodeFlags_DefaultOpen))
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
			sprintf_s(msValue, " (%5.2f ms | Avg: %5.2f ms)", lastSampleTime, averageTime);
			std::string plotName = std::string(perfProfile.name) + msValue;
			ImGui::PlotLines(
				plotName.c_str(),
				perfProfile.timeSamples.data(),
				(int)perfProfile.timeSamples.size(),
				perfProfile.currentSampleCount,
				"",
				0.0f,
				15.0f,
				ImVec2(350.0f, 30.0f)
			);
		}
	}

	if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const MemoryUnit defaultMemoryUnit = MemoryUnit::MegaByte;
		const char* defaultMemoryUnitString = sMemoryUnitNames[defaultMemoryUnit];

		static ImGuiTreeNodeFlags baseFlags = 
			ImGuiTreeNodeFlags_DrawLinesToNodes |
			ImGuiTreeNodeFlags_DefaultOpen | 
			ImGuiTreeNodeFlags_Leaf | 
			ImGuiTreeNodeFlags_Bullet;

		uint32_t currentDepth = 0u;
		const char* formatString = "%.1f %s";
		for (int i = 0; i < m_memoryProfiles.size(); i++)
		{
			const MemoryProfile& memProfile = m_memoryProfiles[i];
			float vramUsed = memProfile.totalVRAM / (float)defaultMemoryUnit;

			if (i < (m_memoryProfiles.size() - 1))
			{
				const MemoryProfile& nextMemProfile = m_memoryProfiles[i + 1];
				const uint16_t nextDepth = nextMemProfile.treeDepth;

				if (nextDepth > currentDepth)
				{
					if (ImGui::TreeNodeEx(memProfile.name, baseFlags))
					{
						ImGui::Text(formatString, vramUsed, defaultMemoryUnitString);
					}
				}
				else
				{
					if (ImGui::TreeNodeEx(memProfile.name, baseFlags))
					{
						ImGui::Text(formatString, vramUsed, defaultMemoryUnitString);
						ImGui::TreePop();
					}

					if (nextDepth < currentDepth)
					{
						int depthDiff = currentDepth - nextDepth;

						for (int i = 0; i < depthDiff; i++)
						{
							ImGui::TreePop();
						}
					}
				}

				currentDepth = nextDepth;
				continue;
			}
			else
			{
				if (ImGui::TreeNodeEx(memProfile.name, baseFlags))
				{
					ImGui::Text(formatString, vramUsed, defaultMemoryUnitString);
					ImGui::TreePop();
				}
			}

			for (uint32_t i = 0; i < currentDepth; i++)
			{
				ImGui::TreePop();
			}
		}
	}

	ImGui::End();
}

PerfProfileBlock::PerfProfileBlock(CommandContext& commandContext, const char* name)
{
	commandList = commandContext.GetCommandList();
	profileIndex = GPUProfiler::Get().StartPerformanceProfile(commandList, name);
}

PerfProfileBlock::~PerfProfileBlock()
{
	ASSERT(commandList != nullptr && profileIndex != uint32_t(-1));

	GPUProfiler::Get().EndPerformanceProfile(commandList, profileIndex);
}

MemProfileBlock::MemProfileBlock(const char* name)
{
	GPUProfiler::Get().PushMemoryEntry(name);
}

MemProfileBlock::~MemProfileBlock()
{
	GPUProfiler::Get().PopMemoryEntry();
}
