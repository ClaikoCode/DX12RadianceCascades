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
	ThrowIfFailedHR(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())));
	ThrowIfFailedHR(dxgiFactory->EnumAdapters(0, reinterpret_cast<IDXGIAdapter**>(m_vramAdapter.GetAddressOf())));

	D3D12_QUERY_HEAP_DESC queryDesc;
	queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryDesc.Count = MaxQueries;
	queryDesc.NodeMask = 0;

	ThrowIfFailedHR(Graphics::g_Device->CreateQueryHeap(
		&queryDesc,
		IID_PPV_ARGS(m_queryHeap.GetAddressOf())
	));

	// Each entry is a 64 bit timestamp value.
	m_queryResultBuffer.Create(L"GPUProfiler Readback buffer", MaxQueries, sizeof(uint64_t));
	m_queryHeapMemory = reinterpret_cast<uint64_t*>(m_queryResultBuffer.Map());

	// Create root of memory profile tree.
	MemoryProfileNode memProfileRoot = {};
	memProfileRoot.value.name = "Root";
	m_memoryRoot = std::make_shared<MemoryProfileNode>(memProfileRoot);
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
	ThrowIfFailedHR(m_vramAdapter->QueryVideoMemoryInfo(
		0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
		&videoMemoryInfo
	));

	return videoMemoryInfo.CurrentUsage;
}

void GPUProfiler::DrawMemoryProfileTree(std::shared_ptr<MemoryProfileNode> root, MemoryUnit defaultMemoryUnit)
{
	const char* defaultMemoryUnitString = sMemoryUnitNames[defaultMemoryUnit];

	static const char* formatString = "%.1f %s";
	static ImGuiTreeNodeFlags baseFlags =
		ImGuiTreeNodeFlags_DrawLinesToNodes |
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_Leaf |
		ImGuiTreeNodeFlags_Bullet;


	if (ImGui::TreeNodeEx(root->value.name, baseFlags))
	{
		uint64_t childrenVRAMSum = 0u;
		for (auto& child : root->children)
		{
			childrenVRAMSum += child->value.totalVRAM;
		}

		const float vramUsed = root->value.totalVRAM / (float)defaultMemoryUnit;
		ImGui::Text(formatString, vramUsed, defaultMemoryUnitString);

		if (!root->children.empty() && childrenVRAMSum != root->value.totalVRAM)
		{
			float memoryDiff = float((double(root->value.totalVRAM) - double(childrenVRAMSum)) / (float)defaultMemoryUnit);

			if (ImGui::TreeNodeEx("Unknown Source", baseFlags))
			{
				ImGui::Text(formatString, memoryDiff, defaultMemoryUnitString);
				ImGui::TreePop();
			}
		}

		for (auto& child : root->children)
		{
			DrawMemoryProfileTree(child);
		}

		ImGui::TreePop();
	}
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

std::shared_ptr<MemoryProfileNode> GPUProfiler::PushMemoryProfile(const char* name)
{
	if (m_memoryRootHead == nullptr)
	{
		m_memoryRootHead = m_memoryRoot;
	}

	auto memProfileNodeChild = std::make_shared<MemoryProfileNode>();

	memProfileNodeChild->parent = m_memoryRootHead;
	memProfileNodeChild->value = {
		.name = name,
		.currentVRAMUsage = GetCurrentVRAMUsageBytes()
	};

	m_memoryRootHead->children.push_back(memProfileNodeChild);
	m_memoryRootHead = memProfileNodeChild;

	return memProfileNodeChild;
}

void GPUProfiler::PopMemoryProfile(std::shared_ptr<MemoryProfileNode> memProfileNode)
{
	ASSERT(memProfileNode != nullptr);

	memProfileNode->value.totalVRAM = GetCurrentVRAMUsageBytes() - memProfileNode->value.currentVRAMUsage;

	m_memoryRootHead = memProfileNode->parent;
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
		perfProfile.currentSampleCount = perfProfile.currentSampleCount % MaxFrametimeSampleCount;
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
		// Used for dynamic text formatting.
		int longestProfileName = 0;
		const size_t longestAllowedLength = 40;
		for (PerfProfile& perfProfile : m_profiles)
		{
			if (perfProfile.name != nullptr)
			{
				int profileNameLength = int(strnlen_s(perfProfile.name, longestAllowedLength));

				if (profileNameLength > longestProfileName)
				{
					longestProfileName = profileNameLength;
				}
			}
		}

		for (PerfProfile& perfProfile : m_profiles)
		{
			if (perfProfile.name == nullptr)
			{
				continue;
			}

			// Moved by MaxSampleCount to avoid underflow if current sample count is 0.
			const uint32_t lastSampleIndex = (perfProfile.currentSampleCount + MaxFrametimeSampleCount - 1) % MaxFrametimeSampleCount;
			float lastSampleTime = perfProfile.timeSamples[lastSampleIndex];

			float timeSum = 0.0f;
			for (float timeSample : perfProfile.timeSamples)
			{
				timeSum += timeSample;
			}
			float averageTime = timeSum / perfProfile.timeSamples.size();

			char formattedPlotName[128] = { 0 };
			sprintf_s(formattedPlotName, "%-*s (% 6.2f ms | Avg: % 6.2f ms)", longestProfileName, perfProfile.name, lastSampleTime, averageTime);

			ImGui::PlotLines(
				formattedPlotName,
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

		ASSERT(m_memoryRoot != nullptr && m_memoryRoot == m_memoryRootHead);

		// Skip drawing from root as it its only purpose is to begin building the tree.
		for (auto& child : m_memoryRoot->children)
		{
			DrawMemoryProfileTree(child);
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
	targetMemProfileNode = GPUProfiler::Get().PushMemoryProfile(name);
}

MemProfileBlock::~MemProfileBlock()
{
	ASSERT(targetMemProfileNode != nullptr);

	GPUProfiler::Get().PopMemoryProfile(targetMemProfileNode);
}
