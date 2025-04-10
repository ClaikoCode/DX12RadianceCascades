#include "rcpch.h"
#include "Core\GraphicsCore.h"
#include "Model\Model.h"
#include "RaytracingUtils.h"

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
		ThrowIfFailed(hr, L"Failed to create state object.");
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

		ThrowIfFailed(hr, errorMsgW);
	}

	ThrowIfFailed(
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

BLASBuffer::BLASBuffer(const Model& model)
{
	Init(model);
}

void BLASBuffer::Init(const Model& model)
{
	m_numGeometryDescriptions = model.m_NumMeshes;
	const Mesh* meshPtr = (const Mesh*)model.m_MeshData.get();

	// Fill geometry description information per submesh.
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(m_numGeometryDescriptions);
	const D3D12_GPU_VIRTUAL_ADDRESS modelDataBuffer = model.m_DataBuffer.GetGpuVirtualAddress();
	for (uint32_t i = 0; i < m_numGeometryDescriptions; i++)
	{
		const Mesh& mesh = meshPtr[i];

		// Only support meshes that require 1 draw per submesh. This has to do with index count data.
		ASSERT(mesh.numDraws == 1);

		D3D12_RAYTRACING_GEOMETRY_DESC& geomDesc = geometryDescs[i];

		geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangleDesc = geomDesc.Triangles;

		triangleDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		triangleDesc.VertexCount = mesh.vbSize / mesh.vbStride;
		triangleDesc.VertexBuffer.StartAddress = modelDataBuffer + mesh.vbOffset;
		triangleDesc.VertexBuffer.StrideInBytes = mesh.vbStride;

		triangleDesc.IndexFormat = DXGI_FORMAT_R16_UINT;
		triangleDesc.IndexBuffer = modelDataBuffer + mesh.ibOffset;
		triangleDesc.IndexCount = mesh.draw[0].primCount;

		triangleDesc.Transform3x4 = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
	auto& blasInputs = blasDesc.Inputs;
	blasInputs.Type				= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	blasInputs.DescsLayout		= D3D12_ELEMENTS_LAYOUT_ARRAY;
	blasInputs.NumDescs			= (UINT)geometryDescs.size();
	blasInputs.pGeometryDescs	= geometryDescs.data();
	blasInputs.Flags			= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	m_asData.CreateBuffers(blasDesc);
	blasDesc.DestAccelerationStructureData = m_asData.bvhBuffer.GetGpuVirtualAddress();
	blasDesc.ScratchAccelerationStructureData = m_asData.scratchBuffer.GetGpuVirtualAddress();
	
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"BLAS Build");
	ComPtr<ID3D12GraphicsCommandList4> rtCommandList;
	ThrowIfFailed(gfxContext.GetCommandList()->QueryInterface(rtCommandList.GetAddressOf()));

	rtCommandList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

	gfxContext.Finish(true);
}

D3D12_GPU_VIRTUAL_ADDRESS BLASBuffer::GetBVH() const
{
	return m_asData.bvhBuffer.GetGpuVirtualAddress();
}

TLASBuffers::TLASBuffers(const BLASBuffer& blas, const std::vector<TLASInstanceGroup>& instanceGroups)
{
	Init(instanceGroups);
}

void TLASBuffers::Init(const std::vector<TLASInstanceGroup>& instanceGroups)
{
	if (instanceGroups.empty())
	{
		LOG_ERROR(L"Cannot initialize TLAS buffer because instance group vector is empty.");
		return;
	}

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs = {};
	// Each instance of a BLAS is offset in the shader table the same amount, i.e, starting at the same index. 
	// This is because it is assumed that there is a shader entry per geometry description.
	uint32_t instanceContributionOffset = 0;
	for (const TLASInstanceGroup& instanceGroup : instanceGroups)
	{
		if (instanceGroup.blasBuffer == nullptr)
		{
			LOG_ERROR(L"BLAS buffer is null.");
			continue;
		}

		const D3D12_GPU_VIRTUAL_ADDRESS blasBVH = instanceGroup.blasBuffer->GetBVH();
		for (auto& instanceTransform : instanceGroup.instanceTransforms)
		{
			auto& desc = instanceDescs.emplace_back();

			desc.AccelerationStructure = blasBVH;
			desc.Flags = 0;
			desc.InstanceID = 0;
			desc.InstanceMask = 1;
			desc.InstanceContributionToHitGroupIndex = instanceContributionOffset;

			const DirectX::XMFLOAT4X4& transformMat = instanceTransform.gpuMat;

			// Initialize data.
			ZeroMemory(desc.Transform, sizeof(desc.Transform));

			// Copy 3 first rows to desc.
			memcpy(desc.Transform[0], transformMat.m[0], sizeof(float) * 4);
			memcpy(desc.Transform[1], transformMat.m[1], sizeof(float) * 4);
			memcpy(desc.Transform[2], transformMat.m[2], sizeof(float) * 4);
		}

		instanceContributionOffset += instanceGroup.blasBuffer->GetNumGeometries();
	}

	

	const uint32_t numInstances = (uint32_t)instanceDescs.size();
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& tlasInputs = tlasDesc.Inputs;
		tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		tlasInputs.NumDescs = numInstances;
		tlasInputs.pGeometryDescs = nullptr;
		tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	}

	m_asData.CreateBuffers(tlasDesc);
	tlasDesc.DestAccelerationStructureData = m_asData.bvhBuffer.GetGpuVirtualAddress();
	tlasDesc.ScratchAccelerationStructureData = m_asData.scratchBuffer.GetGpuVirtualAddress();

	CreateInstanceDataBuffer(tlasDesc, numInstances, instanceDescs.data());
	
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"TLAS Build");
	ComPtr<ID3D12GraphicsCommandList4> rtCommandList;
	ThrowIfFailed(gfxContext.GetCommandList()->QueryInterface(rtCommandList.GetAddressOf()));

	rtCommandList->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);

	gfxContext.Finish(true);
}

D3D12_GPU_VIRTUAL_ADDRESS TLASBuffers::GetBVH() const
{
	return m_asData.bvhBuffer.GetGpuVirtualAddress();
}

void TLASBuffers::CreateInstanceDataBuffer(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& structDesc, uint32_t numInstances, D3D12_RAYTRACING_INSTANCE_DESC* descs)
{
	if (descs == nullptr)
	{
		LOG_ERROR(L"Pointer to RT instances is null.");
		return;
	}

	m_instanceDataBuffer.Create(L"Instance Data Buffer", numInstances, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), descs);
	structDesc.Inputs.InstanceDescs = m_instanceDataBuffer.GetGpuVirtualAddress();
	structDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
}
