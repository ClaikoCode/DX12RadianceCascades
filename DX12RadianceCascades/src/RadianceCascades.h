#pragma once

#include "Core\GameCore.h"
#include "Core\ColorBuffer.h"
#include "Core\DepthBuffer.h"
#include "Core\GpuBuffer.h"
#include "Core\ReadbackBuffer.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"

#include "RaytracingPSO.h"
#include "ShaderTable.h"
#include "RaytracingDispatchRayInputs.h"
#include "RuntimeResourceManager.h"

#include "RadianceCascadesManager2D.h"
#include "RadianceCascadeManager3D.h"

#if defined(_DEBUGDRAWING)
#define ENABLE_DEBUG_DRAW 1
#else
#define ENABLE_DEBUG_DRAW 0
#endif


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
	bool renderRC3D = true;
	bool visualizeRC3DGatherCascades = false;
	bool visualizeRC3DMergeCascades = false;
	bool seeCoalesceResult = false;
	int cascadeVisIndex = 0;

	bool enableCascadeProbeVis = false;
	int cascadeVisProbeIntervalIndex = 0;
	int cascadeVisProbeSubset = 256;

	float rayLength0 = 10.0f;
	bool useDepthAwareMerging = true;
	uint32_t raysPerProbe0 = 16u;
	uint32_t probesPerDim0 = 1024u;
	uint32_t cascadeLevels = 7u;
};

#define ENABLE_RT (false)
#define ENABLE_RASTER (!ENABLE_RT)
struct GlobalSettings
{
	enum RenderMode : int
	{
		RenderModeRaster = 0,
		RenderModeRT,

		// Keep this last.
		RenderModeCount
	};

	RenderMode renderMode = RenderModeRaster;

	bool renderDebugLines = ENABLE_DEBUG_DRAW;
	bool useDepthCheckForDebugLines = false;
	bool useDebugCam = false;
};

struct AppSettings
{
	GlobalSettings globalSettings;
	RadianceCascadesSettings rcSettings;
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

		RootEntryMinMaxDepthSourceInfo = 0,
		RootEntryMinMaxDepthSourceDepthUAV,
		RootEntryMinMaxDepthTargetDepthUAV,
		RootEntryMinMaxDepthCount,

		RootEntryRCRaytracingRTGSceneSRV = 0,
		RootEntryRCRaytracingRTGOutputUAV,
		RootEntryRCRaytracingRTGGlobalInfoCB,
		RootEntryRCRaytracingRTGRCGlobalsCB,
		RootEntryRCRaytracingRTGCascadeInfoCB,
		RootEntryRCRaytracingRTGDepthTextureUAV,
#if defined(_DEBUG)
		RootEntryRCRaytracingRTGRCVisCB,
#endif
		RootEntryRCRaytracingRTGCount,

		RootEntryRCRaytracingRTLGeomDataSRV = 0,
		RootEntryRCRaytracingRTLTexturesSRV,
		RootEntryRCRaytracingRTLGeomOffsetsCB,
		RootEntryRCRaytracingRTLCount,

		RootEntryRC3DMergeCascadeN1SRV = 0,
		RootEntryRC3DMergeCascadeNUAV,
		RootEntryRC3DMergeRCGlobalsCB,
		RootEntryRC3DMergeCascadeInfoCB,
		RootEntryRC3DMergeMinMaxDepthSRV,
		RootEntryRC3DMergeGlobalInfoCB,
		RootEntryRC3DMergeCount,

		RootEntryRC3DCoalesceCascade0SRV = 0,
		RootEntryRC3DCoalesceOutputTexUAV,
		RootEntryRC3DCoalesceRCGlobalsCB,
		RootEntryRC3DCoalesceCount,

		RootEntryDeferredLightingAlbedoSRV = 0,
		RootEntryDeferredLightingNormalSRV,
		RootEntryDeferredLightingDiffuseRadianceSRV,
		RootEntryDeferredLightingCount,
	};

public:
	RadianceCascades();
	~RadianceCascades();

	virtual void Startup() override;
	virtual void Cleanup() override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene() override;

	virtual void RenderUI(GraphicsContext& uiContext) override;

	virtual bool RequiresRaytracingSupport() const override { return true; }

private:
	void InitializeScene();
	void InitializePSOs();
	void InitializeRCResources();
	void InitializeRT();
	
	void RenderRaster(ColorBuffer& targetColor, DepthBuffer& targetDepth, Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor);
	void RenderRaytracing(ColorBuffer& targetColor, Camera& camera);
	void RunRCGather(Camera& camera);
	void RunRCMerge(Math::Camera& cam, ColorBuffer& minMaxDepthBuffer);
	void RenderDepthOnly(Camera& camera, DepthBuffer& targetDepth, D3D12_VIEWPORT viewPort, D3D12_RECT scissor, bool clearDepth = false);
	void BuildMinMaxDepthBuffer(DepthBuffer& sourceDepthBuffer);
	void RunComputeFlatlandScene();
	void RunComputeRCGather();
	void RunComputeRCMerge();
	void RunRCCoalesce();
	void RunComputeRCRadianceField(ColorBuffer& outputBuffer);
	void RunDeferredLightingPass(ColorBuffer& albedoBuffer, ColorBuffer& normalBuffer, ColorBuffer& diffuseRadianceBuffer, ColorBuffer& outputBuffer);
	void UpdateViewportAndScissor();

	void BuildUISettings();

	void ClearPixelBuffers();

	// Will run a compute shader that samples the source and writes to dest.
	// TODO: Take in a sampler type and change PSO to have a binding for a dynamic sampler state that is bound at runtime.
	void FullScreenCopyCompute(PixelBuffer& source, D3D12_CPU_DESCRIPTOR_HANDLE sourceSRV, ColorBuffer& dest);
	void FullScreenCopyCompute(ColorBuffer& source, ColorBuffer& dest);

	InternalModelInstance& AddModelInstance(ModelID modelID);

	// Main scene model instance is defined as the first instance added.
	InternalModelInstance& GetMainSceneModelInstance()
	{
		ASSERT(!m_sceneModels.empty());
		return m_sceneModels[0];
	}

	Math::Vector3 GetMainSceneModelCenter()
	{
		return GetMainSceneModelInstance().GetBoundingBox().GetCenter();
	}

private:

	AppSettings m_settings = {};

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

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
	TLASBuffers m_sceneTLAS;

	ComputePSO m_minMaxDepthPSO = ComputePSO(L"Min Max Depth Compute");
	RootSignature m_minMaxDepthRootSig;

	RaytracingPSO m_rcRaytracePSO = RaytracingPSO(L"RC Raytrace PSO");
	RootSignature1 m_rcRaytraceGlobalRootSig;
	RootSignature1 m_rcRaytraceLocalRootSig;

	ComputePSO m_rc3dMergePSO = ComputePSO(L"RC 3D Merge PSO");
	RootSignature m_rc3dMergeRootSig;

	ComputePSO m_rc3dCoalescePSO = ComputePSO(L"RC 3D Coalesce PSO");
	RootSignature m_rc3dCoalesceRootSig;

	GraphicsPSO m_deferredLightingPSO = GraphicsPSO(L"Deferred Lighting PSO");
	RootSignature m_deferredLightingRootSig;

	ColorBuffer m_flatlandScene = ColorBuffer({ 0.0f, 0.0f, 0.0f, 100000.0f });

	RadianceCascadesManager2D m_rcManager2D;
	RadianceCascadeManager3D m_rcManager3D;

	ColorBuffer m_albedoBuffer;

	ColorBuffer m_minMaxDepthCopy;
	ColorBuffer m_minMaxDepthMips;

	DepthBuffer m_debugCamDepthBuffer;
};

