#include "rcpch.h"
#include "Core\DepthBuffer.h"
#include "Core\GraphicsCore.h"

#include "GPUStructs.h"
#include "RadianceCascadeManager3D.h"
#include "RuntimeResourceManager.h"

#include "AppGUI\AppGUI.h"

constexpr DXGI_FORMAT DefaultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// Using R8_UINT instead of R8_UNORM for these textures (and corrected shader code) has the same performance (down to 0.01 ms).
// I chose R8_UNORM to better work with shaders that expect floating point texel samples, like my debug visualziation shaders.
// TODO: Because gather filtering only checks for 0 or 1, there could exist a solution where only certain bits is modified of each texel but I worry for
// data races as I am doubtful that concurrently modifying bits inside of the same word is possible.
constexpr DXGI_FORMAT DefaultGatherFilterFormat = DXGI_FORMAT_R8_UNORM;

namespace
{
	uint64_t GetResourceVRAMSize(GpuResource& resource, ID3D12Device* device)
	{
		ASSERT(device != nullptr);

		D3D12_RESOURCE_DESC desc = resource.GetResource()->GetDesc();

		D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device->GetResourceAllocationInfo(0, 1, &desc);
		
		if (allocInfo.SizeInBytes == UINT64_MAX)
		{
			// If the size is not available, return 0.
			LOG_WARNING(L"Cannot determine VRAM size for resource, returning 0. Check that resource is valid.");
			return 0;
		}

		return allocInfo.SizeInBytes;
	}
}

void RadianceCascadeManager3D::Generate(uint32_t raysPerProbe0, uint32_t probeSpacing0, uint32_t swapchainWidth, uint32_t swapchainHeight, uint32_t maxAllowedCascadeLevels /*= 8*/)
{
	// Make sure no work is on the GPU before generating.
	Graphics::g_CommandManager.IdleGPU();

	// Probe counts are rounded down to the nearest integer.
	// If they are rounded up, probes in higher cascades would be placed far outside the screen.
	// Probe indecies are clamped to the nearest edge either way but flooring this count makes it more manageable and there will 
	// only be slight stretching on screen edges that should not be very noticeable unless pointed out.
	const uint32_t probeCount0X = uint32_t(Math::Floor(swapchainWidth / (float)probeSpacing0));
	const uint32_t probeCount0Y = uint32_t(Math::Floor(swapchainHeight / (float)probeSpacing0));

	const uint32_t minCount = probeCount0Y < probeCount0X ? probeCount0Y : probeCount0X;
	uint32_t maxCalculatedCascadeLevels = uint32_t(Math::Floor(Math::LogAB((float)m_scalingFactor.probeScalingFactor, (float)minCount)));
	if (maxCalculatedCascadeLevels > maxAllowedCascadeLevels)
	{
		maxCalculatedCascadeLevels = maxAllowedCascadeLevels;
	}

	m_cascadeIntervals.resize(maxCalculatedCascadeLevels);
	m_cascadeGatherFilters.resize(maxCalculatedCascadeLevels - 1);

	uint32_t raysPerProbe = raysPerProbe0;
	ProbeDims probeDims = { probeCount0X, probeCount0Y };
	for (uint32_t i = 0; i < maxCalculatedCascadeLevels; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);
		std::wstring cascadeFilterName = cascadeName + L" Gather Filter";

		// If pre averaged intervals are used, each original ray will average the rays that would be averaged into the lower cascades.
		// This means that each dispatched pixel, representing a probe and a direciton, will already account for the information of rays equal to rayScalingFactor.
		uint32_t raysPerProbeDim = m_rcSettings.staticParams.isUsingPreAveragedIntervals ? (uint32_t)sqrt(raysPerProbe / m_scalingFactor.rayScalingFactor) : (uint32_t)sqrt(raysPerProbe);
		
		const uint32_t probeBufferWidth = probeDims.probesX * raysPerProbeDim;
		const uint32_t probeBufferHeight = probeDims.probesY * raysPerProbeDim;

		// Clear color has alpha of 0.0, indicating that each cascade assumes that its rays are obscured.
		m_cascadeIntervals[i].SetClearColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
		m_cascadeIntervals[i].Create(cascadeName, probeBufferWidth, probeBufferHeight, 1, DefaultFormat);

		if (i > 0)
		{
			uint32_t filterIndex = i - 1;

			m_cascadeGatherFilters[filterIndex].SetClearColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
			m_cascadeGatherFilters[filterIndex].Create(cascadeFilterName, probeBufferWidth, probeBufferHeight, 1, DefaultGatherFilterFormat);
		}

		probeDims.probesX /= m_scalingFactor.probeScalingFactor;
		probeDims.probesY /= m_scalingFactor.probeScalingFactor;
		raysPerProbe *= m_scalingFactor.rayScalingFactor;
	}

	// Coalesced result has one pixel per probe0.
	m_coalescedResult.Create(
		L"Coalesced Result",
		probeCount0X,
		probeCount0Y,
		1,
		DefaultFormat
	);

	uint32_t numElements = GetGatherFilterCount();
	uint32_t elementSize = sizeof(uint32_t);
	m_gatherFilterByteAddressBuffer.Create(L"Gather Filter Reduction Sum Byte Buffer", numElements, elementSize);
	m_gatherFilterReadbackBuffer.Create(L"Gather Filter Reduction Sum Readback Buffer", numElements, elementSize);
	m_gatherFilterReadbackBufferMappedPtr = reinterpret_cast<uint32_t*>(m_gatherFilterReadbackBuffer.Map());

	UpdateResourceDescriptors();

	m_probeCount0X = probeCount0X;
	m_probeCount0Y = probeCount0Y;

	m_rcSettings.staticParams.probeSpacing0 = probeSpacing0;
	m_rcSettings.staticParams.raysPerProbe0 = raysPerProbe0;

	m_swapchainWidth = swapchainWidth;
	m_swapchainHeight = swapchainHeight;
}

void RadianceCascadeManager3D::Resize(uint32_t width, uint32_t height)
{
	Generate(m_rcSettings.staticParams.raysPerProbe0, m_rcSettings.staticParams.probeSpacing0, width, height, GetCascadeIntervalCount());
}

void RadianceCascadeManager3D::FillRCGlobalInfo(RCGlobals& rcGlobalInfo)
{
	rcGlobalInfo.rayCount0 = m_rcSettings.staticParams.raysPerProbe0;
	rcGlobalInfo.rayLength0 = m_rcSettings.rayLength0;

	rcGlobalInfo.probeScalingFactor = m_scalingFactor.probeScalingFactor;
	rcGlobalInfo.rayScalingFactor = m_scalingFactor.rayScalingFactor;
	
	rcGlobalInfo.cascadeCount = GetCascadeIntervalCount();
	rcGlobalInfo.gatherFilterCount = rcGlobalInfo.cascadeCount - 1; // By definition.
	

	rcGlobalInfo.usePreAveraging = m_rcSettings.staticParams.isUsingPreAveragedIntervals;
	rcGlobalInfo.depthAwareMerging = m_rcSettings.useDepthAwareMerging;

	rcGlobalInfo.probeCount0X = m_probeCount0X;
	rcGlobalInfo.probeCount0Y = m_probeCount0Y;
	rcGlobalInfo.probeSpacing0 = m_rcSettings.staticParams.probeSpacing0;

	rcGlobalInfo.useGatherFiltering = m_rcSettings.useGatherFiltering;
}

void RadianceCascadeManager3D::ClearBuffers(GraphicsContext& gfxContext)
{
	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		gfxContext.TransitionResource(cascadeInterval, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.ClearColor(cascadeInterval);
	}

	for (ColorBuffer& cascadeGatherFilter : m_cascadeGatherFilters)
	{
		gfxContext.TransitionResource(cascadeGatherFilter, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.ClearColor(cascadeGatherFilter);
	}

	gfxContext.TransitionResource(m_coalescedResult, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.ClearColor(m_coalescedResult);

	std::vector<uint32_t> zeroVec = std::vector<uint32_t>(GetGatherFilterCount(), 0u);

	gfxContext.TransitionResource(m_gatherFilterByteAddressBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	gfxContext.WriteBuffer(m_gatherFilterByteAddressBuffer, 0u, zeroVec.data(), sizeof(zeroVec[0]) * zeroVec.size());
}

uint32_t RadianceCascadeManager3D::GetRaysPerProbe(uint32_t cascadeIndex)
{
	return m_rcSettings.staticParams.raysPerProbe0 * (uint32_t)Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}

uint32_t RadianceCascadeManager3D::GetProbeCount(uint32_t cascadeIndex)
{
	ProbeDims probeDims = GetProbeDims(cascadeIndex);
	return probeDims.probesX * probeDims.probesY;
}

ProbeDims RadianceCascadeManager3D::GetProbeDims(uint32_t cascadeIndex)
{
	const uint32_t dividend = uint32_t(Math::Pow((float)m_scalingFactor.probeScalingFactor, (float)cascadeIndex));

	ProbeDims probeDims;
	probeDims.probesX = m_probeCount0X / dividend;
	probeDims.probesY = m_probeCount0Y / dividend;

	return probeDims;
}

float RadianceCascadeManager3D::GetStartT(uint32_t cascadeIndex)
{
	float startT = Math::GeometricSeriesSum(GetRayLength(0), (float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
	return startT == -0.0f ? -startT : startT;
}

float RadianceCascadeManager3D::GetRayLength(uint32_t cascadeIndex)
{
	return m_rcSettings.rayLength0 * Math::Pow((float)m_scalingFactor.rayScalingFactor, (float)cascadeIndex);
}

ColorBuffer& RadianceCascadeManager3D::GetCascadeIntervalBuffer(uint32_t cascadeIndex) 
{ 
	ASSERT(cascadeIndex < GetCascadeIntervalCount()); return m_cascadeIntervals[cascadeIndex]; 
}

ColorBuffer& RadianceCascadeManager3D::GetCascadeGatherFilterBuffer(uint32_t filterIndex) 
{ 
	ASSERT(filterIndex < GetGatherFilterCount()); return m_cascadeGatherFilters[filterIndex]; 
}

uint64_t RadianceCascadeManager3D::GetTotalVRAMUsage()
{
	uint64_t totalSize = 0;

	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		totalSize += GetResourceVRAMSize(cascadeInterval, Graphics::g_Device);
	}

	totalSize += GetResourceVRAMSize(m_coalescedResult, Graphics::g_Device);

	return totalSize;
}

uint32_t RadianceCascadeManager3D::GetFilteredRayCount(uint32_t filterIndex)
{
	ASSERT(m_gatherFilterReadbackBufferMappedPtr != nullptr && filterIndex < GetGatherFilterCount());

	return m_gatherFilterReadbackBufferMappedPtr[filterIndex];
}

void RadianceCascadeManager3D::DrawRCSettingsUI()
{
	RC3DSettings::StaticParameters oldStaticParams = m_rcSettings.staticParams;

	ImGui::Separator();

	ImGui::Checkbox("Use Depth Aware Merging (WIP)", &m_rcSettings.useDepthAwareMerging);
	ImGui::Checkbox("Use Gather Filtering", &m_rcSettings.useGatherFiltering);

	ImGui::Checkbox("Use Pre Average Intervals", &m_rcSettings.staticParams.isUsingPreAveragedIntervals);

	ImGui::SliderFloat("Ray Length", &m_rcSettings.rayLength0, 0.1f, 250.0f);

	// Rays per probe settings
	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Rays per probe 0:");
		ImGui::SameLine();

		int& raysperProbe0 = m_rcSettings.staticParams.raysPerProbe0;
		ImGui::RadioButton("4", &raysperProbe0, 4); ImGui::SameLine();
		ImGui::RadioButton("16", &raysperProbe0, 16); ImGui::SameLine();
		ImGui::RadioButton("36", &raysperProbe0, 36); ImGui::SameLine();
		ImGui::RadioButton("64", &raysperProbe0, 64);
	}

	// Probe spacing settings
	{
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.2f);
		int& probeSpacing0 = m_rcSettings.staticParams.probeSpacing0;
		if (ImGui::InputInt("Probe Spacing [1 - 16]", &probeSpacing0, 1, 0))
		{
			probeSpacing0 = int(Math::Clamp(float(probeSpacing0), 1.0f, 16.0f));
		}
	}

	// Max cascade count settings
	{
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.2f);
		int& maxCascadeCount = m_rcSettings.staticParams.maxCascadeCount;
		if (ImGui::InputInt("Max Cascade Count [1 - 10]", &maxCascadeCount, 1, 0))
		{
			maxCascadeCount = int(Math::Clamp(float(maxCascadeCount), 1.0f, 10.0f));
		}
	}

	// If static parameters change, a rebuild is required.
	if (memcmp(&oldStaticParams, &m_rcSettings.staticParams, sizeof(oldStaticParams)) != 0)
	{
		Generate(
			m_rcSettings.staticParams.raysPerProbe0,
			m_rcSettings.staticParams.probeSpacing0,
			m_swapchainWidth,
			m_swapchainHeight,
			m_rcSettings.staticParams.maxCascadeCount
		);
	}

	const ColorBuffer& cascade0Buffer = GetCascadeIntervalBuffer(0);
	const uint32_t cascadeResolutionWidth = cascade0Buffer.GetWidth();
	const uint32_t cascadeResolutionHeight = cascade0Buffer.GetHeight();

	ImGui::Text("Cascade Count: %u", GetCascadeIntervalCount());
	ImGui::Text("Using pre-averaging: %s", UsesPreAveragedIntervals() ? "Yes" : "No");
	uint64_t rcVRAMUsage = GetTotalVRAMUsage();
	ImGui::Text("Vram usage: %.1f MB", rcVRAMUsage / (float)(1024 * 1024));

	// Create a table with 6 columns: Cascade, Buffer Resolution, Probe Count, Ray Count, Start Dist, Length
	uint32_t cascadeTableHeaderCount = 6;
	// One extra on the end if gather filtering is used.
	if (m_rcSettings.useGatherFiltering) { cascadeTableHeaderCount++; }

	if (ImGui::BeginTable("CascadeTable", cascadeTableHeaderCount, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoHostExtendX))
	{
		ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Buffer Resolution", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Probe Count", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Rays Per Probe", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Ray Start Distance", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Ray Length", ImGuiTableColumnFlags_WidthFixed);
		if (m_rcSettings.useGatherFiltering) { ImGui::TableSetupColumn("Filtered Rays", ImGuiTableColumnFlags_WidthFixed); }
		ImGui::TableHeadersRow();

		// Add a row for each cascade
		for (unsigned int i = 0; i < GetCascadeIntervalCount(); i++) {
			ImGui::TableNextRow();

			// Cascade column
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%u", i);

			// Cascade Resolution column
			const ColorBuffer& cascadeBuffer = GetCascadeIntervalBuffer(i);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%u x %u", cascadeBuffer.GetWidth(), cascadeBuffer.GetHeight());

			// Probe Count column
			//uint32_t cascadeCountPerDim = m_rcManager3D.GetProbeCountPerDim(i);
			ProbeDims cascadeCountPerDim = GetProbeDims(i);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u x %u", cascadeCountPerDim.probesX, cascadeCountPerDim.probesY);

			// Rays Per Probe column
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u", GetRaysPerProbe(i));

			// Ray Start Distance column
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.1f", GetStartT(i));

			// Ray Length column
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.1f", GetRayLength(i));

			if (m_rcSettings.useGatherFiltering)
			{
				ImGui::TableSetColumnIndex(6);
				if (i == 0)
				{
					ImGui::Text("n/a");
				}
				else
				{
					int gatherFilterIndex = i - 1;
					ColorBuffer& gatherFilterTexture = GetCascadeGatherFilterBuffer(gatherFilterIndex);
					uint32_t gatherFilterReductionSum = GetFilteredRayCount(gatherFilterIndex);

					uint32_t totalRays = gatherFilterTexture.GetWidth() * gatherFilterTexture.GetHeight();
					uint32_t filteredRayCount = totalRays - gatherFilterReductionSum;

					ImGui::Text("%u (%.2f%%)", filteredRayCount, 100.0f * filteredRayCount / totalRays);
				}
			}
		}

		ImGui::EndTable();
	}
}

void RadianceCascadeManager3D::UpdateResourceDescriptors()
{
	for (size_t i = 0; i < m_cascadeIntervals.size(); i++)
	{
		RuntimeResourceManager::UpdateDescriptor(m_cascadeIntervals[i].GetSRV());
		RuntimeResourceManager::UpdateDescriptor(m_cascadeIntervals[i].GetUAV());
	}

	for (size_t i = 0; i < m_cascadeGatherFilters.size(); i++)
	{
		RuntimeResourceManager::UpdateDescriptor(m_cascadeGatherFilters[i].GetSRV());
		RuntimeResourceManager::UpdateDescriptor(m_cascadeGatherFilters[i].GetUAV());
	}

	RuntimeResourceManager::UpdateDescriptor(m_coalescedResult.GetSRV());
	RuntimeResourceManager::UpdateDescriptor(m_coalescedResult.GetUAV());

	RuntimeResourceManager::UpdateDescriptor(m_gatherFilterByteAddressBuffer.GetUAV());
}
