#include "rcpch.h"
#include "Core\GraphicsCore.h"
#include "RaytracingUtils.h"

using namespace Microsoft::WRL;


RaytracingPSO::RaytracingPSO(const wchar_t* Name)
{
	m_name = Name;
	m_desc = CD3DX12_STATE_OBJECT_DESC(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
}

void RaytracingPSO::Finalize()
{
	if(m_finalized)
	{
		LOG_ERROR(L"RaytracingPSO::Finalize() called multiple times for '{}'.", m_name);
		return;
	}

	D3D12_STATE_OBJECT_DESC* stateObjectDesc = const_cast<D3D12_STATE_OBJECT_DESC*>((const D3D12_STATE_OBJECT_DESC*)m_desc);

	HRESULT hr = Graphics::g_Device5->CreateStateObject(
		stateObjectDesc,
		IID_PPV_ARGS(m_stateObject.GetAddressOf())
	);
	
	if (FAILED(hr))
	{
		LOG_ERROR(L"Could not create state object '{}'.", m_name);
		ThrowIfFailed(hr, L"Failed to create state object.");
	}

	m_finalized = true;
}

void* RaytracingPSO::GetShaderIdentifier(const std::wstring& exportName)
{
	if (m_finalized == false)
	{
		LOG_ERROR(L"RaytracingPSO::GetShaderIdentifier() called before Finalize() for '{}'.", m_name);
		return nullptr;
	}

	ComPtr<ID3D12StateObjectProperties> stateObjectProperties = nullptr;
	ThrowIfFailed(m_stateObject->QueryInterface(IID_PPV_ARGS(stateObjectProperties.GetAddressOf())), L"Failed to get state object properties.");

	void* shaderIdentifier = stateObjectProperties->GetShaderIdentifier(exportName.c_str());

	if (shaderIdentifier == nullptr)
	{
		LOG_ERROR(L"Could not get shader identifier for export name '{}'.", exportName);
	}

	return shaderIdentifier;
}

void RootSignature1::Finalize(const std::wstring& name, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
	UINT numParameters = (UINT)m_params.size();
	UINT numSamplers = (UINT)m_samplers.size();

	D3D12_ROOT_PARAMETER1* params = numParameters > 0 ? m_params.data() : nullptr;
	D3D12_STATIC_SAMPLER_DESC* samplers = numSamplers > 0 ? m_samplers.data() : nullptr;

    auto versionedRootSigDesc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(
		numParameters,
		params,
		numSamplers,
		samplers,
		flags
	);
    
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&versionedRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
	if (FAILED(hr))
	{
		// Get the error message and output it.
		std::wstring errorMsgW = L"No error message.";
		if (error)
		{
			LOG_ERROR(L"Failed to serialize root signature.");
			std::string errorMsg = std::string((char*)error->GetBufferPointer(), error->GetBufferSize());
			errorMsgW = Utils::StringToWstring(errorMsg);
		}
		else
		{
			LOG_ERROR(L"Failed to serialize root signature (no error blob).");
		}

		ThrowIfFailed(hr, errorMsgW);
	}

	ThrowIfFailed(
		Graphics::g_Device5->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(m_signature.GetAddressOf())), 
		L"Failed to create root signature."
	);

	m_signature->SetName(name.c_str());
}

D3D12_DISPATCH_RAYS_DESC RaytracingDispatchRayInputs::BuildDispatchRaysDesc(UINT width, UINT height)
{
	D3D12_DISPATCH_RAYS_DESC desc = {};

	desc.HitGroupTable.StartAddress = m_hitGroupShaderTable.GetGpuVirtualAddress();
	desc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable.GetBufferSize();
	desc.HitGroupTable.StrideInBytes = m_hitGroupStride;

	desc.MissShaderTable.StartAddress = m_missShaderTable.GetGpuVirtualAddress();
	desc.MissShaderTable.SizeInBytes = m_missShaderTable.GetBufferSize();
	desc.MissShaderTable.StrideInBytes = desc.MissShaderTable.SizeInBytes; // Assumes only a single entry for miss ST.

	desc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable.GetGpuVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable.GetBufferSize();

	desc.Width = width;
	desc.Height = height;
	desc.Depth = 1;

	return desc;
}
