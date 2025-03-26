#pragma once

#include "Core\GameCore.h"
#include "Core\ColorBuffer.h"
#include "Core\GpuBuffer.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"

enum ShaderID : UUID64
{
	ShaderIDInvalid = 0,
	ShaderIDSceneRenderPS,
	ShaderIDSceneRenderVS,
	ShaderIDTestCS,
	ShaderIDRCGatherCS,
	ShaderIDFlatlandSceneCS,

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

	PSOIDCount
};

__declspec(align(16)) struct CascadeInfo
{
	uint32_t probePixelSize;
	uint32_t cascadeIndex;
	float padding[2];
};

__declspec(align(16)) struct RCGlobals
{
	uint32_t probeScalingFactor;
	uint32_t rayScalingFactor;
	uint32_t probeDim0;
	float rayLength0;
	uint32_t scenePixelWidth;
	uint32_t scenePixelHeight;
};

class RadianceCascadesManager
{
public:
	static const uint32_t s_RaysPerProbe0 = 4u; // Always 4 rays per base probe.

public:
	RadianceCascadesManager() : probeDim0(0), rayLength0(0.0f) {};
	~RadianceCascadesManager();
	void Init(float rayLength0, float maxRayLength);
	void Shutdown();

	ColorBuffer& GetCascadeInterval(uint32_t cascadeIndex);
	uint32_t GetCascadeCount();

	RCGlobals FillRCGlobalsData(uint32_t scenePixelWidth, uint32_t scenePixelHeight);

	uint32_t GetProbePixelSize(uint32_t cascadeIndex); // Per dim.
	uint32_t GetProbeCount(uint32_t cascadeIndex); // Per dim.

public:
	struct ScalingFactor
	{
		uint16_t probeScalingFactor = 2u; // Scaling factor per dim.
		uint16_t rayScalingFactor = 4u;
	} scalingFactor;

	uint16_t probeDim0; // Amount of probes in one dimension of cascade 0
	float rayLength0; // The length of a ray in cascade 0.

private:
	std::vector<ColorBuffer> m_cascadeIntervals;
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
		RootEntryRCGatherSceneSampler,
		RootEntryRCGatherCount,

		RootEntryFlatlandSceneInfo = 0,
		RootEntryFlatlandSceneUAV,
		RootEntryFlatlandCount
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

	void InitializeScene();
	void InitializeShaders();
	void InitializePSOs();
	void InitializeRCResources();

	void RenderSceneImpl(Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor);
	void RunComputeFlatlandScene();
	void RunComputeRCGather();
	void UpdateViewportAndScissor();
	void UpdateGraphicsPSOs();

	ModelInstance& AddModelInstance(std::shared_ptr<Model> modelPtr);
	void AddShaderDependency(ShaderID shaderID, std::vector<PSOIDType> psoIDs);
	void RegisterPSO(PSOID psoID, PSO* psoPtr);

private:

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

	uint32_t m_mainSceneModelInstanceIndex;
	std::vector<ModelInstance> m_sceneModels;

	D3D12_VIEWPORT m_mainViewport;
	D3D12_RECT m_mainScissor;

	// Tells what PSOs are dependent on what shaders.
	std::unordered_map<UUID64, std::set<PSOIDType>> m_shaderPSODependencyMap;
	std::array<PSO*, PSOIDCount> m_usedPSOs;

	ComputePSO m_rcGatherPSO = ComputePSO(L"Compute RC Gather");
	RootSignature m_computeGatherRootSig;

	ComputePSO m_flatlandScenePSO = ComputePSO(L"Compute Flatland Scene");
	RootSignature m_computeFlatlandSceneRootSig;

	ColorBuffer m_flatlandScene = ColorBuffer({ 0.0f, 0.0f, 0.0f, 100000.0f });

	RadianceCascadesManager m_rcManager;
};

