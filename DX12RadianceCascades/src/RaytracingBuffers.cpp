#include "rcpch.h"
#include "Model\Model.h"
#include "Core\GpuBuffer.h"
#include "RaytracingBuffers.h"

using namespace Microsoft::WRL;

BLASBuffer::BLASBuffer(std::shared_ptr<Model> modelPtr)
{
	Init(modelPtr);
}

void BLASBuffer::Init(std::shared_ptr<Model> modelPtr)
{
	ASSERT(modelPtr != nullptr);

	m_modelPtr = modelPtr;
	const Model& model = *m_modelPtr;
	const uint32_t numMeshes = model.m_NumMeshes;
	const Mesh* meshPtr = (const Mesh*)model.m_MeshData.get();

	// Fill geometry description information per submesh.
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(numMeshes);
	const D3D12_GPU_VIRTUAL_ADDRESS modelDataBuffer = model.m_DataBuffer.GetGpuVirtualAddress();
	for (uint32_t i = 0; i < numMeshes; i++)
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
	blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	blasInputs.NumDescs = (UINT)geometryDescs.size();
	blasInputs.pGeometryDescs = geometryDescs.data();
	blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

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

D3D12_GPU_VIRTUAL_ADDRESS TLASBuffers::GetBVH() const
{
	return m_asData.bvhBuffer.GetGpuVirtualAddress();
}