#pragma once

#include "Core\GpuBuffer.h"
#include "Core\PipelineState.h"
#include "Core\RootSignature.h"

// Raytracing entry data.
struct LocalHitData
{
	D3D12_GPU_DESCRIPTOR_HANDLE sourceTex;
	D3D12_GPU_DESCRIPTOR_HANDLE geometrySRV;
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

// This is a custom root parameter structure that uses CD3DX12_ROOT_PARAMETER1.
// The interface is largely inspired by the original RootParameter class in MiniEngine.
struct RootParameter1 : public CD3DX12_ROOT_PARAMETER1
{
	RootParameter1() : CD3DX12_ROOT_PARAMETER1() {}
	~RootParameter1()
	{
		Clear();
	}

	void Clear()
	{
		if (ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			delete[] DescriptorTable.pDescriptorRanges;
		}

		ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
	}
	
	void InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE type, UINT reg, UINT count, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL, UINT space = 0)
	{
		CD3DX12_DESCRIPTOR_RANGE1* range = new CD3DX12_DESCRIPTOR_RANGE1[count];
		range->Init(type, count, reg, space);

		InitAsDescriptorTable(1, range, visibility);
	}

	/*
		IMPORTANT THAT THIS STRUCT DOES NOT CONTAIN ANY EXTRA DATA AS 
		IT IS ASSUMED TO BE FULLY ALIGNED WITH D3D12_ROOT_PARAMETER1
	*/ 
};

// This implementation is largely adapted from the RootSignature class in MiniEngine (defined in RootSignature.h).
// Its purpose is to be updated to use the new D3D12_ROOT_PARAMETER1 and D3D12_STATIC_SAMPLER_DESC1 structures.
class RootSignature1
{
public:
	RootSignature1(UINT numParams = 0, UINT numSamplers = 0)
	{
		Reset(numParams, numSamplers);
	}

	void Reset(uint32_t numParams, uint32_t numSamplers)
	{
		m_params.clear();
		m_params.resize(numParams);

		m_samplers.clear();
		m_samplers.resize(numSamplers);

		m_initializedSamplers = 0;
	}
	

	ID3D12RootSignature* GetSignature() const
	{
		return m_signature.Get();
	}

	void Finalize(const std::wstring& name, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);
	void InitStaticSampler(uint32_t reg, const D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);
	
	RootParameter1& operator[](size_t entryIndex)
	{
		ASSERT(entryIndex < m_params.size());
		return m_params[entryIndex];
	}

	const RootParameter1& operator[](size_t entryIndex) const
	{
		ASSERT(entryIndex < m_params.size());
		return m_params[entryIndex];
	}

private:
	std::vector<RootParameter1> m_params;
	uint32_t m_initializedSamplers;
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_samplers;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_signature;
};

// Each member is a pointer to a CD3DX12 subobject. All initialzed as nullptr at first.
struct SubObjectPtrs
{
	CD3DX12_NODE_MASK_SUBOBJECT* nodeMask = nullptr;
	CD3DX12_HIT_GROUP_SUBOBJECT* hitGroup = nullptr;
	CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* shaderConfig = nullptr;
	CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pipelineConfig = nullptr;
	CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* localRootSignature = nullptr;
	CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* globalRootSignature = nullptr;
	CD3DX12_DXIL_LIBRARY_SUBOBJECT* dxilLibrary = nullptr;
};

class RaytracingPSO
{
public:
	RaytracingPSO(const wchar_t* Name = L"Unnamed Raytracing PSO");
	void Finalize();

	void SetNodeMask(UINT nodeMask)
	{
		GetOrCreate(m_subObjectPtrs.nodeMask)->SetNodeMask(nodeMask);
	}

	void SetMaxRayRecursionDepth(UINT maxRayRecursionDepth)
	{
		GetOrCreate(m_subObjectPtrs.pipelineConfig)->Config(maxRayRecursionDepth);
	}

	void SetGlobalRootSignature(RootSignature1* rootSig)
	{
		if (rootSig == nullptr)
			return;

		GetOrCreate(m_subObjectPtrs.globalRootSignature)->SetRootSignature(rootSig->GetSignature());
	}

	void SetLocalRootSignature(RootSignature1* rootSig)
	{
		if (rootSig == nullptr)
			return;

		GetOrCreate(m_subObjectPtrs.localRootSignature)->SetRootSignature(rootSig->GetSignature());
	}

	void SetDxilLibrary(const std::vector<std::wstring>& exportNames, const D3D12_SHADER_BYTECODE& shaderByteCode)
	{
		auto dxilLibrary = GetOrCreate(m_subObjectPtrs.dxilLibrary);
		// This effectively clears the library.
		*dxilLibrary = CD3DX12_DXIL_LIBRARY_SUBOBJECT();

		dxilLibrary->SetDXILLibrary(&shaderByteCode);

		std::vector<LPCWSTR> exportNamePtrs(exportNames.size());
		for (size_t i = 0; i < exportNames.size(); ++i)
		{
			exportNamePtrs[i] = exportNames[i].c_str();
		}

		dxilLibrary->DefineExports(exportNamePtrs.data(), (UINT)exportNames.size());
	}

	void SetPayloadAndAttributeSize(UINT payloadSize, UINT attributeSize)
	{
		GetOrCreate(m_subObjectPtrs.shaderConfig)->Config(payloadSize, attributeSize);
	}

	void SetHitGroup(const std::wstring& hitGroupName, const D3D12_HIT_GROUP_TYPE& hitGroupType = D3D12_HIT_GROUP_TYPE_TRIANGLES)
	{
		auto hitGroup = GetOrCreate(m_subObjectPtrs.hitGroup);
		hitGroup->SetHitGroupExport(hitGroupName.c_str());
		hitGroup->SetHitGroupType(hitGroupType);
	}

	void SetClosestHitShader(const std::wstring& shaderExportName)
	{
		auto hitGroup = GetOrCreate(m_subObjectPtrs.hitGroup);
		hitGroup->SetClosestHitShaderImport(shaderExportName.c_str());
	}

	void SetAnyHitShader(const std::wstring& shaderExportName)
	{
		auto hitGroup = GetOrCreate(m_subObjectPtrs.hitGroup);
		hitGroup->SetAnyHitShaderImport(shaderExportName.c_str());
	}

	void* GetShaderIdentifier(const std::wstring& exportName);
	Microsoft::WRL::ComPtr<ID3D12StateObject> GetStateObject() { return m_stateObject; }


private:

	template<typename T>
	T* GetOrCreate(T*& subobject)
	{
		if (subobject == nullptr)
		{
			subobject = m_desc.CreateSubobject<T>();
		}
		return subobject;
	}

	template<typename T>
	bool IsCreated(T*& subobject)
	{
		return subobject != nullptr;
	}

private:

	std::wstring m_name;
	SubObjectPtrs m_subObjectPtrs;
	CD3DX12_STATE_OBJECT_DESC m_desc;
	Microsoft::WRL::ComPtr<ID3D12StateObject> m_stateObject;
};

// This struct is adapted from the RaytracingDispatchRayInputs struct in ModelViewer.cpp.
struct RaytracingDispatchRayInputs
{
	RaytracingDispatchRayInputs() : m_hitGroupStride(0) {}

	template<typename T>
	RaytracingDispatchRayInputs(RaytracingPSO& rtPSO, ShaderTable<T>& hitShaderTable, const std::wstring& rayGenExport, const std::wstring& missShaderExport)
	{
		Init(rtPSO, hitShaderTable, rayGenExport, missShaderExport);
	}

	template<typename T>
	void Init(RaytracingPSO& rtPSO, ShaderTable<T>& hitShaderTable, const std::wstring& rayGenExport, const std::wstring& missShaderExport)
	{
		m_stateObject = rtPSO.GetStateObject();
		m_hitGroupStride = GetShaderTableEntrySize(hitShaderTable);

		m_hitGroupShaderTable.Create(
			L"Hit Shader Table",
			1,
			GetShaderTableSize(hitShaderTable),
			hitShaderTable.data()
		);

		ShaderTableEntrySimple rayGenShaderIdentifier = ShaderTableEntrySimple(rtPSO.GetShaderIdentifier(rayGenExport));
		ShaderTableSimple rayGenShaderTable = { {rayGenShaderIdentifier} };
		m_rayGenShaderTable.Create(
			L"Ray Gen Shader Table",
			1,
			GetShaderTableSimpleSize(rayGenShaderTable),
			rayGenShaderTable.data()
		);

		ShaderTableEntrySimple missShaderIdentifier = ShaderTableEntrySimple(rtPSO.GetShaderIdentifier(missShaderExport));
		ShaderTableSimple missShaderTable = { {missShaderIdentifier} };
		m_missShaderTable.Create(
			L"Miss Shader Table",
			1,
			GetShaderTableSimpleSize(missShaderTable),
			missShaderTable.data()
		);

	}

	D3D12_DISPATCH_RAYS_DESC BuildDispatchRaysDesc(UINT width, UINT height);

	Microsoft::WRL::ComPtr<ID3D12StateObject> m_stateObject;
	uint32_t m_hitGroupStride;
	ByteAddressBuffer m_rayGenShaderTable;
	ByteAddressBuffer m_missShaderTable;
	ByteAddressBuffer m_hitGroupShaderTable;
};

class AccelerationStructureBuffer : public ByteAddressBuffer
{
public:
	AccelerationStructureBuffer() : ByteAddressBuffer()
	{
		m_UsageState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}
};

struct AccelerationStructureData
{
	AccelerationStructureBuffer bvhBuffer;
	ByteAddressBuffer scratchBuffer;

	// Takes in the AS desc to get prebuild info and creates BVH and Scratch buffers from that data.
	void CreateBuffers(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& structDesc)
	{
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
		Graphics::g_Device5->GetRaytracingAccelerationStructurePrebuildInfo(&structDesc.Inputs, &prebuildInfo);

		scratchBuffer.Create(L"Scratch Buffer", (uint32_t)prebuildInfo.ScratchDataSizeInBytes, 1);
		bvhBuffer.Create(L"BVH Buffer", 1, (uint32_t)prebuildInfo.ResultDataMaxSizeInBytes);
	}
};

class BLASBuffer
{
public:
	BLASBuffer() = default;
	BLASBuffer(std::shared_ptr<Model> modelPtr, DescriptorHeap& descHeap);
	
	void Init(std::shared_ptr<Model> modelPtr, DescriptorHeap& descHeap);

	D3D12_GPU_VIRTUAL_ADDRESS GetBVH() const;
	DescriptorHandle GetGeometrySRV() const { return m_geometryDataSRVHandle; }
	uint32_t GetNumGeometries() const { return m_modelPtr->m_NumMeshes; }
	std::shared_ptr<const Model> GetModelPtr() const { return m_modelPtr; }

private:
	AccelerationStructureData m_asData;
	StructuredBuffer m_geometryInstanceData;

	DescriptorHandle m_geometryDataSRVHandle;
	std::shared_ptr<const Model> m_modelPtr;
};

struct TLASInstanceGroup
{
	const BLASBuffer* blasBuffer;
	std::vector<Utils::GPUMatrix> instanceTransforms;
};

class TLASBuffers
{
public:

	TLASBuffers() = default;
	TLASBuffers(const BLASBuffer& blas, const std::vector<TLASInstanceGroup>& instanceGroups);

	void Init(const std::vector<TLASInstanceGroup>& instanceGroups);

	D3D12_GPU_VIRTUAL_ADDRESS GetBVH() const;

private:

	void CreateInstanceDataBuffer(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& structDesc, uint32_t numInstances, D3D12_RAYTRACING_INSTANCE_DESC* descs);

private:
	AccelerationStructureData m_asData;
	ByteAddressBuffer m_instanceDataBuffer;
};