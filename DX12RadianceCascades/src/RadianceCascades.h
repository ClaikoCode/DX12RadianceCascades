#pragma once

#include "Core\GameCore.h"
#include "Core\ColorBuffer.h"
#include "Core\GpuBuffer.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"
#include "RaytracingUtils.h"

#include "RadianceCascadesManager.h"

typedef uintptr_t modelhash_t;

enum ModelID : UUID64
{
	ModelIDSponza = 0,
	ModelIDSphereTest,
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

	ShaderIDNone = NULL_ID
};

typedef uint32_t PSOIDType;
enum PSOID : PSOIDType
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

	PSOIDCount
};

enum PSOType : uint32_t
{
	PSOTypeCompute = 0,
	PSOTypeGraphics,
	PSOTypeRaytracing,
	
	PSOTypeCount
};

struct RadianceCascadesSettings
{
	bool visualize2DCascades = false;
};

struct GlobalSettings
{
	bool renderRaster = false;
	bool renderRaytracing = true;
};

struct AppSettings
{
	GlobalSettings globalSettings;
	RadianceCascadesSettings rcSettings;
};

struct DescriptorCopies
{
	void Init();

	std::array<DescriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descHeaps;

	DescriptorHandle sceneColorUAVHandle;
};

struct PSOPackage
{
	void* PSOPointer;
	PSOType psoType;
};

class RadianceCascades : public GameCore::IGameApp
{
private:

	enum RootEntry : uint32_t
	{
		RootEntryRCGatherGlobals = 0,
		RootEntryRCGatherCascadeInfo,
		RootEntryRCGatherCascadeUAV,
		RootEntryRCGatherSceneSRV,
		RootEntryRCGatherCount,

		RootEntryFlatlandSceneInfo = 0,
		RootEntryFlatlandSceneUAV,
		RootEntryFlatlandCount,

		RootEntryFullScreenCopySource = 0,
		RootEntryFullScreenCopyCount,

		RootEntryFullScreenCopyComputeSource = 0,
		RootEntryFullScreenCopyComputeDest,
		RootEntryFullScreenCopyComputeDestInfo,
		RootEntryFullScreenCopyComputeCount,

		RootEntryRCMergeCascadeNUAV = 0,
		RootEntryRCMergeCascadeN1SRV,
		RootEntryRCMergeCascadeInfo,
		RootEntryRCMergeGlobals,
		RootEntryRCMergeCount,

		RootEntryRCRadianceFieldGlobals = 0,
		RootEntryRCRadianceFieldCascadeInfo,
		RootEntryRCRadianceFieldUAV,
		RootEntryRCRadianceFieldCascadeSRV,
		RootEntryRCRadianceFieldInfo,
		RootEntryRCRadianceFieldCount,

		RootEntryRTGSRV = 0,
		RootEntryRTGUAV,
		RootEntryRTGParamCB,
		RootEntryRTGInfoCB,
		RootEntryRTGCount,

		RootEntryRTLSRV = 0,
		RootEntryRTLCount,
	};

public:
	RadianceCascades();
	~RadianceCascades();

	virtual void Startup() override;
	virtual void Cleanup() override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene() override;

	virtual bool RequiresRaytracingSupport() const override { return true; }

private:

	void InitializeResources();
	void InitializeScene();
	void InitializeShaders();
	void InitializePSOs();
	void InitializeRCResources();
	void InitializeRT();

	void RenderSceneImpl(Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor);
	void RenderRaytracing(Camera& camera, ColorBuffer& colorTarget);
	void RunComputeFlatlandScene();
	void RunComputeRCGather();
	void RunComputeRCMerge();
	void RunComputeRCRadianceField(ColorBuffer& outputBuffer);
	void UpdateViewportAndScissor();
	void UpdateGraphicsPSOs();

	void ClearPixelBuffers();

	// Will run a compute shader that samples the source and writes to dest.
	void FullScreenCopyCompute(PixelBuffer& source, D3D12_CPU_DESCRIPTOR_HANDLE sourceSRV, ColorBuffer& dest);
	void FullScreenCopyCompute(ColorBuffer& source, ColorBuffer& dest);

	ModelInstance& AddModelInstance(std::shared_ptr<Model> modelPtr);
	void AddShaderDependency(ShaderID shaderID, std::vector<PSOIDType> psoIDs);
	void RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType);

	ModelInstance& GetMainSceneModelInstance()
	{
		ASSERT(m_mainSceneModelInstanceIndex < m_sceneModels.size());
		return m_sceneModels[m_mainSceneModelInstanceIndex];
	}

	std::shared_ptr<Model> GetModelPtr(ModelID modelID)
	{
		return m_models[modelID];
	}

private:

	AppSettings m_settings = {};

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

	uint32_t m_mainSceneModelInstanceIndex;
	std::vector<ModelInstance> m_sceneModels;

	D3D12_VIEWPORT m_mainViewport;
	D3D12_RECT m_mainScissor;

	// Tells what PSOs are dependent on what shaders.
	std::unordered_map<UUID64, std::set<PSOIDType>> m_shaderPSODependencyMap;
	std::array<PSOPackage, PSOIDCount> m_usedPSOs;

	ComputePSO m_rcGatherPSO = ComputePSO(L"RC Gather Compute");
	RootSignature m_computeGatherRootSig;

	ComputePSO m_flatlandScenePSO = ComputePSO(L"Compute Flatland Scene");
	RootSignature m_computeFlatlandSceneRootSig;

	ComputePSO m_fullScreenCopyComputePSO = ComputePSO(L"Full Screen Copy Compute");
	RootSignature m_fullScreenCopyComputeRootSig;

	ComputePSO m_rcMergePSO = ComputePSO(L"RC Merge Compute");
	RootSignature m_rcMergeRootSig;

	ComputePSO m_rcRadianceFieldPSO = ComputePSO(L"RC Radiance Field Compute");
	RootSignature m_rcRadianceFieldRootSig;

	RaytracingPSO m_rtTestPSO = RaytracingPSO(L"RT Test PSO");
	RootSignature1 m_rtTestGlobalRootSig;
	RootSignature1 m_rtTestLocalRootSig;
	RaytracingDispatchRayInputs m_testRTDispatch;
	TLASBuffers m_sceneModelTLASInstance;

	ColorBuffer m_flatlandScene = ColorBuffer({ 0.0f, 0.0f, 0.0f, 100000.0f });

	RadianceCascadesManager m_rcManager;

	DescriptorCopies m_descCopies;

	std::unordered_map<ModelID, std::shared_ptr<Model>> m_models;
	std::unordered_map<modelhash_t, BLASBuffer> m_modelBLASes;
};

