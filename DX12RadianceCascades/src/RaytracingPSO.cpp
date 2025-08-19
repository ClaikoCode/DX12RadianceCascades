#include "rcpch.h"
#include "Core\GraphicsCore.h"
#include "RaytracingPSO.h"

using namespace Microsoft::WRL;


RaytracingPSO::RaytracingPSO(const wchar_t* Name)
{
	m_name = Name;
	m_desc = CD3DX12_STATE_OBJECT_DESC(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
}

void RaytracingPSO::Finalize()
{
	// Releases the previous state object.
	m_stateObject = nullptr;

	D3D12_STATE_OBJECT_DESC* stateObjectDesc = const_cast<D3D12_STATE_OBJECT_DESC*>((const D3D12_STATE_OBJECT_DESC*)m_desc);

	HRESULT hr = Graphics::g_Device5->CreateStateObject(
		stateObjectDesc,
		IID_PPV_ARGS(m_stateObject.GetAddressOf())
	);
	
	if (FAILED(hr))
	{
		LOG_ERROR(L"Could not create state object '{}'.", m_name);
		ThrowIfFailedHR(hr, L"Failed to create state object.");
	}

	m_stateObject.Get()->SetName(m_name.c_str());
}

void* RaytracingPSO::GetShaderIdentifier(const std::wstring& exportName)
{
	if (m_stateObject == nullptr)
	{
		LOG_ERROR(L"RaytracingPSO::GetShaderIdentifier() called before Finalize() for '{}'.", m_name);
		return nullptr;
	}

	ComPtr<ID3D12StateObjectProperties> stateObjectProperties = nullptr;
	ThrowIfFailedHR(m_stateObject->QueryInterface(IID_PPV_ARGS(stateObjectProperties.GetAddressOf())), L"Failed to get state object properties.");

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
	ASSERT(numSamplers == m_initializedSamplers);

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

		ThrowIfFailedHR(hr, errorMsgW);
	}

	ThrowIfFailedHR(
		Graphics::g_Device5->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(m_signature.GetAddressOf())), 
		L"Failed to create root signature."
	);

	m_signature->SetName(name.c_str());
}

void RootSignature1::InitStaticSampler(uint32_t reg, const D3D12_SAMPLER_DESC& samplerDesc, D3D12_SHADER_VISIBILITY visibility)
{
	// The code below is adapted from the InitStaticSampler inside RootSignature.cpp in MiniEngine.

	ASSERT(m_initializedSamplers < m_samplers.size());
	D3D12_STATIC_SAMPLER_DESC& StaticSamplerDesc = m_samplers[m_initializedSamplers++];

	StaticSamplerDesc.Filter = samplerDesc.Filter;
	StaticSamplerDesc.AddressU = samplerDesc.AddressU;
	StaticSamplerDesc.AddressV = samplerDesc.AddressV;
	StaticSamplerDesc.AddressW = samplerDesc.AddressW;
	StaticSamplerDesc.MipLODBias = samplerDesc.MipLODBias;
	StaticSamplerDesc.MaxAnisotropy = samplerDesc.MaxAnisotropy;
	StaticSamplerDesc.ComparisonFunc = samplerDesc.ComparisonFunc;
	StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	StaticSamplerDesc.MinLOD = samplerDesc.MinLOD;
	StaticSamplerDesc.MaxLOD = samplerDesc.MaxLOD;
	StaticSamplerDesc.ShaderRegister = reg;
	StaticSamplerDesc.RegisterSpace = 0;
	StaticSamplerDesc.ShaderVisibility = visibility;

	if (StaticSamplerDesc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		StaticSamplerDesc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		StaticSamplerDesc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	{
		WARN_ONCE_IF_NOT(
			// Transparent Black
			samplerDesc.BorderColor[0] == 0.0f &&
			samplerDesc.BorderColor[1] == 0.0f &&
			samplerDesc.BorderColor[2] == 0.0f &&
			samplerDesc.BorderColor[3] == 0.0f ||
			// Opaque Black
			samplerDesc.BorderColor[0] == 0.0f &&
			samplerDesc.BorderColor[1] == 0.0f &&
			samplerDesc.BorderColor[2] == 0.0f &&
			samplerDesc.BorderColor[3] == 1.0f ||
			// Opaque White
			samplerDesc.BorderColor[0] == 1.0f &&
			samplerDesc.BorderColor[1] == 1.0f &&
			samplerDesc.BorderColor[2] == 1.0f &&
			samplerDesc.BorderColor[3] == 1.0f,
			"Sampler border color does not match static sampler limitations");

		if (samplerDesc.BorderColor[3] == 1.0f)
		{
			if (samplerDesc.BorderColor[0] == 1.0f)
				StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
			else
				StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		}
		else
			StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	}
}

