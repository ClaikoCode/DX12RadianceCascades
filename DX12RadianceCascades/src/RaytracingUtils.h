#pragma once

#include "Core\GpuBuffer.h"
#include "Core\PipelineState.h"
#include "Core\RootSignature.h"

struct RaytracingDispatchRayInputs
{
	RaytracingDispatchRayInputs() : m_hitGroupStride(0) {}

	D3D12_DISPATCH_RAYS_DESC GetDispatchRaysDesc();

	uint32_t m_hitGroupStride;
	ByteAddressBuffer m_rayGenShaderTable;
	ByteAddressBuffer m_missShaderTable;
	ByteAddressBuffer m_hitGroupShaderTable;
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
	}

	ID3D12RootSignature* GetSignature() const
	{
		return m_signature.Get();
	}

	void Finalize(const std::wstring& name, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);
	
	CD3DX12_ROOT_PARAMETER1& operator[](size_t entryIndex)
	{
		ASSERT(entryIndex < m_params.size());
		return m_params[entryIndex];
	}

	const CD3DX12_ROOT_PARAMETER1& operator[](size_t entryIndex) const
	{
		ASSERT(entryIndex < m_params.size());
		return m_params[entryIndex];
	}

private:
	std::vector<CD3DX12_ROOT_PARAMETER1> m_params;
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
	std::vector<CD3DX12_DXIL_LIBRARY_SUBOBJECT*> dxilLibraries = {nullptr};
};

class RaytracingPSO
{
public:
	RaytracingPSO(const wchar_t* Name = L"Unnamed Raytracing PSO");
	void Finalize();

	void SetNodeMask(UINT nodeMask) // This function is called without template
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

	void SetDxilLibrary(const std::wstring& exportName, const D3D12_SHADER_BYTECODE& shaderByteCode)
	{
		auto dxilLibrary = GetOrCreate(m_subObjectPtrs.dxilLibraries.emplace_back());
		dxilLibrary->SetDXILLibrary(&shaderByteCode);
		dxilLibrary->DefineExport(exportName.c_str());
	}

	void SetPayloadAndAttributeSize(UINT payloadSize, UINT attributeSize)
	{
		GetOrCreate(m_subObjectPtrs.shaderConfig)->Config(payloadSize, attributeSize);
	}

	void SetHitGroup(const std::wstring& exportName, const std::wstring& hitGroupName, const D3D12_HIT_GROUP_TYPE& hitGroupType = D3D12_HIT_GROUP_TYPE_TRIANGLES)
	{
		auto hitGroup = GetOrCreate(m_subObjectPtrs.hitGroup);
		hitGroup->SetHitGroupExport(exportName.c_str());
		hitGroup->SetHitGroupType(hitGroupType);
		hitGroup->SetClosestHitShaderImport(hitGroupName.c_str());
	}

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

private:

	std::wstring m_name;
	SubObjectPtrs m_subObjectPtrs;
	CD3DX12_STATE_OBJECT_DESC m_desc;
	Microsoft::WRL::ComPtr<ID3D12StateObject> m_stateObject;
};