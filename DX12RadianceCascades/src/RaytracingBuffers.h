#pragma once

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
	BLASBuffer(std::shared_ptr<Model> modelPtr);

	void Init(std::shared_ptr<Model> modelPtr);

	D3D12_GPU_VIRTUAL_ADDRESS GetBVH() const;
	uint32_t GetNumGeometries() const { return m_modelPtr->m_NumMeshes; }
	std::shared_ptr<const Model> GetModelPtr() const { return m_modelPtr; }

private:
	AccelerationStructureData m_asData;
	StructuredBuffer m_geometryInstanceData;
	std::shared_ptr<const Model> m_modelPtr; // This can probably be removed.
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