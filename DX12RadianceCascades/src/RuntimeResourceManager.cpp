#include "rcpch.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "ShaderCompilation\ShaderCompilationManager.h"
#include "RaytracingPSO.h"
#include "Model\ModelLoader.h"
#include "Model\Renderer.h"
#include "RuntimeResourceManager.h"

void RuntimeResourceManager::CheckAndUpdatePSOs()
{
	Get().CheckAndUpdatePSOsImpl();
}

void RuntimeResourceManager::RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType)
{
	Get().RegisterPSOImpl(psoID, psoPtr, psoType);
}

void RuntimeResourceManager::SetShadersForPSO(PSOID psoID, std::vector<ShaderID> shaderIDs, bool updatePSO)
{ 
	for (ShaderID shaderID : shaderIDs) 
	{ 
		SetShaderForPSO(psoID, shaderID, false); // Set forceupdate to false always.
	}

	// Update after all shaders has been set.
	if (updatePSO)
	{
		Get().UpdatePSOImpl(psoID);
	}
}

void RuntimeResourceManager::AddModel(ModelID modelID, const std::wstring& modelPath, bool createBLAS)
{
	Get().AddModelImpl(modelID, modelPath, createBLAS);
}

InternalModel& RuntimeResourceManager::GetInternalModel(ModelID modelID)
{
	return Get().GetInternalModelImpl(modelID);
}

std::shared_ptr<Model> RuntimeResourceManager::GetModelPtr(ModelID modelID)
{
	return Get().GetModelPtrImpl(modelID);
}

BLASBuffer& RuntimeResourceManager::GetModelBLAS(ModelID modelID)
{
	return Get().GetModelBLASImpl(modelID);
}

D3D12_SHADER_BYTECODE RuntimeResourceManager::GetShader(ShaderID shaderID)
{
	return ShaderCompilationManager::Get().GetShaderByteCode(shaderID);
}

void RuntimeResourceManager::BuildCombinedShaderTable(PSOID psoID, std::set<ModelID> models, ShaderTable<LocalHitData>& outShaderTable)
{
	outShaderTable.clear();

	for (ModelID modelID : models)
	{
		HitShaderTablePackage& hitShaderTablePackage = GetOrCreateHitShaderTablePackage(psoID, modelID);

		const ShaderTable<LocalHitData>& modelHitTable = hitShaderTablePackage.hitShaderTable;
		outShaderTable.insert(outShaderTable.end(), modelHitTable.begin(), modelHitTable.end());
	}
}

RaytracingDispatchRayInputs& RuntimeResourceManager::GetRaytracingDispatch(RayDispatchID rayDispatchID)
{
	return Get().GetRaytracingDispatchImpl(rayDispatchID);
}

void RuntimeResourceManager::BuildRaytracingDispatchInputs(PSOID psoID, std::set<ModelID>& models, RayDispatchID rayDispatchID)
{
	Get().BuildRaytracingDispatchInputsImpl(psoID, models, rayDispatchID);
}

RuntimeResourceManager::RuntimeResourceManager() : m_psoMap({})
{
	m_descHeap.Create(L"Runtime Resource Manager Desc Heap", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048);

	// Load and compile shaders.
	{
		auto& shaderCM = ShaderCompilationManager::Get();
		for (auto& [shaderID, shaderFilename] : s_ShaderIDFilenameMap)
		{
			shaderCM.RegisterShader(shaderID, shaderFilename, true);
		}
	}

	// Initialize Models
	{
		AddModelImpl(ModelIDSponza, L"models\\Sponza\\PBR\\sponza2.gltf", true);
		AddModelImpl(ModelIDSphereTest, L"models\\Testing\\SphereTest.gltf", true);
		AddModelImpl(ModelIDPlane, L"models\\Testing\\Plane.gltf", true);
	}
}

void RuntimeResourceManager::CheckAndUpdatePSOsImpl()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	if (shaderCompManager.HasRecentReCompilations())
	{
		auto& compSet = shaderCompManager.GetRecentReCompilations();

		for (UUID64 shaderID : compSet)
		{
			const auto& psoIds = m_shaderPSODependencyMap[shaderID];

			if (!psoIds.empty())
			{
				for (psoid_t psoID : psoIds)
				{
					SetShaderForPSOImpl((PSOID)psoID, (ShaderID)shaderID, true);
				}
			}
		}

		shaderCompManager.ClearRecentReCompilations();
	}
}

HitShaderTablePackage& RuntimeResourceManager::GetOrCreateHitShaderTablePackage(PSOID psoID, ModelID modelID)
{
	HitShaderTablePackage& package = m_shaderTablePSOMap[psoID][modelID];

	if (package.hitShaderTable.empty())
	{
		BuildHitShaderTablePackage(psoID, modelID, package);
	}

	return package;
}

void RuntimeResourceManager::BuildHitShaderTablePackage(PSOID psoID, ModelID modelID, HitShaderTablePackage& outPackage)
{
	RaytracingPSO& rtPSO = GetRaytracingPSOImpl(psoID);

	InternalModel& internalModel = GetInternalModelImpl(modelID);
	ASSERT(internalModel.IsValid());

	const Model& model = *internalModel.modelPtr;

	ShaderTable<LocalHitData>& hitShaderTable = outPackage.hitShaderTable;
	hitShaderTable.clear();
	hitShaderTable.resize(model.m_NumMeshes);

	const Mesh* meshPtr = (const Mesh*)model.m_MeshData.get();
	void* shaderIdentifier = rtPSO.GetShaderIdentifier(outPackage.hitGroupShaderExport);
	for (int i = 0; i < (int)model.m_NumMeshes; i++)
	{
		const Mesh& mesh = meshPtr[i];
		ASSERT(mesh.numDraws == 1);

		auto& entry = hitShaderTable[i];
		entry.entryData.materialSRVs = Renderer::s_TextureHeap[mesh.srvTable]; // Start of descriptor table.
		entry.entryData.geometrySRV = internalModel.geometryDataSRVHandle;
		entry.entryData.indexByteOffset = mesh.ibOffset;
		entry.entryData.vertexByteOffset = mesh.vbOffset;

		entry.SetShaderIdentifier(shaderIdentifier);
	}
}

void RuntimeResourceManager::ForceBuildHitShaderTables(PSOID psoID)
{
	RaytracingPSO& rtPSO = GetRaytracingPSOImpl(psoID);

	auto& hitShaderTableModelMap = m_shaderTablePSOMap[psoID];
	
	if (hitShaderTableModelMap.empty())
	{
		LOG_INFO(L"PSO does not have any associated models for shader tables.");
		return;
	}

	for (auto& [rtModelID, hitShaderTablePackage] : hitShaderTableModelMap)
	{
		BuildHitShaderTablePackage(psoID, rtModelID, hitShaderTablePackage);
	}
}

void RuntimeResourceManager::SetShaderForPSOImpl(PSOID psoID, ShaderID shaderID, bool updatePSO)
{
	ShaderCompilationManager& compManager = ShaderCompilationManager::Get();

	const ShaderData* shaderData = compManager.GetShaderData(shaderID);
	ASSERT(shaderData != nullptr); // TODO: Handle default shaders if one doesnt exist.

	PSOPackage& psoPackage = GetPSOImpl(psoID);
	PSOType psoType = psoPackage.psoType;

	ShaderType shaderType = shaderData->shaderCompPackage.shaderType;
	D3D12_SHADER_BYTECODE shaderByteCode = compManager.GetShaderByteCode(shaderID);

	if (psoType == PSOTypeCompute)
	{
		if (!IS_OF_SHADER_TYPE(shaderType, ShaderTypeCS))
		{
			LOG_ERROR(L"Invalid shader type '{}' for a Compute PSO.", ShaderTypeToString(shaderType));
			return;
		}

		ComputePSO& pso = psoPackage.AsComputePSO();
		pso.SetComputeShader(shaderByteCode);
	}
	else if (psoType == PSOTypeGraphics)
	{
		if (!IS_OF_SHADER_TYPE(shaderType, ShaderTypeGraphics))
		{
			LOG_ERROR(L"Invalid shader type '{}' for a Graphics PSO.", ShaderTypeToString(shaderType));
			return;
		}

		GraphicsPSO& pso = psoPackage.AsGraphicsPSO();

		switch (shaderType)
		{
		case ShaderTypeVS:
			pso.SetVertexShader(shaderByteCode);
			break;

		case ShaderTypePS:
			pso.SetPixelShader(shaderByteCode);
			break;

		default:
			LOG_ERROR(L"Setting a shader of type '{}' has not yet been implemented for Graphics PSO.", ShaderTypeToString(shaderType));
		}
	}
	else if (psoType == PSOTypeRaytracing)
	{
		if (!IS_OF_SHADER_TYPE(shaderType, ShaderTypeRT))
		{
			LOG_ERROR(L"Invalid shader type '{}' for a Raytracing PSO.", ShaderTypeToString(shaderType));
			return;
		}

		RaytracingPSO& pso = psoPackage.AsRaytracingPSO();
		pso.SetDxilLibrary(s_DXILExports, shaderByteCode);
	}

	// If everything succeeded, add the shader dependency.
	AddShaderDependencyToPSOImpl(shaderID, psoID);

	if (updatePSO)
	{
		UpdatePSOImpl(psoID);
	}
}

void RuntimeResourceManager::UpdatePSOImpl(PSOID psoID)
{
	// Wait for all work to be done before changing any PSO.
	Graphics::g_CommandManager.IdleGPU();

	PSOPackage& psoPackage = GetPSOImpl(psoID);
	PSOType psoType = psoPackage.psoType;

	if (psoType == PSOTypeCompute)
	{
		psoPackage.AsComputePSO().Finalize();
	}
	else if (psoType == PSOTypeGraphics)
	{
		psoPackage.AsGraphicsPSO().Finalize();
	}
	else if (psoType == PSOTypeRaytracing)
	{
		psoPackage.AsRaytracingPSO().Finalize();

		LOG_DEBUG(L"Updating all hit shader tables with new shader identifier data.");
		ForceBuildHitShaderTables(psoID);

		std::set<ModelID> dependantModels = {};
		for (auto& [modelID, _] : m_shaderTablePSOMap[psoID])
		{
			dependantModels.insert(modelID);
		}

		LOG_DEBUG(L"Updating all raytracing dispatch inputs with new shader tables.");
		for (const RayDispatchID rayDispatchID : m_psoRayDispatchDependencyMap[psoID])
		{
			BuildRaytracingDispatchInputsImpl(psoID, dependantModels, rayDispatchID);
		}
	}
	else
	{
		LOG_ERROR(L"Invalid PSO type {}", (uint32_t)psoPackage.psoType);
	}
}

void RuntimeResourceManager::AddShaderDependencyToPSOImpl(ShaderID shaderID, psoid_t psoID)
{
	// TODO: 
	// Check for the given PSO if it exist for any other shader. If that shader is the same type, remove the PSO as a dependency. There can only be a single shader for a given PSO and Shader type.
	m_shaderPSODependencyMap[shaderID].insert(psoID);
}

void RuntimeResourceManager::AddShaderDependenciesToPSOImpl(ShaderID shaderID, std::vector<psoid_t>& psoIDs)
{
	for (psoid_t psoID : psoIDs)
	{
		AddShaderDependencyToPSOImpl(shaderID, psoID);
	}
}

void RuntimeResourceManager::RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType)
{
	m_psoMap[psoID] = { psoPtr, psoType };
}

PSOPackage& RuntimeResourceManager::GetPSOImpl(PSOID psoID)
{
	return m_psoMap[psoID];
}

void RuntimeResourceManager::AddModelImpl(ModelID modelID, const std::wstring& modelPath, bool createBLAS)
{
	std::shared_ptr<Model> modelPtr = Renderer::LoadModel(modelPath, false);
	ASSERT(modelPtr != nullptr);

	// Will overwrite any existing internal models.
	InternalModel& internalModel = GetInternalModelImpl(modelID);
	internalModel.modelPtr = modelPtr;

	// Copy the SRV handle to geometry data for binding in shader table. 
	// If the handle already exists it will be overwritten instead.
	{
		DescriptorHandle& descHandle = internalModel.geometryDataSRVHandle;
		if (descHandle.IsNull())
		{
			descHandle = m_descHeap.Alloc();
		}

		Model& model = *modelPtr;
		Graphics::g_Device->CopyDescriptorsSimple(1, descHandle, model.m_DataBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	if (createBLAS)
	{
		internalModel.modelBLAS.Init(modelPtr);
	}
}

InternalModel& RuntimeResourceManager::GetInternalModelImpl(ModelID modelID)
{
	return m_internalModels[modelID];
}

inline std::shared_ptr<Model> RuntimeResourceManager::GetModelPtrImpl(ModelID modelID)
{
	return GetInternalModelImpl(modelID).modelPtr;
}

BLASBuffer& RuntimeResourceManager::GetModelBLASImpl(ModelID modelID)
{
	return GetInternalModelImpl(modelID).modelBLAS;
}

RaytracingPSO& RuntimeResourceManager::GetRaytracingPSOImpl(PSOID rtPSOID)
{
	return GetPSOImpl(rtPSOID).AsRaytracingPSO();
}

GraphicsPSO& RuntimeResourceManager::GetGraphicsPSOImpl(PSOID gfxPSOID)
{
	return GetPSOImpl(gfxPSOID).AsGraphicsPSO();
}

ComputePSO& RuntimeResourceManager::GetComputePSOImpl(PSOID cmptPSOID)
{
	return GetPSOImpl(cmptPSOID).AsComputePSO();
}

void RuntimeResourceManager::BuildRaytracingDispatchInputsImpl(PSOID psoID, std::set<ModelID>& models, RayDispatchID rayDispatchID)
{
	ShaderTable<LocalHitData> combinedShaderTable = {};
	BuildCombinedShaderTable(psoID, models, combinedShaderTable);

	RaytracingDispatchRayInputs& rayDispatchInputs = m_rayDispatchInputs[rayDispatchID];
	rayDispatchInputs.Init(GetRaytracingPSOImpl(psoID), combinedShaderTable, L"RayGenerationShader", L"MissShader");

	// Add dependency.
	m_psoRayDispatchDependencyMap[psoID].insert(rayDispatchID);
}

RaytracingDispatchRayInputs& RuntimeResourceManager::GetRaytracingDispatchImpl(RayDispatchID rayDispatchID)
{
	return m_rayDispatchInputs[rayDispatchID];
}

void RuntimeResourceManager::CopyDescriptorImpl(const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
	ASSERT(handle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_NULL);

	DescriptorHandle& destDescHandle = m_descriptorCopiedCBVSRVUAV[handle.ptr];
	destDescHandle = m_descHeap.Alloc(1);

	Graphics::g_Device->CopyDescriptorsSimple(1, destDescHandle, handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

const DescriptorHandle& RuntimeResourceManager::GetDescCopyImpl(const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
{
	auto it = m_descriptorCopiedCBVSRVUAV.find(handle.ptr);
	if (it == m_descriptorCopiedCBVSRVUAV.end())
	{
		CopyDescriptorImpl(handle);
	}

	return m_descriptorCopiedCBVSRVUAV.at(handle.ptr);
}

void RuntimeResourceManager::DestroyImpl()
{
	Graphics::g_CommandManager.IdleGPU();

	m_internalModels.clear();
	m_rayDispatchInputs.clear();
	m_descHeap.Destroy();
}

GraphicsPSO& PSOPackage::AsGraphicsPSO()
{
	ASSERT(PSOPointer != nullptr && psoType == PSOTypeGraphics);
	return *reinterpret_cast<GraphicsPSO*>(PSOPointer);
}

ComputePSO& PSOPackage::AsComputePSO()
{
	ASSERT(PSOPointer != nullptr && psoType == PSOTypeCompute);
	return *reinterpret_cast<ComputePSO*>(PSOPointer);
}

RaytracingPSO& PSOPackage::AsRaytracingPSO()
{
	ASSERT(PSOPointer != nullptr && psoType == PSOTypeRaytracing);
	return *reinterpret_cast<RaytracingPSO*>(PSOPointer);
}
