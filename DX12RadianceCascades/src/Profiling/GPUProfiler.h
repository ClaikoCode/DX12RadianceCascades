#pragma once

#include <array>
#include <stack>

#include "Core\ReadbackBuffer.h"
#include "Core\CommandContext.h"

constexpr uint32_t MaxProfiles = 16u;
constexpr uint32_t MaxQueries = MaxProfiles * 2; // Two queries per profile.

constexpr uint32_t MaxSampleCount = 256u;

#if defined(_DEBUG)
	#define GPU_PROFILE_BLOCK(name, context) ProfileBlock profileBlock(context, name)
#else
	#define GPU_PROFILE_BLOCK(name, context)
#endif

enum MemoryUnit : unsigned int
{
	Byte = 1u,
	KiloByte = Byte * 1024u,
	MegaByte = KiloByte * 1024u,
	GigaByte = MegaByte * 1024u
};

struct PerfProfile
{
	const char* name = nullptr;

	bool isQuerying = false;

	// Circular buffer.
	std::array<float, MaxSampleCount> timeSamples = {};
	uint32_t currentSampleCount = 0u;
};

struct MemoryEntry
{
	const char* name = nullptr;
	uint64_t currentVRAMUsage = 0u; // In bytes
	uint64_t thisVRAMUsage = 0u; // In bytes
};

// Singleton class
class GPUProfiler
{
public:

	static GPUProfiler& Get()
	{
		static GPUProfiler gpuProfiler = GPUProfiler();

		return gpuProfiler;
	}

	static void Initialize()
	{
		Get();
	}

	static void Destroy()
	{
		Get().DestroyImpl();
	}

public:

	// Will return a profile index.
	uint32_t StartPerformanceProfile(ID3D12GraphicsCommandList* commandList, const char* name);
	void EndPerformanceProfile(ID3D12GraphicsCommandList* commandList, uint32_t profileIndex);

	void PushMemoryEntry(const char* name);
	void PopMemoryEntry();

	float GetCurrentVRAMUsage(MemoryUnit memoryUnit = MemoryUnit::MegaByte);


	void UpdatePerformanceProfiles(uint64_t timestampFrequency);
	void UpdateMemoryEntries();

	void UpdateData(uint64_t timestampFrequency);
	void DrawProfilerUI();


private:

	GPUProfiler();

	void DestroyImpl();

	uint64_t GetCurrentVRAMUsageBytes();

private:

	std::array<PerfProfile, MaxProfiles> m_profiles;
	uint32_t m_profileCount = 0;
	
	std::vector<MemoryEntry> m_memoryEntries;
	
	Microsoft::WRL::ComPtr<IDXGIAdapter3> m_vramAdapter = nullptr;
	// Each query profile reserves two consecutive entries. One for start and one for end.
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap = nullptr;
	ReadbackBuffer m_queryResultBuffer;
	// Mapped memoryptr to result buffer.
	uint64_t* m_queryHeapMemory = nullptr;
};

struct ProfileBlock
{
	ProfileBlock(CommandContext& commandContext, const char* name);
	~ProfileBlock();

	ID3D12GraphicsCommandList* commandList = nullptr;
	uint32_t profileIndex = uint32_t(-1);
};

