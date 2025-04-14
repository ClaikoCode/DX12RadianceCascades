#pragma once

#include "Core\GameCore.h"
#include "Core\ColorBuffer.h"
#include "Core\GpuBuffer.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"
#include "RaytracingUtils.h"
#include "RuntimeResourceManager.h"

#include "RadianceCascadesManager.h"

class InternalModelInstance : public ModelInstance
{
public:
	InternalModelInstance() = default;

	InternalModelInstance(std::shared_ptr<Model> modelPtr, ModelID modelID) 
		: ModelInstance(modelPtr)
	{
		underlyingModelID = modelID;
	}

	ModelID underlyingModelID;
};

struct RadianceCascadesSettings
{
	bool visualize2DCascades = false;
};

#define ENABLE_RT (false)
#define ENABLE_RASTER (!ENABLE_RT)
struct GlobalSettings
{
	bool renderRaster = ENABLE_RASTER;
	bool renderRaytracing = ENABLE_RT;
};

struct AppSettings
{
	GlobalSettings globalSettings;
	RadianceCascadesSettings rcSettings;
};

struct DescriptorHeaps
{
	void Init();

	std::array<DescriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descHeaps;

	DescriptorHandle sceneColorUAVHandle;
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

		RootEntryRTLGeometryDataSRV = 0,
		RootEntryRTLTextureSRV,
		RootEntryRTLOffsetConstants,
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

	void InitializeHeaps();
	void InitializeScene();
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

	InternalModelInstance& AddModelInstance(ModelID modelID);

	InternalModelInstance& GetMainSceneModelInstance()
	{
		ASSERT(m_mainSceneModelInstanceIndex < m_sceneModels.size());
		return m_sceneModels[m_mainSceneModelInstanceIndex];
	}

private:

	AppSettings m_settings = {};

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

	uint32_t m_mainSceneModelInstanceIndex;
	std::vector<InternalModelInstance> m_sceneModels;

	D3D12_VIEWPORT m_mainViewport;
	D3D12_RECT m_mainScissor;

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

	DescriptorHeaps m_descCopies;

	std::unordered_map<ModelID, BLASBuffer> m_modelBLASes;
};

