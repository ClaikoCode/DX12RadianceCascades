#pragma once

static const std::vector<std::wstring> s_DXILExports = { L"RayGenerationShader", L"AnyHitShader", L"ClosestHitShader", L"MissShader" };
static const std::wstring s_HitGroupName = L"HitGroup";

// Raytracing entry data.
struct LocalHitData
{
	D3D12_GPU_DESCRIPTOR_HANDLE geometrySRV;
	D3D12_GPU_DESCRIPTOR_HANDLE materialSRVs;
	uint32_t indexByteOffset;
	uint32_t vertexByteOffset;
};

// Only shader identifier.
struct alignas(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT) ShaderTableEntrySimple
{
	BYTE shaderIdentifierData[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];

	ShaderTableEntrySimple()
	{
		ZeroMemory((void*)shaderIdentifierData, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	};

	ShaderTableEntrySimple(const void* shaderIdentifier)
	{
		SetShaderIdentifier(shaderIdentifier);
	}

	void SetShaderIdentifier(const void* shaderIdentifier)
	{
		if (shaderIdentifier == nullptr)
		{
			LOG_ERROR(L"Shader identifier ptr is null. Cannot copy.");
			return;
		}

		memcpy(&shaderIdentifierData, shaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
	}
};

template<typename T>
struct alignas(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT) ShaderTableEntry : public ShaderTableEntrySimple
{
	T entryData;
};

template<typename T>
using ShaderTable = std::vector<ShaderTableEntry<T>>;

using ShaderTableSimple = std::vector<ShaderTableEntrySimple>;

// The two getter template functions below feels inefficient but will have to do for now.

template<typename T>
uint32_t GetShaderTableEntrySize(const ShaderTable<T>& shaderTable)
{
	return sizeof(ShaderTableEntry<T>);
}

template<typename T>
uint32_t GetShaderTableSize(const ShaderTable<T>& shaderTable)
{
	return (uint32_t)(sizeof(ShaderTableEntry<T>) * shaderTable.size());
}

inline uint32_t GetShaderTableSimpleSize(const ShaderTableSimple& shaderTable)
{
	return (uint32_t)(sizeof(ShaderTableEntrySimple) * shaderTable.size());
}