#include "rcpch.h"
#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "ShaderCompilation\ShaderCompilationManager.h"
#include "RaytracingPSO.h"
#include "Model\ModelLoader.h"
#include "Model\Renderer.h"
#include "RuntimeResourceManager.h"

static const std::set<Shader::ShaderType> s_ValidShaderTypes = { Shader::ShaderTypeCS, Shader::ShaderTypeVS, Shader::ShaderTypePS, Shader::ShaderTypeRT };

void RuntimeResourceManager::UpdateGraphicsPSOs()
{
	Get().UpdatePSOs();
}

void RuntimeResourceManager::AddShaderDependency(ShaderID shaderID, std::vector<psoid_t> psoIDs)
{
	Get().AddShaderDependencyImpl(shaderID, psoIDs);
}

void RuntimeResourceManager::RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType)
{
	Get().RegisterPSOImpl(psoID, psoPtr, psoType);
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

RuntimeResourceManager::RuntimeResourceManager() : m_usedPSOs({})
{
	m_descHeap.Create(L"Runtime Resource Manager Desc Heap", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048);
	m_sceneColorDescHande = m_descHeap.Alloc();
	Graphics::g_Device->CopyDescriptorsSimple(1, m_sceneColorDescHande, Graphics::g_SceneColorBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto& shaderCM = ShaderCompilationManager::Get();

	// Initialize shaders
	{
		// Pixel Shaders
		shaderCM.RegisterPixelShader(ShaderIDSceneRenderPS, L"SceneRenderPS.hlsl", true);
		shaderCM.RegisterPixelShader(ShaderIDFullScreenCopyPS, L"DirectWritePS.hlsl", true);
		shaderCM.RegisterPixelShader(ShaderIDDebugDrawPS, L"DebugDrawPS.hlsl", true);

		// Vertex Shaders
		shaderCM.RegisterVertexShader(ShaderIDSceneRenderVS, L"SceneRenderVS.hlsl", true);
		shaderCM.RegisterVertexShader(ShaderIDFullScreenQuadVS, L"FullScreenQuadVS.hlsl", true);
		shaderCM.RegisterVertexShader(ShaderIDDebugDrawVS, L"DebugDrawVS.hlsl", true);

		// Compute Shaders
		shaderCM.RegisterComputeShader(ShaderIDRCGatherCS, L"RCGatherCS.hlsl", true);
		shaderCM.RegisterComputeShader(ShaderIDFlatlandSceneCS, L"FlatlandSceneCS.hlsl", true);
		shaderCM.RegisterComputeShader(ShaderIDFullScreenCopyCS, L"DirectCopyCS.hlsl", true);
		shaderCM.RegisterComputeShader(ShaderIDRCMergeCS, L"RCMergeCS.hlsl", true);
		shaderCM.RegisterComputeShader(ShaderIDRCRadianceFieldCS, L"RCRadianceFieldCS.hlsl", true);
		shaderCM.RegisterComputeShader(ShaderIDMinMaxDepthCS, L"MinMaxDepthCS.hlsl", true);

		// RT Shaders
		shaderCM.RegisterRaytracingShader(ShaderIDRaytracingTestRT, L"RaytracingTest.hlsl", true);
	}

	// Initialize Models
	{
		AddModelImpl(ModelIDSponza, L"models\\Sponza\\PBR\\sponza2.gltf", true);
		AddModelImpl(ModelIDSphereTest, L"models\\Testing\\SphereTest.gltf", true);
	}
}

void RuntimeResourceManager::UpdatePSOs()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	if (shaderCompManager.HasRecentReCompilations())
	{
		// Wait for all work to be done before changing PSOs.
		Graphics::g_CommandManager.IdleGPU();

		auto& compSet = shaderCompManager.GetRecentReCompilations();

		for (UUID64 shaderID : compSet)
		{
			Shader::ShaderType shaderType = shaderCompManager.GetShaderType(shaderID);

			if (s_ValidShaderTypes.find(shaderType) == s_ValidShaderTypes.end())
			{
				LOG_INFO(L"Invalid shader type: {}. Skipping.", (uint32_t)shaderType);
				continue;
			}

			D3D12_SHADER_BYTECODE shaderByteCode = shaderCompManager.GetShaderByteCode(shaderID);

			if (shaderByteCode.pShaderBytecode)
			{
				const auto& psoIds = m_shaderPSODependencyMap[shaderID];

				if (!psoIds.empty())
				{
					for (psoid_t psoID : psoIds)
					{
						const PSOPackage& package = m_usedPSOs[psoID];

						if (package.psoType == PSOTypeCompute)
						{
							ComputePSO& pso = *(reinterpret_cast<ComputePSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								pso.SetComputeShader(shaderByteCode);
								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeGraphics)
						{
							GraphicsPSO& pso = *(reinterpret_cast<GraphicsPSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								if (shaderType == Shader::ShaderTypePS)
								{
									pso.SetPixelShader(shaderByteCode);
								}
								else if (shaderType == Shader::ShaderTypeVS)
								{
									pso.SetVertexShader(shaderByteCode);
								}

								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeRaytracing)
						{
							RaytracingPSO& pso = *(reinterpret_cast<RaytracingPSO*>(package.PSOPointer));

							LOG_DEBUG(L"Updating RT PSO {}.", psoID);
							if (pso.GetStateObject() != nullptr)
							{
								pso.SetDxilLibrary(s_DXILExports, shaderByteCode);

								pso.Finalize();
							}

							PSOID rtPSOID = (PSOID)psoID;
							LOG_DEBUG(L"Updating all hit shader tables with new shader identifier data.");
							ForceBuildHitShaderTables(rtPSOID);

							std::set<ModelID> dependantModels = {};
							for (auto& [modelID, _] : m_shaderTablePSOMap[rtPSOID])
							{
								dependantModels.insert(modelID);
							}

							LOG_DEBUG(L"Updating all raytracing dispatch inputs with new shader tables.");
							for (const RayDispatchID rayDispatchID : m_psoRayDispatchDependencyMap[rtPSOID])
							{
								BuildRaytracingDispatchInputsImpl(rtPSOID, dependantModels, rayDispatchID);
							}
						}
					}
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

void RuntimeResourceManager::AddShaderDependencyImpl(ShaderID shaderID, std::vector<psoid_t>& psoIDs)
{
	for (uint32_t psoID : psoIDs)
	{
		m_shaderPSODependencyMap[shaderID].insert(psoID);
	}
}

void RuntimeResourceManager::RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType)
{
	m_usedPSOs[psoID] = { psoPtr, psoType };
}

PSOPackage& RuntimeResourceManager::GetPSOImpl(PSOID psoID)
{
	return m_usedPSOs[psoID];
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
	const PSOPackage& psoPackage = GetPSOImpl(rtPSOID);
	ASSERT(psoPackage.psoType == PSOTypeRaytracing && psoPackage.PSOPointer != nullptr);

	return *reinterpret_cast<RaytracingPSO*>(psoPackage.PSOPointer);
}

GraphicsPSO& RuntimeResourceManager::GetGraphicsPSOImpl(PSOID gfxPSOID)
{
	const PSOPackage& psoPackage = GetPSOImpl(gfxPSOID);
	ASSERT(psoPackage.psoType == PSOTypeGraphics && psoPackage.PSOPointer != nullptr);

	return *reinterpret_cast<GraphicsPSO*>(psoPackage.PSOPointer);
}

ComputePSO& RuntimeResourceManager::GetComputePSOImpl(PSOID cmptPSOID)
{
	const PSOPackage& psoPackage = GetPSOImpl(cmptPSOID);
	ASSERT(psoPackage.psoType == PSOTypeCompute && psoPackage.PSOPointer != nullptr);

	return *reinterpret_cast<ComputePSO*>(psoPackage.PSOPointer);
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

void RuntimeResourceManager::DestroyImpl()
{
	Graphics::g_CommandManager.IdleGPU();

	m_internalModels.clear();
	m_rayDispatchInputs.clear();
	m_descHeap.Destroy();
}
