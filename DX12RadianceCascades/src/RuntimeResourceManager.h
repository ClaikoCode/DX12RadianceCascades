#pragma once

#include "Model\Model.h"
#include "ShaderTable.h"
#include "RaytracingBuffers.h"
#include "RaytracingDispatchRayInputs.h"

enum ModelID : UUID64
{
	ModelIDSponza = 0,
	ModelIDSphereTest,
};

enum RayDispatchID : uint32_t
{
	RayDispatchIDNone = 0,
	RayDispatchIDTest,
	RayDispatchIDRCRaytracing,

	RayDistpatchIDCount
};

enum ShaderID : UUID64
{
	ShaderIDInvalid = 0,
	ShaderIDSceneRenderPS,
	ShaderIDSceneRenderVS,
	ShaderIDRCGatherCS,
	ShaderIDFlatlandSceneCS,
	ShaderIDFullScreenQuadVS,
	ShaderIDFullScreenCopyPS,
	ShaderIDFullScreenCopyCS,
	ShaderIDRCMergeCS,
	ShaderIDRCRadianceFieldCS,
	ShaderIDRaytracingTestRT,
	ShaderIDDebugDrawPS,
	ShaderIDDebugDrawVS,
	ShaderIDMinMaxDepthCS,
	ShaderIDRCRaytraceRT,

	ShaderIDNone = NULL_ID
};

typedef uint32_t psoid_t;
enum PSOID : psoid_t
{
	PSOIDFirstExternalPSO = 0,
	PSOIDSecondExternalPSO,
	PSOIDComputeTestPSO,
	PSOIDComputeRCGatherPSO,
	PSOIDComputeFlatlandScenePSO,
	PSOIDComputeFullScreenCopyPSO,
	PSOIDComputeRCMergePSO,
	PSOIDComputeRCRadianceFieldPSO,
	PSOIDRaytracingTestPSO,
	PSOIDDebugDrawPSO,
	PSOIDComputeMinMaxDepthPSO,
	PSOIDRCRaytracingPSO,

	PSOIDCount
};

enum PSOType : uint32_t
{
	PSOTypeCompute = 0,
	PSOTypeGraphics,
	PSOTypeRaytracing,

	PSOTypeCount
};

struct PSOPackage
{
	void* PSOPointer;
	PSOType psoType;
};

struct InternalModel
{
	std::shared_ptr<Model> modelPtr = nullptr;
	DescriptorHandle geometryDataSRVHandle;

	BLASBuffer modelBLAS; // Must be created explicitly when adding a model.

	bool IsValid() { return modelPtr != nullptr; }
};

struct HitShaderTablePackage
{
	std::wstring hitGroupShaderExport = s_HitGroupName;
	ShaderTable<LocalHitData> hitShaderTable;
};

class RuntimeResourceManager
{
public:
	static RuntimeResourceManager& Get()
	{
		static RuntimeResourceManager instance = RuntimeResourceManager();

		return instance;
	}

	static void UpdateGraphicsPSOs();
	// Not a reference so rvalues can be input. Inefficient but its not a hotpath.
	static void AddShaderDependency(ShaderID shaderID, std::vector<psoid_t> psoIDs); 
	static void RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType);

	static void AddModel(ModelID modelID, const std::wstring& modelPath, bool createBLAS = false);
	static InternalModel& GetInternalModel(ModelID  modelID);
	static std::shared_ptr<Model> GetModelPtr(ModelID modelID);
	static BLASBuffer& GetModelBLAS(ModelID modelID);
	static D3D12_SHADER_BYTECODE GetShader(ShaderID shaderID);

	static ID3D12DescriptorHeap* GetDescriptorHeapPtr() { return Get().m_descHeap.GetHeapPointer(); }

	static RaytracingDispatchRayInputs& GetRaytracingDispatch(RayDispatchID rayDispatchID);
	static void BuildRaytracingDispatchInputs(PSOID psoID, std::set<ModelID>& models, RayDispatchID rayDispatchID);

	static RaytracingPSO& GetRaytracingPSO(PSOID rtPSOID) { return Get().GetRaytracingPSOImpl(rtPSOID); }
	static GraphicsPSO& GetGraphicsPSO(PSOID gfxPSOID) { return Get().GetGraphicsPSOImpl(gfxPSOID); }
	static ComputePSO& GetComputePSO(PSOID cmptPSOID) { return Get().GetComputePSOImpl(cmptPSOID); }

	static void CopyDescriptor(const D3D12_CPU_DESCRIPTOR_HANDLE& handle) { return Get().CopyDescriptorImpl(handle); }
	// Will copy the descriptor if it doesnt exist already.
	// This is only for UAV, SRV, and CBV
	static const DescriptorHandle& GetDescCopy(const D3D12_CPU_DESCRIPTOR_HANDLE& handle) { return Get().GetDescCopyImpl(handle); }

	static void Destroy() { Get().DestroyImpl(); }

private: 
	RuntimeResourceManager();
	void UpdatePSOs();

	// Will create the shader table if it doesnt exist.
	HitShaderTablePackage& GetOrCreateHitShaderTablePackage(PSOID psoID, ModelID modelID);

	void ForceBuildHitShaderTables(PSOID psoID);
	void BuildHitShaderTablePackage(PSOID psoID, ModelID modelID, HitShaderTablePackage& outPackage);
	void BuildCombinedShaderTable(PSOID psoID, std::set<ModelID> models, ShaderTable<LocalHitData>& outShaderTable);

private:
	void AddShaderDependencyImpl(ShaderID shaderID, std::vector<psoid_t>& psoIDs); 
	void RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType);
	PSOPackage& GetPSOImpl(PSOID psoID);

	void AddModelImpl(ModelID modelID, const std::wstring& modelPath, bool createBLAS);
	InternalModel& GetInternalModelImpl(ModelID modelID);
	std::shared_ptr<Model> GetModelPtrImpl(ModelID modelID);
	BLASBuffer& GetModelBLASImpl(ModelID modelID);

	RaytracingPSO& GetRaytracingPSOImpl(PSOID rtPSOID);
	GraphicsPSO& GetGraphicsPSOImpl(PSOID gfxPSOID);
	ComputePSO& GetComputePSOImpl(PSOID cmptPSOID);

	void BuildRaytracingDispatchInputsImpl(PSOID psoID, std::set<ModelID>& models, RayDispatchID rayDispatchID);
	RaytracingDispatchRayInputs& GetRaytracingDispatchImpl(RayDispatchID rayDispatchID);

	void CopyDescriptorImpl(const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
	const DescriptorHandle& GetDescCopyImpl(const D3D12_CPU_DESCRIPTOR_HANDLE& handle);

	void DestroyImpl();

private:
	DescriptorHeap m_descHeap;

	std::unordered_map<UUID64, std::set<psoid_t>> m_shaderPSODependencyMap;
	std::array<PSOPackage, PSOIDCount> m_usedPSOs;

	std::unordered_map<ModelID, InternalModel> m_internalModels;
	std::unordered_map<PSOID, std::unordered_map<ModelID, HitShaderTablePackage>> m_shaderTablePSOMap;
	std::unordered_map<RayDispatchID, RaytracingDispatchRayInputs> m_rayDispatchInputs;
	std::unordered_map<PSOID, std::unordered_set<RayDispatchID>> m_psoRayDispatchDependencyMap;

	// Maps the ptr inside D3D12_CPU_DESCRIPTOR_HANDLE to a copied version.
	// This is to be able to expose CPU only visible desc handles as GPU and CPU visible in the resource manager heap.
	// This is because most of MiniEngine works with CPU exposed handles only.
	std::unordered_map<SIZE_T, DescriptorHandle> m_descriptorCopiedCBVSRVUAV;
};