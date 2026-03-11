#include "rcpch.h"

#include "Core\Display.h"

#include "Model\ModelLoader.h"
#include "Model\Renderer.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "Core\GameInput.h"

#include "Core\DynamicDescriptorHeap.h"
#include "GPUStructs.h"
#include "DebugDrawer.h"
#include "ShaderCompilation\ShaderCompilationManager.h"

#include "AppGUI\AppGUI.h"
#include "Profiling\GPUProfiler.h"

#include "RadianceCascades.h"

#include "TestSuiteMasters.h"
#include "TestSuiteGatherFilter.h"

#include "Core\TextureManager.h"
#include "Model\TextureConvert.h"

using namespace Microsoft::WRL;

// Mimicing how Microsoft exposes their global variables (example in Display.cpp)
namespace GameCore { extern HWND g_hWnd; }

#if defined(PROFILE_GPU)
	#if defined(RUN_TESTS)
		static std::unique_ptr<TestSuiteBase> sTestSuite = nullptr;
		bool sNeedMoreOptimizationFrames = false;
		bool sShouldWaitNextAvailableFrame = false;
	#endif // RUN_TESTS
#endif // PROFILE_GPU

constexpr size_t MAX_INSTANCES = 256;
static const DXGI_FORMAT s_FlatlandSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const std::wstring s_BackupModelPath = L"models\\Testing\\SphereTest.gltf";

#define SAMPLE_LEN_0 20.0f
#define RAYS_PER_PROBE_0 4.0f
#define CAM_FOV 90.0f

typedef ComputeContext RaytracingContext;

namespace
{
	uint32_t GetSceneColorWidth()
	{
		return Graphics::g_SceneColorBuffer.GetWidth();
	}

	uint32_t GetSceneColorHeight()
	{
		return Graphics::g_SceneColorBuffer.GetHeight();
	}

	DXGI_FORMAT GetSceneColorFormat()
	{
		return Graphics::g_SceneColorBuffer.GetFormat();
	}

	void FillGlobalInfo(GlobalInfo& globalInfo, const Camera& camera, bool useSkybox)
	{
		globalInfo.viewProjMatrix = camera.GetViewProjMatrix();

		globalInfo.invViewProjMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetViewProjMatrix()));
		globalInfo.invProjMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetProjMatrix()));
		globalInfo.invViewMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));

		globalInfo.cameraPos = camera.GetPosition();

		globalInfo.useSkybox = useSkybox;
	}

	RaytracingContext& BeginRaytracingContext(const std::wstring& name, ComPtr<ID3D12GraphicsCommandList4>& rtCommandList)
	{
		ComputeContext& cmptContext = ComputeContext::Begin(name);

		ThrowIfFailedHR(cmptContext.GetCommandList()->QueryInterface(rtCommandList.GetAddressOf()));

		return cmptContext;
	}

	void DispatchRays(RayDispatchID rayDispatchID, uint32_t width, uint32_t height, ComPtr<ID3D12GraphicsCommandList4>& rtCommandList)
	{
		const RaytracingDispatchRayInputs& rayDispatch = RuntimeResourceManager::GetRaytracingDispatch(rayDispatchID);
		D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = rayDispatch.BuildDispatchRaysDesc(width, height);
		rtCommandList->SetPipelineState1(rayDispatch.m_stateObject.Get());
		rtCommandList->DispatchRays(&rayDispatchDesc);
	}

	void AddModelsForRender(std::vector<InternalModelInstance>& modelInstances, Renderer::MeshSorter& meshSorter)
	{
		for (auto& modelInstance : modelInstances)
		{
			modelInstance.Render(meshSorter);
		}

		meshSorter.Sort();
	}

	void SetComputePSOAndRootSig(ComputeContext& cmptContext, PSOID psoID)
	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(psoID);
		cmptContext.SetPipelineState(pso);
		cmptContext.SetRootSignature(pso.GetRootSignature());
	}

	void SetGraphicsPSOAndRootSig(GraphicsContext& gfxContext, PSOID psoID)
	{
		GraphicsPSO& pso = RuntimeResourceManager::GetGraphicsPSO(psoID);
		gfxContext.SetPipelineState(pso);
		gfxContext.SetRootSignature(pso.GetRootSignature());
	}

	// Behaviour Script -> BScript
	void BScriptPosOscillation(ModelInstance* modelInstance, float deltaTime, double time)
	{
		UniformTransform& transform = modelInstance->GetTransform();
		double yPos = transform.GetTranslation().GetY();
		Vector3 centerPoint = Vector3(0.0f, (float)yPos, 0.0f);
		float amplitude = 1000.0f;      
		double frequency = 1.0;

		Vector3 position = centerPoint;
		position += Vector3(amplitude * (float)sin(frequency * time + yPos), 0.0f, 0.0f);

		transform.SetTranslation(position);
	}

	void EnableDriverBackgroundOptimizations()
	{
		ComPtr<ID3D12Device6> device6;
		ThrowIfFailedHR(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&device6)));

		// This will force the GPU driver to collect measurements in any way it wants to collect the data necessary for dynamic compilations.
		// This will be queried each frame until it says it has had enough frames and will stop collecting measurements.
		// Then these optimizations will be applied and disabled for the rest of a testing suite.
		ThrowIfFailedHR(device6->SetBackgroundProcessingMode(
			D3D12_BACKGROUND_PROCESSING_MODE_ALLOW_INTRUSIVE_MEASUREMENTS,
			D3D12_MEASUREMENTS_ACTION_KEEP_ALL,
			nullptr,
			nullptr
		));

		LOG_INFO(L"Driver background optimizations enabled.");
	}

	void EnableStablePowerState()
	{
		ComPtr<ID3D12Device5> device5;
		ThrowIfFailedHR(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&device5)));
		ThrowIfFailedHR(device5->SetStablePowerState(TRUE));

		LOG_INFO(L"Stable Power State enabled.");
	}

	// This function will return true if the device needs more frames to collect measurements for optimizations.
	bool NeedsMoreFramesForOptimization()
	{
		ComPtr<ID3D12Device6> device6;
		ThrowIfFailedHR(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&device6)));

		BOOL needsMoreFrames = TRUE;
		ThrowIfFailedHR(device6->SetBackgroundProcessingMode(
			D3D12_BACKGROUND_PROCESSING_MODE_ALLOW_INTRUSIVE_MEASUREMENTS,
			D3D12_MEASUREMENTS_ACTION_KEEP_ALL,
			nullptr, 
			&needsMoreFrames
		));

		return needsMoreFrames;
	}

	TextureRef LoadSkyboxTexture(const std::wstring& originalFile)
	{
		CompileTextureOnDemand(originalFile, 0);

		std::wstring ddsFile = Utility::RemoveExtension(originalFile) + L".dds";
		return TextureManager::LoadDDSFromFile(ddsFile);
	}
}

RadianceCascades::RadianceCascades()
	: m_mainViewport({}), 
	m_mainScissor({}), 
	m_rcManager3D(
		m_settings.rcSettings.rayLength0, 
		m_settings.rcSettings.usePreAveragedGather
	)
{
	m_sceneModels.reserve(MAX_INSTANCES);
}

RadianceCascades::~RadianceCascades()
{
}

void RadianceCascades::Startup()
{
	GPUProfiler::Initialize();
	GPU_MEMORY_BLOCK("Startup");

	{
		GPU_MEMORY_BLOCK("Microsoft Renderer");
		Renderer::Initialize();
	}
	
	{
		GPU_MEMORY_BLOCK("Program Specific Resources");

		{
			GPU_MEMORY_BLOCK("App GUI");
			AppGUI::Initialize(GameCore::g_hWnd);
		}

		InitializeScene();
		InitializePSOs();
		InitializeRCResources();

		InitializeRT();

		{
			GPU_MEMORY_BLOCK("Misc");

			uint32_t width = ::GetSceneColorWidth();
			uint32_t height = ::GetSceneColorHeight();

			m_albedoBuffer.Create(L"Albedo Buffer", width, height, 1, ::GetSceneColorFormat());
			RegisterDisplayDependentTexture(&m_albedoBuffer, TextureTypeColor);

			m_debugCamDepthBuffer.Create(L"Debug Cam Depth Buffer", width, height, Graphics::g_SceneDepthBuffer.GetFormat());
			RegisterDisplayDependentTexture(&m_debugCamDepthBuffer, TextureTypeDepth);

			m_depthBufferCopy.Create(L"Depth Copy", width, height, 1, DXGI_FORMAT_R32_FLOAT);
			RegisterDisplayDependentTexture(&m_depthBufferCopy, TextureTypeColor);
		}
	}
	
	UpdateViewportAndScissor();

#if defined(RUN_TESTS)
	::EnableStablePowerState();
	::EnableDriverBackgroundOptimizations();

	sTestSuite = std::make_unique<TestSuiteGatherFilter>(*this, m_rcManager3D, m_camera);
#endif

	// Initializing skybox textures
	{
		for (uint32_t i = 0; i < SkyboxIDCount; i++)
		{
			SkyboxID skyboxID = (SkyboxID)i;
			m_skyboxTextures[skyboxID] = ::LoadSkyboxTexture(Utils::StringToWstring(m_skyboxIDToName[skyboxID]));
		}
	}
}

void RadianceCascades::Cleanup()
{
	Graphics::g_CommandManager.IdleGPU();
	AppGUI::Shutdown();
	
	// TODO: It might make more sense for an outer scope to execute this as this class is not responsible for their initialization.
	DebugDrawer::Destroy();
	RuntimeResourceManager::Destroy();
	GPUProfiler::Destroy();

	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
{
	RuntimeResourceManager::CheckAndUpdatePSOs();
	static double sTime = 0.0;
	sTime += deltaT;

#if !defined(RUN_TESTS)
	// Mouse update
	{
		static bool isMouseExclusive = true;
		if (GameInput::IsFirstPressed(GameInput::kKey_f))
		{
			isMouseExclusive = !isMouseExclusive;
			GameInput::SetMouseExclusiveMode(isMouseExclusive);
		}

		if (isMouseExclusive)
		{
			m_cameraController->Update(deltaT);
		}
	}

	// Print the position and direction of the camera
	if (GameInput::IsFirstPressed(GameInput::kKey_p))
	{
		const Math::Vector3 cameraPos = m_camera.GetPosition();
		LOG_INFO(L"Camera position: ({}, {}, {})", (float)cameraPos.GetX(), (float)cameraPos.GetY(), (float)cameraPos.GetZ());
		const Math::Vector3 cameraDir = m_camera.GetForwardVec();
		LOG_INFO(L"Camera direction: ({}, {}, {})", (float)cameraDir.GetX(), (float)cameraDir.GetY(), (float)cameraDir.GetZ());
	}

	// Toggle UI
	if (GameInput::IsFirstPressed(GameInput::kKey_u))
	{
		m_settings.globalSettings.renderUI = !m_settings.globalSettings.renderUI;
	}

#endif


#if defined(RUN_TESTS)
	if (!sTestSuite->HasCompleted())
	{
		if (sNeedMoreOptimizationFrames == false)
		{
			if (!sShouldWaitNextAvailableFrame)
			{
				bool newTestCase = sTestSuite->Tick();

				if (newTestCase)
				{
					// Assumes that more optimization frames are to be required when new test case is instantiated.
					sNeedMoreOptimizationFrames = true;
					::EnableDriverBackgroundOptimizations();
					sShouldWaitNextAvailableFrame = true;
				}
			}
			else
			{
				sShouldWaitNextAvailableFrame = false;
			}
			
		}
		else
		{
			LOG_DEBUG(L"Waiting for GPU driver optimizations to finish before continuing with the tests.");
		}
	}
	else
	{
		sTestSuite->OutputTestSuiteToCSV();

		// All tests are done, quit the application.
		m_shouldQuit = true;
	}
#endif


	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

	{
		GPU_PROFILE_BLOCK("Scene Update", gfxContext);

		for (InternalModelInstance& modelInstance : m_sceneModels)
		{
			modelInstance.UpdateInstance(gfxContext, deltaT, sTime);
		}

		std::vector<TLASInstanceGroup> tlasInstances = GetTLASInstanceGroups();
		m_sceneTLAS.UpdateTLASInstances(gfxContext, tlasInstances);
	}


	gfxContext.Finish();
	
	UpdateViewportAndScissor();
}

void RadianceCascades::RenderScene()
{
	ClearBuffers();

	Camera renderCamera = m_camera;
	if (m_settings.globalSettings.useDebugCam)
	{
		float offset = 500.0f;
		renderCamera.SetPosition(GetMainSceneModelCenter() + Math::Vector3(offset, 0.0f, 0.0f));
		renderCamera.SetLookDirection(Math::Vector3(1.0f, 0.0f, 0.0f), Math::Vector3(0.0f, 1.0f, 0.0f));

		renderCamera.Update();

		RenderDepthOnly(renderCamera, m_debugCamDepthBuffer, m_mainViewport, m_mainScissor, true);
	}


	if (m_settings.globalSettings.renderMode == GlobalSettings::RenderModeRaster)
	{
		RenderRaster(Graphics::g_SceneColorBuffer, Graphics::g_SceneDepthBuffer, renderCamera, m_mainViewport, m_mainScissor);

		if (m_settings.rcSettings.renderRC3D)
		{
			//if (m_settings.globalSettings.useDebugCam)
			//{
			//	BuildMinMaxDepthBuffer(m_debugCamDepthBuffer);
			//}
			//else
			//{
			//	BuildMinMaxDepthBuffer(Graphics::g_SceneDepthBuffer);
			//}

			RunRCGather(renderCamera, Graphics::g_SceneDepthBuffer);

			if (m_settings.rcSettings.currentTextureVis == RadianceCascadesSettings::CascadeTextureVisGather)
			{
				FullScreenCopyCompute(m_rcManager3D.GetCascadeIntervalBuffer(m_settings.rcSettings.cascadeVisIndex), Graphics::g_SceneColorBuffer);
			}
			else if (m_settings.rcSettings.currentTextureVis == RadianceCascadesSettings::CascadeTextureVisGatherFilter)
			{
				FullScreenCopyCompute(m_rcManager3D.GetCascadeGatherFilterBuffer(m_settings.rcSettings.cascadeFilterIndex), Graphics::g_SceneColorBuffer);
			}
			else
			{
				RunRCMerge(renderCamera, m_minMaxDepthMips);

				if (m_settings.rcSettings.currentTextureVis == RadianceCascadesSettings::CascadeTextureVisMerge)
				{
					FullScreenCopyCompute(m_rcManager3D.GetCascadeIntervalBuffer(m_settings.rcSettings.cascadeVisIndex), Graphics::g_SceneColorBuffer);
				}
				else
				{
					RunRCCoalesce();

					if (m_settings.rcSettings.seeCoalesceResult)
					{
						FullScreenCopyCompute(m_rcManager3D.GetCoalesceBuffer(), Graphics::g_SceneColorBuffer);
					}
					else
					{
						// Copy scene color to albedo buffer.
						FullScreenCopyCompute(Graphics::g_SceneColorBuffer, m_albedoBuffer);
						RunDeferredLightingPass(
							m_albedoBuffer,
							Graphics::g_SceneNormalBuffer,
							m_rcManager3D.GetCoalesceBuffer(),
							Graphics::g_SceneColorBuffer
						);
					}

					if (m_rcManager3D.useGatherFiltering)
					{
						RunComputeRCGatherFilterReduction();
					}
				}
			}
		}
	}
	else if (m_settings.globalSettings.renderMode == GlobalSettings::RenderModeRT)
	{
		RenderRaytracing(Graphics::g_SceneColorBuffer, renderCamera);
	}

	// Keep render debug last in pipeline.
	if (m_settings.globalSettings.renderDebugLines)
	{
		DebugRenderCameraInfo camInfo;
		camInfo.viewProjMatrix = renderCamera.GetViewProjMatrix();
		DebugDrawer::Draw(
			camInfo, 
			Graphics::g_SceneColorBuffer, 
			Graphics::g_SceneDepthBuffer, 
			m_mainViewport, 
			m_mainScissor, 
			m_settings.globalSettings.useDepthCheckForDebugLines
		);
	}

	// Update timing info
	{
		uint64_t timestampFrequency;
		ThrowIfFailedHR(Graphics::g_CommandManager.GetCommandQueue()->GetTimestampFrequency(&timestampFrequency));

		GPUProfiler::Get().UpdateData(timestampFrequency);
	}

#if defined(RUN_TESTS)
	
	if (sNeedMoreOptimizationFrames)
	{
		sNeedMoreOptimizationFrames = NeedsMoreFramesForOptimization();

		if (sNeedMoreOptimizationFrames == false)
		{
			ComPtr<ID3D12Device6> device6;
			ThrowIfFailedHR(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&device6)));

			// Disable all dynamic optimizations when optimizations have fully finished.
			// When disabling background processing is combined with commiting results at high priority,
			// the GPU driver will apply all optimizations that it has collected so far and will stop when it returns from the function.
			// For the D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY to work the system needs to have developer mode enabled.
			ThrowIfFailedHR(device6->SetBackgroundProcessingMode(
				D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM,
				D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY,
				nullptr,
				nullptr
			));

			// Clear profile data.
			GPUProfiler::Get().ClearProfiles();

			LOG_INFO(L"GPU driver optimizations have been applied and disabled for the rest of the testing suite.");
		}
	}
#endif
}

void RadianceCascades::RenderUI(GraphicsContext& uiContext)
{

#if !defined(DRAW_UI) || defined(RUN_TESTS)
	return; // Skip UI rendering when test running is enabled.
#endif

	if (!m_settings.globalSettings.renderUI)
	{
		return;
	}

	AppGUI::NewFrame();

	{
		DrawSettingsUI();

#if defined(PROFILE_GPU)
		GPUProfiler::Get().DrawProfilerUI();
#endif
	}

	// Render UI
	AppGUI::Render(uiContext);

}

bool RadianceCascades::IsDone()
{
	bool isDone = m_shouldQuit || GameInput::IsFirstPressed(GameInput::kKey_escape);

	if( isDone )
	{
		LOG_INFO(L"RadianceCascades application is done and will quit.");
	}

	return isDone;
}

void RadianceCascades::ResizeToResolutionTarget(ResolutionTarget resolutionTarget)
{
	// TODO: Fix this quick and dirty solution that is due to bad architecture for this feature.
	static ResolutionTarget prevResTarget = ResolutionTargetInvalid;

	if (prevResTarget == resolutionTarget)
	{
		return;
	}
	prevResTarget = resolutionTarget;

	uint32_t width, height;
	ResolutionTargetToDimensions(resolutionTarget, width, height);

	switch (resolutionTarget)
	{
		case ResolutionTarget1080p:
			Display::Set1080p();
			break;
	
		case ResolutionTarget1440p:
			Display::Set1440p();
			break;
	
		case ResolutionTarget2160p:
			Display::Set2160p();
			break;
	
		default:
			ASSERT(false, "Invalid resolution target");
	}

	LOG_INFO(L"Resizing buffers ({}x{}).", width, height);

	Graphics::g_CommandManager.IdleGPU();

	for (auto& textureRef : m_displayDependentTextures)
	{
		ASSERT(textureRef.pixelBuffer != nullptr);

		wchar_t nameRaw[128] = {0};
#if defined(DEBUG)
		UINT size = sizeof(nameRaw);
		ThrowIfFailedHR(textureRef.pixelBuffer->GetResource()->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nameRaw));
#endif 
		std::wstring name = std::wstring(nameRaw);

		switch (textureRef.pixelBufferType)
		{
			case TextureTypeColor:
			{
				ColorBuffer* colorTexture = dynamic_cast<ColorBuffer*>(textureRef.pixelBuffer);
				
				uint32_t numMips = colorTexture->GetNumMipMaps();
				ASSERT(numMips == 0, "Does not support resizing textures with mip maps.");

				// At creation, num mips includes the 0:th mip (the texture itself). Using 0 means all mips.
				colorTexture->Create(name, width, height, numMips + 1, colorTexture->GetFormat());

				// Update resource manager descriptors.
				RuntimeResourceManager::UpdateDescriptor(colorTexture->GetUAV());
				RuntimeResourceManager::UpdateDescriptor(colorTexture->GetSRV());

				break;
			}

			case TextureTypeDepth:
			{
				DepthBuffer* depthTexture = dynamic_cast<DepthBuffer*>(textureRef.pixelBuffer);
				depthTexture->Create(name, width, height, depthTexture->GetFormat());
				break;
			}

			default:
				LOG_ERROR(L"Texture type was not identifable during resize.");
		}
	}

	m_rcManager3D.Resize(width, height);
}

void RadianceCascades::InitializeScene()
{
	GPU_MEMORY_BLOCK("Scene");

	// Super lazy, I know.
	int sceneIndex = 6;

#if defined(RUN_TESTS)
	sceneIndex = 3; 
#endif

	if (sceneIndex == 0) // Default scene
	{
		{
			AddSceneModel(ModelIDSponza, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });
		}

		{
			Math::Vector3 modelCenter = GetMainSceneModelCenter();
			
			AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(880.0f, -550.0f, 90.0f )});

			for (int i = 0; i < 5; i++)
			{
				float yPos = (100.0f * i) - 500.0f;
				AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(0.0f, yPos, 0.0f) + modelCenter, {}, ::BScriptPosOscillation });
			}
			
			AddSceneModel(ModelIDSphereTest, { 50.0f, Vector3(200.0f, -300.0f, 500.0f) + modelCenter });
			AddSceneModel(ModelIDSphereTest, { 30.0f, Vector3(200.0f, -300.0f, -500.0f) + modelCenter });
			
			AddSceneModel(ModelIDLantern, { 25.0f, Vector3(1100.0f, -500.0f, 0.0f) + modelCenter, Math::Quaternion(0.0f, Math::XM_PI, 0.0f)});
		}


	}
	else if (sceneIndex == 1)
	{
		AddSceneModel(ModelIDSphereTest, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });
	}
	else if (sceneIndex == 2)
	{
		AddSceneModel(ModelIDLantern, { 100.0f });
	}
	else if (sceneIndex == 3) // Testing scene
	{
		AddSceneModel(ModelIDSponza, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });

		Math::Vector3 modelCenter = GetMainSceneModelCenter();
		float yPos = -100.0f;
		AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(0.0f, yPos, 0.0f) + modelCenter, {}, ::BScriptPosOscillation });
	}
	else if (sceneIndex == 6) // Presentation photos
	{
		AddSceneModel(ModelIDSponza, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });

		Math::Vector3 modelCenter = GetMainSceneModelCenter();

		AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(0.0f, -550.0f, 0.0f) });
	}

	// Setup camera
	{
		float heightOverWidth = (float)::GetSceneColorHeight() / (float)::GetSceneColorWidth();
		m_camera.SetAspectRatio(heightOverWidth);
		m_camera.SetFOV(Utils::HorizontalFovToVerticalFov(Math::XMConvertToRadians(CAM_FOV), 1.0f / heightOverWidth));

		
		Vector3 modelCenter = GetMainSceneModelCenter();
		m_camera.SetEyeAtUp(Vector3(0.0f, -560.0f, -200.0f), modelCenter - Vector3(0.0f, 300.0f, 0.0f), Vector3(kYUnitVector));
		//m_camera.SetEyeAtUp(Vector3(-250.0f, -325.0f, 0.0f), Vector3(0.0f, -550.0f, 0.0f), Vector3(kYUnitVector));
		m_camera.SetZRange(0.5f, 5000.0f);
		
		m_cameraController.reset(new FlyingFPSCamera(m_camera, Vector3(kYUnitVector)));
	}
}

void RadianceCascades::InitializePSOs()
{
	GPU_MEMORY_BLOCK("PSOs");

	// Pointers to used PSOs
	{
		RuntimeResourceManager::RegisterPSO(PSOIDFirstExternalPSO,			&Renderer::sm_PSOs[9],			PSOTypeGraphics);
		RuntimeResourceManager::RegisterPSO(PSOIDSecondExternalPSO,			&Renderer::sm_PSOs[11],			PSOTypeGraphics);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeRCGatherPSO,		&m_rcGatherPSO,					PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeFlatlandScenePSO,	&m_flatlandScenePSO,			PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDGatherFilterReductionPSO,	&m_gatherFilterReductionPSO,	PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeFullScreenCopyPSO,	&m_fullScreenCopyComputePSO,	PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRaytracingTestPSO,			&m_rtTestPSO,					PSOTypeRaytracing);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeMinMaxDepthPSO,		&m_minMaxDepthPSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRCRaytracingPSO,			&m_rcRaytracePSO,				PSOTypeRaytracing);
		RuntimeResourceManager::RegisterPSO(PSOIDRC3DMergePSO,				&m_rc3dMergePSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRC3DCoalescePSO,			&m_rc3dCoalescePSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDDeferredLightingPSO,		&m_deferredLightingPSO,			PSOTypeGraphics);
		RuntimeResourceManager::RegisterPSO(PSOIDSkyboxPSO,					&m_skyboxPSO,					PSOTypeGraphics);
	}

	// Overwrite and update external PSO shaders.
	{
		std::vector<ShaderID> sceneRenderShaderIDs = { ShaderIDSceneRenderVS, ShaderIDSceneRenderPS };
		RuntimeResourceManager::SetShadersForPSO(PSOIDFirstExternalPSO, sceneRenderShaderIDs, true);
		RuntimeResourceManager::SetShadersForPSO(PSOIDSecondExternalPSO, sceneRenderShaderIDs, true);
	}

#pragma region GraphicsPSOs

	{
		GraphicsPSO& pso = RuntimeResourceManager::GetGraphicsPSO(PSOIDDeferredLightingPSO);
		RuntimeResourceManager::SetShadersForPSO(PSOIDDeferredLightingPSO, { ShaderIDFullScreenQuadVS, ShaderIDDeferredLightingPassPS });

		RootSignature& rootSig = m_deferredLightingRootSig;
		rootSig.Reset(
			RootEntryDeferredLightingCount,
			1
#if defined(_DEBUGDRAWING)
			,false
#endif
		);
		rootSig[RootEntryDeferredLightingAlbedoSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryDeferredLightingNormalSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		rootSig[RootEntryDeferredLightingDiffuseRadianceSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);
		rootSig[RootEntryDeferredLightingCascade0MinMaxDepthSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1);
		rootSig[RootEntryDeferredLightingDepthBufferSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 1);
		rootSig[RootEntryDeferredLightingCascade0SRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1);
		rootSig[RootEntryDeferredLightingGlobalInfoCB].InitAsConstantBuffer(0);
		rootSig[RootEntryDeferredLightingRCGlobalsCB].InitAsConstantBuffer(1);
		{
			SamplerDesc samplerState = Graphics::SamplerLinearBorderDesc;
			rootSig.InitStaticSampler(0, samplerState);
		}
		rootSig.Finalize(L"Deferred Lighting");

		pso.SetRasterizerState(Graphics::RasterizerTwoSided);
		pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		pso.SetBlendState(Graphics::BlendTraditional);
		pso.SetDepthStencilState(Graphics::DepthStateDisabled);
		pso.SetRenderTargetFormats(1, &Graphics::g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		GraphicsPSO& pso = RuntimeResourceManager::GetGraphicsPSO(PSOIDSkyboxPSO);
		RuntimeResourceManager::SetShadersForPSO(PSOIDSkyboxPSO, { ShaderIDSkyboxVS, ShaderIDSkyboxPS });

		RootSignature& rootSig = m_skyboxRootSig;
		rootSig.Reset(RootEntrySkyboxCount, 1, false);
		rootSig[RootEntrySkyboxGlobalInfoCB].InitAsConstantBuffer(0);
		rootSig[RootEntrySkyboxEquirectangularSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		{
			SamplerDesc samplerState = Graphics::SamplerLinearWrapDesc;
			rootSig.InitStaticSampler(0, samplerState);
		}
		rootSig.Finalize(L"Skybox");

		pso.SetRasterizerState(Graphics::RasterizerTwoSided);
		pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		pso.SetBlendState(Graphics::BlendTraditional);
		pso.SetDepthStencilState(Graphics::DepthStateTestEqual);
		pso.SetRenderTargetFormat(Graphics::g_SceneColorBuffer.GetFormat(), Graphics::g_SceneDepthBuffer.GetFormat());

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

#pragma endregion

#pragma region ComputePSOs
	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeRCGatherPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeRCGatherPSO, ShaderIDRCGatherCS);

		RootSignature& rootSig = m_computeGatherRootSig;
		rootSig.Reset(RootEntryRCGatherCount, 1);
		rootSig[RootEntryRCGatherGlobals].InitAsConstantBuffer(0);
		rootSig[RootEntryRCGatherCascadeInfo].InitAsConstantBuffer(1);
		rootSig[RootEntryRCGatherCascadeUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRCGatherSceneSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);

		{
			SamplerDesc sampler = Graphics::SamplerPointBorderDesc;
			rootSig.InitStaticSampler(0, sampler);
		}

		rootSig.Finalize(L"Compute RC Gather");

		pso.SetRootSignature(rootSig);

		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeFullScreenCopyPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeFullScreenCopyPSO, ShaderIDDirectCopyCS);

		RootSignature& rootSig = m_fullScreenCopyComputeRootSig;
		rootSig.Reset(
			RootEntryFullScreenCopyComputeCount, 
			2
#if defined(_DEBUGDRAWING)
			,false
#endif
		);
		rootSig[RootEntryFullScreenCopyComputeSource].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDest].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDestInfo].InitAsConstants(0, 2);

		{
			SamplerDesc pointSampler = Graphics::SamplerPointBorderDesc;
			rootSig.InitStaticSampler(0, pointSampler);

			SamplerDesc linearSampler = Graphics::SamplerLinearBorderDesc;
			rootSig.InitStaticSampler(1, linearSampler);
		}

		rootSig.Finalize(L"Compute Full Screen Copy");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeFlatlandScenePSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeFlatlandScenePSO, ShaderIDFlatlandSceneCS);

		RootSignature& rootSig = m_computeFlatlandSceneRootSig;
		rootSig.Reset(RootEntryFlatlandCount, 0);
		rootSig[RootEntryFlatlandSceneInfo].InitAsConstants(0, 2);
		rootSig[RootEntryFlatlandSceneUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig.Finalize(L"Compute Flatland Scene");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeMinMaxDepthPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeMinMaxDepthPSO, ShaderIDMinMaxDepthCS);

		RootSignature& rootSig = m_minMaxDepthRootSig;
		rootSig.Reset(RootEntryMinMaxDepthCount);
		rootSig[RootEntryMinMaxDepthSourceInfo].InitAsConstantBuffer(0);
		rootSig[RootEntryMinMaxDepthSourceDepthUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryMinMaxDepthTargetDepthUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		rootSig.Finalize(L"Min Max Depth");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDRC3DMergePSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDRC3DMergePSO, ShaderIDRCMerge3DCS);

		RootSignature& rootSig = m_rc3dMergeRootSig;
		rootSig.Reset(RootEntryRC3DMergeCount, 1);
		rootSig[RootEntryRC3DMergeCascadeN1SRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryRC3DMergeCascadeNUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRC3DMergeRCGlobalsCB].InitAsConstantBuffer(0);
		rootSig[RootEntryRC3DMergeCascadeInfoCB].InitAsConstantBuffer(1);
		rootSig[RootEntryRC3DMergeMinMaxDepthSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		rootSig[RootEntryRC3DMergeGlobalInfoCB].InitAsConstantBuffer(2);
		{
			SamplerDesc sampler = Graphics::SamplerLinearBorderDesc;
			sampler.SetBorderColor(Color(0.0f, 0.0f, 0.0f, 1.0f)); // Alpha of 1 to set visibility term.
			rootSig.InitStaticSampler(0, sampler);
		}
		rootSig.Finalize(L"RC 3D Merge");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDRC3DCoalescePSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDRC3DCoalescePSO, ShaderIDRCCoalesce3DCS);

		RootSignature& rootSig = m_rc3dCoalesceRootSig;
		rootSig.Reset(RootEntryRC3DCoalesceCount);
		rootSig[RootEntryRC3DCoalesceCascade0SRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryRC3DCoalesceOutputTexUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRC3DCoalesceRCGlobalsCB].InitAsConstantBuffer(0);
		rootSig.Finalize(L"RC 3D Coalesce");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDGatherFilterReductionPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDGatherFilterReductionPSO, ShaderIDGatherFilterReduceCS);

		RootSignature& rootSig = m_gatherFilterReductionRootSig;
		rootSig.Reset(RootEntryGatherFilterReductionCount, 0);
		rootSig[RootEntryGatherFilterReductionInfo].InitAsConstantBuffer(0);
		rootSig[RootEntryGatherFilterReductionTexture].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryGatherFilterReductionByteBuffer].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig.Finalize(L"Gather Filter Reduction");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}
#pragma endregion

#pragma region RaytracingPSOs
	{
		RaytracingPSO& pso = RuntimeResourceManager::GetRaytracingPSO(PSOIDRaytracingTestPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDRaytracingTestPSO, ShaderIDRaytracingTestRT);

		RootSignature1& globalRootSig = m_rtTestGlobalRootSig;
		globalRootSig.Reset(
			RootEntryRTGCount, 1
#if defined(_DEBUGDRAWING)
			, true
#endif
		);

		globalRootSig[RootEntryRTGSRV].InitAsShaderResourceView(0);
		globalRootSig[RootEntryRTGUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		globalRootSig[RootEntryRTGParamCB].InitAsConstantBufferView(0);
		globalRootSig[RootEntryRTGInfoCB].InitAsConstantBufferView(1);

		{
			SamplerDesc sampler = Graphics::SamplerLinearWrapDesc;
			globalRootSig.InitStaticSampler(0, sampler);
		}

		globalRootSig.Finalize(L"Regular RT Global Root Signature");
		pso.SetGlobalRootSignature(&globalRootSig);

		RootSignature1& localRootSig = m_rtTestLocalRootSig;
		const uint32_t localRootSigSpace = 1;
		localRootSig.Reset(RootEntryRTLCount, 0);
		localRootSig[RootEntryRTLGeometryDataSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRTLTextureSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRTLOffsetConstants].InitAsConstants(2, 0, localRootSigSpace, D3D12_SHADER_VISIBILITY_ALL);
		localRootSig.Finalize(L"Local Root Signature", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		pso.SetLocalRootSignature(&localRootSig);

		pso.SetPayloadAndAttributeSize(4, 8);

		pso.SetHitGroup(s_HitGroupName, D3D12_HIT_GROUP_TYPE_TRIANGLES);
		pso.SetClosestHitShader(L"ClosestHitShader");

		pso.SetMaxRayRecursionDepth(1);

		pso.Finalize();
	}

	{
		RaytracingPSO& pso = RuntimeResourceManager::GetRaytracingPSO(PSOIDRCRaytracingPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDRCRaytracingPSO, ShaderIDRCRaytraceRT);

		RootSignature1& globalRootSig = m_rcRaytraceGlobalRootSig;
		globalRootSig.Reset(
			RootEntryRCRaytracingRTGCount, 1
#if defined(_DEBUGDRAWING)
			, true
#endif
		);

		globalRootSig[RootEntryRCRaytracingRTGSceneSRV].InitAsShaderResourceView(0);
		globalRootSig[RootEntryRCRaytracingRTGSkyboxSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		// TODO: These three could simply be a single range.
		globalRootSig[RootEntryRCRaytracingRTGOutputUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		globalRootSig[RootEntryRCRaytracingRTGGatherFilterNUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		globalRootSig[RootEntryRCRaytracingRTGGatherFilterN1UAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1);
		globalRootSig[RootEntryRCRaytracingRTGGlobalInfoCB].InitAsConstantBufferView(0);
		globalRootSig[RootEntryRCRaytracingRTGRCGlobalsCB].InitAsConstantBufferView(1);
		globalRootSig[RootEntryRCRaytracingRTGCascadeInfoCB].InitAsConstantBufferView(2);
#if defined(_DEBUG)
		globalRootSig[RootEntryRCRaytracingRTGRCVisCB].InitAsConstantBufferView(127);
#endif
		globalRootSig[RootEntryRCRaytracingRTGDepthTextureUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 1);
		{
			SamplerDesc sampler = Graphics::SamplerLinearWrapDesc;
			globalRootSig.InitStaticSampler(0, sampler);
		}

		globalRootSig.Finalize(L"RC RT Global Root Signature");
		pso.SetGlobalRootSignature(&globalRootSig);

		RootSignature1& localRootSig = m_rcRaytraceLocalRootSig;
		localRootSig.Reset(RootEntryRCRaytracingRTLCount, 0);
		const uint32_t localRootSigSpace = 1;
		localRootSig.Reset(RootEntryRCRaytracingRTLCount, 0);
		localRootSig[RootEntryRCRaytracingRTLGeomDataSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRCRaytracingRTLTexturesSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRCRaytracingRTLGeomOffsetsCB].InitAsConstants(2, 0, localRootSigSpace, D3D12_SHADER_VISIBILITY_ALL);

		localRootSig.Finalize(L"Local Root Signature", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		pso.SetLocalRootSignature(&localRootSig);

		// Payload: int2 probeIndex, float4 result
		pso.SetPayloadAndAttributeSize(8 + 4 * 4, 8);

		pso.SetHitGroup(s_HitGroupName, D3D12_HIT_GROUP_TYPE_TRIANGLES);
		pso.SetClosestHitShader(L"ClosestHitShader");

		pso.SetMaxRayRecursionDepth(1);

		pso.Finalize();
	}
#pragma endregion
}

void RadianceCascades::InitializeRCResources()
{
	GPU_MEMORY_BLOCK("RC Resources");

	// RC 3D
	{
		GPU_MEMORY_BLOCK("RC 3D");

		uint32_t screenWidth = ::GetSceneColorWidth();
		uint32_t screenHeight = ::GetSceneColorHeight();

		m_minMaxDepthMips.Create(L"Min Max Depth Mips", screenWidth / 2, screenHeight / 2, 0, DXGI_FORMAT_R32G32_FLOAT);

		m_rcManager3D.Generate(
			m_settings.rcSettings.raysPerProbe0, 
			m_settings.rcSettings.probeSpacing0, 
			screenWidth,
			screenHeight,
			m_settings.rcSettings.maxCascadeCount
		);
	}
	
}

void RadianceCascades::InitializeRT()
{
	GPU_MEMORY_BLOCK("RT Resources");

	// Initialize TLASes.
	std::unordered_map<ModelID, std::vector<Utils::GPUMatrix>> blasInstances = GetGroupedModelInstances();

	// Initialize RT dispatch inputs.
	{
		std::set<ModelID> modelIDs = {};
		for (auto& [key, _] : blasInstances)
		{
			modelIDs.insert(key);
		}

		RuntimeResourceManager::BuildRaytracingDispatchInputs(PSOIDRaytracingTestPSO, modelIDs, RayDispatchIDTest);
		RuntimeResourceManager::BuildRaytracingDispatchInputs(PSOIDRCRaytracingPSO, modelIDs, RayDispatchIDRCRaytracing);
	}

	{
		std::vector<TLASInstanceGroup> tlasInstances = GetTLASInstanceGroups();
		m_sceneTLAS.Init();
	}
}

void RadianceCascades::RenderRaster(ColorBuffer& targetColor, DepthBuffer& targetDepth, Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor)
{
	GlobalConstants globals = {};
	{
		// Update global constants
		float sunOriantation = -0.5f;
		float sunInclination = 0.75f;
		float costheta = cosf(sunOriantation);
		float sintheta = sinf(sunOriantation);
		float cosphi = cosf(sunInclination * 3.14159f * 0.5f);
		float sinphi = sinf(sunInclination * 3.14159f * 0.5f);

		Vector3 SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

		globals.SunDirection = SunDirection;
		globals.SunIntensity = Vector3(Scalar(0.5f));
	}

	GlobalInfo globalInfo = {};
	::FillGlobalInfo(globalInfo, camera, m_settings.globalSettings.useSkybox);

	Renderer::MeshSorter meshSorter = Renderer::MeshSorter(Renderer::MeshSorter::kDefault);
	meshSorter.SetCamera(camera);
	meshSorter.SetViewport(viewPort);
	meshSorter.SetScissor(scissor);
	meshSorter.SetDepthStencilTarget(targetDepth);
	meshSorter.AddRenderTarget(targetColor);

	::AddModelsForRender(m_sceneModels, meshSorter);

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	// Zpass
	{
		GPU_PROFILE_BLOCK("Z Pass", gfxContext);

		gfxContext.TransitionResource(targetDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		meshSorter.RenderMeshes(Renderer::MeshSorter::kZPass, gfxContext, globals);
	}
	
	// Opaque pass
	{
		GPU_PROFILE_BLOCK("Opaque Pass", gfxContext);

		gfxContext.TransitionResource(targetDepth, D3D12_RESOURCE_STATE_DEPTH_READ, true);
		gfxContext.TransitionResource(targetColor, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		gfxContext.SetRenderTarget(targetColor.GetRTV(), targetDepth.GetDSV());
		gfxContext.SetViewportAndScissor(viewPort, scissor);

#if defined(_DEBUGDRAWING)
		DebugDrawer::BindDebugBuffers(gfxContext, Renderer::kNumRootBindings);
#endif

		meshSorter.RenderMeshes(Renderer::MeshSorter::kOpaque, gfxContext, globals);
	}

	// Skybox pass
	if(m_settings.globalSettings.useSkybox)
	{
		GPU_PROFILE_BLOCK("Skybox Pass", gfxContext);

		gfxContext.SetPipelineState(RuntimeResourceManager::GetGraphicsPSO(PSOIDSkyboxPSO));
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetRootSignature(m_skyboxRootSig);

		gfxContext.SetDynamicConstantBufferView(RootEntrySkyboxGlobalInfoCB, sizeof(GlobalInfo), &globalInfo);
		gfxContext.SetDynamicDescriptor(RootEntrySkyboxEquirectangularSRV, 0, GetCurrentSkybox().GetSRV());

		// Not required but is done to be explicit that no vertex buffer is being used for skybox rendering. 
		// Everything is done in shader.
		D3D12_VERTEX_BUFFER_VIEW* vbView = nullptr;
		gfxContext.GetCommandList()->IASetVertexBuffers(0, 1, vbView);

		// Matches index count in shader.
		gfxContext.DrawInstanced(36, 1);
	}

	gfxContext.Finish(true);
}

void RadianceCascades::RenderRaytracing(ColorBuffer& targetColor, Camera& camera)
{
	DescriptorHandle colorDescriptorHandle = RuntimeResourceManager::GetDescCopy(targetColor.GetSRV());

	RTParams rtParams = {};
	rtParams.dispatchHeight = targetColor.GetHeight();
	rtParams.dispatchWidth = targetColor.GetWidth();
	rtParams.rayFlags = D3D12_RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
	rtParams.holeSize = 0.0f;

	GlobalInfo rtGlobalInfo = {};
	::FillGlobalInfo(rtGlobalInfo, camera, m_settings.globalSettings.useSkybox);

	ComPtr<ID3D12GraphicsCommandList4> rtCommandList = nullptr;
	RaytracingContext& rtContext = ::BeginRaytracingContext(L"Render Raytracing", rtCommandList);

	{
		GPU_PROFILE_BLOCK("RT Pass", rtContext);

		// Transition resources
		{
			rtContext.TransitionResource(targetColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		}

		ID3D12DescriptorHeap* pDescriptorHeaps[] = { RuntimeResourceManager::GetDescriptorHeapPtr() };
		rtCommandList->SetDescriptorHeaps(1, pDescriptorHeaps);
		rtCommandList->SetComputeRootSignature(m_rtTestGlobalRootSig.GetSignature());
		rtCommandList->SetComputeRootShaderResourceView(RootEntryRTGSRV, m_sceneTLAS.GetBVH());
		rtCommandList->SetComputeRootDescriptorTable(RootEntryRTGUAV, colorDescriptorHandle);
		rtContext.SetDynamicConstantBufferView(RootEntryRTGParamCB, sizeof(RTParams), &rtParams);
		rtContext.SetDynamicConstantBufferView(RootEntryRTGInfoCB, sizeof(GlobalInfo), &rtGlobalInfo);

#if defined(_DEBUGDRAWING)
		DebugDrawer::BindDebugBuffers(rtContext, RootEntryRTGCount);
#endif

		::DispatchRays(RayDispatchIDTest, targetColor.GetWidth(), targetColor.GetHeight(), rtCommandList);
	}

	rtContext.Finish(true);
}

void RadianceCascades::RunRCGather(Camera& camera, DepthBuffer& sourceDepthBuffer)
{
	ColorBuffer& destDepthBuffer = m_depthBufferCopy;

	GlobalInfo globalInfo = {};
	::FillGlobalInfo(globalInfo, camera, m_settings.globalSettings.useSkybox);

	RCGlobals rcGlobalInfo = {};
	m_rcManager3D.FillRCGlobalInfo(rcGlobalInfo);

	ComPtr<ID3D12GraphicsCommandList4> rtCommandList = nullptr;
	RaytracingContext& rtContext = ::BeginRaytracingContext(L"RC Gather Pass", rtCommandList);

	{
		GPU_PROFILE_BLOCK("RC Gather", rtContext);

		// Copy depth buffer to UAV bindable buffer.
		{
			rtContext.TransitionResource(sourceDepthBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
			rtContext.TransitionResource(destDepthBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
			rtContext.CopySubresource(destDepthBuffer, 0, sourceDepthBuffer, 0);
		}

		ID3D12DescriptorHeap* pDescriptorHeaps[] = { RuntimeResourceManager::GetDescriptorHeapPtr() };
		rtCommandList->SetDescriptorHeaps(1, pDescriptorHeaps);

		// TODO: Add this to ::DispatchRays() by saving the global root sig with a specific PSO so they can be fetched together.
		rtCommandList->SetComputeRootSignature(m_rcRaytraceGlobalRootSig.GetSignature());

		{
			rtCommandList->SetComputeRootShaderResourceView(RootEntryRCRaytracingRTGSceneSRV, m_sceneTLAS.GetBVH());
			rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGSkyboxSRV, RuntimeResourceManager::GetDescCopy(GetCurrentSkybox().GetSRV()));
			rtContext.SetDynamicConstantBufferView(RootEntryRCRaytracingRTGGlobalInfoCB, sizeof(GlobalInfo), &globalInfo);
			rtContext.SetDynamicConstantBufferView(RootEntryRCRaytracingRTGRCGlobalsCB, sizeof(RCGlobals), &rcGlobalInfo);

#if defined(_DEBUG)

			CascadeVisInfo cascadeVisInfo = {};
			cascadeVisInfo.enableProbeVis = m_settings.rcSettings.enableCascadeProbeVis;
			cascadeVisInfo.cascadeVisIndex = m_settings.rcSettings.cascadeVisProbeIntervalIndex;
			cascadeVisInfo.probeSubset = m_settings.rcSettings.cascadeVisProbeSubset;

			rtContext.SetDynamicConstantBufferView(RootEntryRCRaytracingRTGRCVisCB, sizeof(CascadeVisInfo), &cascadeVisInfo);
#endif

#if defined(_DEBUGDRAWING)
			DebugDrawer::BindDebugBuffers(rtContext, RootEntryRCRaytracingRTGCount);
#endif
		}

		// Bind depth buffer as UAV.
		{
			rtContext.TransitionResource(destDepthBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			const DescriptorHandle& depthUAV = RuntimeResourceManager::GetDescCopy(destDepthBuffer.GetUAV());
			rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGDepthTextureUAV, depthUAV);
		}

		uint32_t baseCascade = 0;
		uint32_t maxCascade = uint32_t(m_rcManager3D.GetCascadeIntervalCount());

		int cascdeVisResultIndex = m_settings.rcSettings.cascadeVisResultIndex;
		if (cascdeVisResultIndex > -1 && (uint32_t)cascdeVisResultIndex < maxCascade)
		{
			baseCascade = m_settings.rcSettings.cascadeVisResultIndex;
			maxCascade = baseCascade + 1;
		}

		for (uint32_t cascadeIndex = baseCascade; cascadeIndex < maxCascade; cascadeIndex++)
		{
			CascadeInfo cascadeInfo = {};
			cascadeInfo.cascadeIndex = cascadeIndex;

			rtContext.SetDynamicConstantBufferView(RootEntryRCRaytracingRTGCascadeInfoCB, sizeof(CascadeInfo), &cascadeInfo);

			ColorBuffer& cascadeBuffer = m_rcManager3D.GetCascadeIntervalBuffer(cascadeIndex);
			rtContext.InsertUAVBarrier(cascadeBuffer);
			rtContext.TransitionResource(cascadeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			const DescriptorHandle& rcBufferUAV = RuntimeResourceManager::GetDescCopy(cascadeBuffer.GetUAV());
			rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGOutputUAV, rcBufferUAV);

			if (m_rcManager3D.useGatherFiltering)
			{
				// Last cascade has no higher cascade to filter.
				if (cascadeIndex < m_rcManager3D.GetCascadeIntervalCount() - 1)
				{
					ColorBuffer& gatherFilterBufferN1 = m_rcManager3D.GetCascadeGatherFilterBuffer(cascadeIndex);
					rtContext.InsertUAVBarrier(gatherFilterBufferN1);
					rtContext.TransitionResource(gatherFilterBufferN1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

					const DescriptorHandle& rcFilterBufferN1UAV = RuntimeResourceManager::GetDescCopy(gatherFilterBufferN1.GetUAV());
					rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGGatherFilterN1UAV, rcFilterBufferN1UAV);
				}

				// First cascade has no prior cascade to be filtered by.
				if (cascadeIndex > 0)
				{
					ColorBuffer& gatherFilterBufferN = m_rcManager3D.GetCascadeGatherFilterBuffer(cascadeIndex - 1);

					const DescriptorHandle& rcFilterBufferNUAV = RuntimeResourceManager::GetDescCopy(gatherFilterBufferN.GetUAV());
					rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGGatherFilterNUAV, rcFilterBufferNUAV);
				}
			}

			::DispatchRays(
				RayDispatchIDRCRaytracing,
				cascadeBuffer.GetWidth(),
				cascadeBuffer.GetHeight(),
				rtCommandList
			);
		}
	}

	rtContext.Finish(true);
}

void RadianceCascades::RunRCMerge(Math::Camera& cam, ColorBuffer& minMaxDepthBuffer)
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Merge Compute");

	{
		GPU_PROFILE_BLOCK("RC Merge Pass", cmptContext);

		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDRC3DMergePSO);
		cmptContext.SetPipelineState(pso);
		cmptContext.SetRootSignature(pso.GetRootSignature());

		RCGlobals rcGlobals = {};
		m_rcManager3D.FillRCGlobalInfo(rcGlobals);

		GlobalInfo globalInfo = {};
		::FillGlobalInfo(globalInfo, cam, m_settings.globalSettings.useSkybox);

		cmptContext.SetDynamicConstantBufferView(RootEntryRC3DMergeRCGlobalsCB, sizeof(RCGlobals), &rcGlobals);
		cmptContext.SetDynamicConstantBufferView(RootEntryRC3DMergeGlobalInfoCB, sizeof(GlobalInfo), &globalInfo);

		cmptContext.TransitionResource(m_depthBufferCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmptContext.SetDynamicDescriptor(RootEntryRC3DMergeMinMaxDepthSRV, 0, m_depthBufferCopy.GetSRV());

#if defined(_DEBUGDRAWING)
		DebugDrawer::BindDebugBuffers(cmptContext, RootEntryRC3DMergeCount);
#endif

		for (uint32_t i = m_rcManager3D.GetCascadeIntervalCount() - 1; i >= 1; i--)
		{
			CascadeInfo cascadeInfo = {};
			cascadeInfo.cascadeIndex = i - 1;

			cmptContext.SetDynamicConstantBufferView(RootEntryRC3DMergeCascadeInfoCB, sizeof(CascadeInfo), &cascadeInfo);

			ColorBuffer& cascadeN1 = m_rcManager3D.GetCascadeIntervalBuffer(i);
			ColorBuffer& cascadeN = m_rcManager3D.GetCascadeIntervalBuffer(i - 1);

			cmptContext.TransitionResource(cascadeN1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cmptContext.TransitionResource(cascadeN, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmptContext.FlushResourceBarriers();

			cmptContext.SetDynamicDescriptor(RootEntryRC3DMergeCascadeN1SRV, 0, cascadeN1.GetSRV());
			cmptContext.SetDynamicDescriptor(RootEntryRC3DMergeCascadeNUAV, 0, cascadeN.GetUAV());

			cmptContext.Dispatch2D(cascadeN.GetWidth(), cascadeN.GetHeight());
		}
	}

	cmptContext.Finish(true);
}

void RadianceCascades::RenderDepthOnly(Camera& camera, DepthBuffer& targetDepth, D3D12_VIEWPORT viewPort, D3D12_RECT scissor, bool clearDepth)
{
	Renderer::MeshSorter meshSorter = Renderer::MeshSorter(Renderer::MeshSorter::kDefault);
	meshSorter.SetCamera(camera);
	meshSorter.SetViewport(viewPort);
	meshSorter.SetScissor(scissor);
	meshSorter.SetDepthStencilTarget(targetDepth);

	::AddModelsForRender(m_sceneModels, meshSorter);

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	if (clearDepth)
	{
		gfxContext.TransitionResource(targetDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(targetDepth);
	}

	GlobalConstants globals = {}; // Empty because depth pass fills its own global info.
	meshSorter.RenderMeshes(Renderer::MeshSorter::kZPass, gfxContext, globals);

	gfxContext.Finish(true);
}

void RadianceCascades::BuildMinMaxDepthBuffer(DepthBuffer& sourceDepthBuffer)
{
	ColorBuffer& minMaxDepthCopy = m_depthBufferCopy;
	ColorBuffer& minMaxMipMaps = m_minMaxDepthMips;

	ComputeContext& cmptContext = ComputeContext::Begin(L"Min Max Depth");

	{
		GPU_PROFILE_BLOCK("Min Max Depth Pass", cmptContext);

		const ComputePSO& cmptPSO = RuntimeResourceManager::GetComputePSO(PSOIDComputeMinMaxDepthPSO);
		cmptContext.SetPipelineState(cmptPSO);
		cmptContext.SetRootSignature(cmptPSO.GetRootSignature());

		// Copy the depth buffer to a color buffer that can be read from as a UAV.
		{
			cmptContext.TransitionResource(sourceDepthBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
			cmptContext.TransitionResource(minMaxDepthCopy, D3D12_RESOURCE_STATE_COPY_DEST);
			cmptContext.CopySubresource(minMaxDepthCopy, 0, sourceDepthBuffer, 0);
		}

		cmptContext.TransitionResource(minMaxMipMaps, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.TransitionResource(minMaxDepthCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.TransitionResource(m_minMaxDepthMips, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		// The first pass of min max depth uses the full resolution copy and writes to the first mip. 
		{
			SourceInfo depthSourceInfo = {};
			depthSourceInfo.isFirstDepth = true;
			depthSourceInfo.sourceWidth = minMaxDepthCopy.GetWidth();
			depthSourceInfo.sourceHeight = minMaxDepthCopy.GetHeight();

			cmptContext.SetDynamicConstantBufferView(RootEntryMinMaxDepthSourceInfo, sizeof(depthSourceInfo), &depthSourceInfo);
			cmptContext.SetDynamicDescriptors(RootEntryMinMaxDepthSourceDepthUAV, 0, 1, &minMaxDepthCopy.GetUAV());
			cmptContext.SetDynamicDescriptors(RootEntryMinMaxDepthTargetDepthUAV, 0, 1, &m_minMaxDepthMips.GetUAV());

			cmptContext.Dispatch2D(depthSourceInfo.sourceWidth >> 1, depthSourceInfo.sourceHeight >> 1);
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE* startUAV = &m_minMaxDepthMips.GetUAV();
		const uint32_t numMipMaps = minMaxMipMaps.GetNumMipMaps();
		for (uint32_t i = 0; i < numMipMaps; i++)
		{
			SourceInfo depthSourceInfo = {};
			depthSourceInfo.isFirstDepth = false;
			depthSourceInfo.sourceWidth = minMaxMipMaps.GetWidth() >> i;
			depthSourceInfo.sourceHeight = minMaxMipMaps.GetHeight() >> i;

			cmptContext.SetDynamicConstantBufferView(RootEntryMinMaxDepthSourceInfo, sizeof(depthSourceInfo), &depthSourceInfo);
			cmptContext.SetDynamicDescriptors(RootEntryMinMaxDepthSourceDepthUAV, 0, 1, startUAV + i);
			cmptContext.SetDynamicDescriptors(RootEntryMinMaxDepthTargetDepthUAV, 0, 1, startUAV + i + 1);

			// Must insert resource barrier between each dispatch as the output will otherwise be undefined.
			// TODO: Find why this is necessary when the same resource is being used? Each dispatch needs to be executed before the next can start, no?
			cmptContext.InsertUAVBarrier(m_minMaxDepthMips);
			cmptContext.Dispatch2D(depthSourceInfo.sourceWidth >> 1, depthSourceInfo.sourceHeight >> 1);
		}
	}

	cmptContext.Finish(true);
}

void RadianceCascades::RunRCCoalesce()
{
	RCGlobals rcGlobals = {};
	m_rcManager3D.FillRCGlobalInfo(rcGlobals);

	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Coalesce Compute");

	{
		GPU_PROFILE_BLOCK("RC Coalesce Pass", cmptContext);

		::SetComputePSOAndRootSig(cmptContext, PSOIDRC3DCoalescePSO);

		cmptContext.SetDynamicConstantBufferView(RootEntryRC3DCoalesceRCGlobalsCB, sizeof(RCGlobals), &rcGlobals);

		ColorBuffer& cascade0Buffer = m_rcManager3D.GetCascadeIntervalBuffer(0);
		ColorBuffer& coalesceBuffer = m_rcManager3D.GetCoalesceBuffer();

		cmptContext.TransitionResource(cascade0Buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmptContext.TransitionResource(coalesceBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmptContext.SetDynamicDescriptor(RootEntryRC3DCoalesceCascade0SRV, 0, cascade0Buffer.GetSRV());
		cmptContext.SetDynamicDescriptor(RootEntryRC3DCoalesceOutputTexUAV, 0, coalesceBuffer.GetUAV());

		cmptContext.Dispatch2D(coalesceBuffer.GetWidth(), coalesceBuffer.GetHeight());
	}

	cmptContext.Finish(true);
}

void RadianceCascades::RunComputeRCGatherFilterReduction()
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"Gather Filter Reduction");

	ByteAddressBuffer& gatherFilterByteAddresBuffer = m_rcManager3D.GetGatherFilterByteAddressBuffer();
	ReadbackBuffer& gatherFilterReadbackBuffer = m_rcManager3D.GetGatherFilterReadbackBuffer();

	{
		GPU_PROFILE_BLOCK("Gather Filter Reduction", cmptContext);

		cmptContext.SetPipelineState(m_gatherFilterReductionPSO);
		cmptContext.SetRootSignature(m_gatherFilterReductionRootSig);

		cmptContext.SetDynamicDescriptor(RootEntryGatherFilterReductionByteBuffer, 0, gatherFilterByteAddresBuffer.GetUAV());

		for (uint32_t i = 0; i < m_rcManager3D.GetGatherFilterCount(); i++)
		{
			ColorBuffer& gatherFilterTexture = m_rcManager3D.GetCascadeGatherFilterBuffer(i);

			cmptContext.TransitionResource(gatherFilterTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cmptContext.SetDynamicDescriptor(RootEntryGatherFilterReductionTexture, 0, gatherFilterTexture.GetSRV());

			// All data will be copied so ptr will not be invalid when executing the context at the end.
			FilterInfo filterInfo = {
				.width = gatherFilterTexture.GetWidth(),
				.height = gatherFilterTexture.GetHeight(),
				.filterIndex = i
			};

			cmptContext.SetDynamicConstantBufferView(RootEntryGatherFilterReductionInfo, sizeof(filterInfo), &filterInfo);

			cmptContext.TransitionResource(gatherFilterByteAddresBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			// Group size matches shader.
			// Each thread is responsible in a 4x4 area.
			cmptContext.Dispatch2D(filterInfo.width / 4u, filterInfo.height / 4u, 16u, 16u);
		}

		
		cmptContext.TransitionResource(gatherFilterByteAddresBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		cmptContext.TransitionResource(gatherFilterReadbackBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
		cmptContext.CopyBuffer(gatherFilterReadbackBuffer, gatherFilterByteAddresBuffer);
	}

	cmptContext.Finish(true);
}

void RadianceCascades::RunDeferredLightingPass(ColorBuffer& albedoBuffer, ColorBuffer& normalBuffer, ColorBuffer& diffuseRadianceBuffer, ColorBuffer& outputBuffer)
{
	// TODO: Have these as input parameters. Better yet, create an input struct because its getting very long.
	ColorBuffer& depthBuffer = m_depthBufferCopy;
	ColorBuffer& minMaxDepthBuffer = m_minMaxDepthMips;
	ColorBuffer& cascade0Buffer = m_rcManager3D.GetCascadeIntervalBuffer(0);

	GlobalInfo globalInfo = {};
	::FillGlobalInfo(globalInfo, m_camera, m_settings.globalSettings.useSkybox); // TODO: Make camera as input.

	RCGlobals rcGlobalInfo = {};
	m_rcManager3D.FillRCGlobalInfo(rcGlobalInfo);

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Diffuse Lighting Pass");

	{
		GPU_PROFILE_BLOCK("Diffuse Lighting", gfxContext);

		// Transition resources
		gfxContext.TransitionResource(albedoBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(normalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(diffuseRadianceBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(depthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(minMaxDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(cascade0Buffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(outputBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// Set up the pipeline state and root signature
		::SetGraphicsPSOAndRootSig(gfxContext, PSOIDDeferredLightingPSO);

		// Set the render target and viewport
		gfxContext.SetRenderTarget(outputBuffer.GetRTV());
		gfxContext.SetViewportAndScissor(m_mainViewport, m_mainScissor);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		// Set descriptors
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingAlbedoSRV, 0, albedoBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingNormalSRV, 0, normalBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingDiffuseRadianceSRV, 0, diffuseRadianceBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingCascade0MinMaxDepthSRV, 0, minMaxDepthBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingDepthBufferSRV, 0, depthBuffer.GetSRV());
		gfxContext.SetDynamicDescriptor(RootEntryDeferredLightingCascade0SRV, 0, cascade0Buffer.GetSRV());
		gfxContext.SetDynamicConstantBufferView(RootEntryDeferredLightingGlobalInfoCB, sizeof(GlobalInfo), &globalInfo);
		gfxContext.SetDynamicConstantBufferView(RootEntryDeferredLightingRCGlobalsCB, sizeof(RCGlobals), &rcGlobalInfo);

		// Draw a full-screen quad
		gfxContext.Draw(4, 0);
	}

	gfxContext.Finish(true);
}

void RadianceCascades::UpdateViewportAndScissor()
{
	float width = (float)Graphics::g_SceneColorBuffer.GetWidth();
	float height = (float)Graphics::g_SceneColorBuffer.GetHeight();

	m_mainViewport.Width = width;
	m_mainViewport.Height = height;
	m_mainViewport.MinDepth = 0.0f;
	m_mainViewport.MaxDepth = 1.0f;
	m_mainViewport.TopLeftX = 0.0f;
	m_mainViewport.TopLeftY = 0.0f;

	m_mainScissor.left = 0;
	m_mainScissor.top = 0;
	m_mainScissor.right = (LONG)width;
	m_mainScissor.bottom = (LONG)height;
}

void RadianceCascades::DrawSettingsUI()
{
	ImGui::Begin("Settings");

#pragma region Info
	if (ImGui::CollapsingHeader("App", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Swapchain Resolution: %u x %u", ::GetSceneColorWidth(), ::GetSceneColorHeight());
	}


#pragma endregion


#pragma region StandaloneSettings

	if (ImGui::CollapsingHeader("Standalone Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Camera FOV.
		{
			static float cameraFov = 0.0f;
			float widthOverHeight = 1.0f / m_camera.GetAspectRatio();
			cameraFov = Math::XMConvertToDegrees(Utils::VerticalFovToHorizontalFov(m_camera.GetFOV(), widthOverHeight));
			if (ImGui::SliderFloat("Camera FOV", &cameraFov, 0.0f, 180.0f))
			{
				m_camera.SetFOV(Utils::HorizontalFovToVerticalFov(Math::XMConvertToRadians(cameraFov), widthOverHeight));
				m_camera.Update();
			}
		}
	}

#pragma endregion

#pragma region GlobalSettings
	if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		GlobalSettings& gs = m_settings.globalSettings;

		ImGui::SeparatorText("Display Resolution");
		int* resolutionTarget = reinterpret_cast<int*>(&gs.resolutionTarget);
		int prevResolutionTarget = *resolutionTarget;
		ImGui::RadioButton("1080p", resolutionTarget, ResolutionTarget1080p); ImGui::SameLine();
		ImGui::RadioButton("1440p", resolutionTarget, ResolutionTarget1440p); ImGui::SameLine();
		ImGui::RadioButton("2160p", resolutionTarget, ResolutionTarget2160p);
		
		if (*resolutionTarget != prevResolutionTarget)
		{
			ResizeToResolutionTarget(ResolutionTarget(*resolutionTarget));
		}

		ImGui::SeparatorText("UI");
		ImGui::Checkbox("Draw UI (toggle with 'u')", &gs.renderUI);

		if (ImGui::Checkbox("Use larger font scale for UI", &gs.useLargerUIFontScale))
		{
			AppGUI::SetFontScale(gs.useLargerUIFontScale ? 1.5f : 1.0f);
		}

		ImGui::SeparatorText("Rendering Mode");
		int* renderMode = reinterpret_cast<int*>(&gs.renderMode);
		ImGui::RadioButton("Raster", renderMode, GlobalSettings::RenderModeRaster); ImGui::SameLine();
		ImGui::RadioButton("Raytracing", renderMode, GlobalSettings::RenderModeRT);

		ImGui::SeparatorText("Skybox");
		ImGui::Checkbox("Render Skybox", &gs.useSkybox);
		std::array<const char*, SkyboxIDCount> skyboxNames = {};
		for (uint32_t i = 0; i < skyboxNames.size(); i++)
		{
			skyboxNames[i] = m_skyboxIDToName[(SkyboxID)i];
		}
		ImGui::Combo("Skyboxes", reinterpret_cast<int*>(&m_currentSkybox), skyboxNames.data(), skyboxNames.size());


#if defined(_DEBUGDRAWING)
		ImGui::SeparatorText("Debug Drawing");
		ImGui::Checkbox("Use Debug Cam", &gs.useDebugCam);
		ImGui::Checkbox("Draw Debug Lines", &gs.renderDebugLines);
		if (gs.renderDebugLines)
		{
			ImGui::Checkbox("Use Depth For Debug Lines", &gs.useDepthCheckForDebugLines);
		}
#endif

		ImGui::SeparatorText("GPU Profiler");
		ImGui::Checkbox("Draw inactive profile graphs", &GPUProfiler::Get().profilerSettings.drawInactiveProfiles);
	}
#pragma endregion
	
#pragma region CascadeSettings
	// TODO: This feels a bit backwards. Almost all settings are taken from the RC manager itself. 
	// Maybe its better to just have a function in the rc manager that calls GUI functions?
	if (ImGui::CollapsingHeader("Radiance Cascade Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{

		RadianceCascadesSettings& rcs = m_settings.rcSettings;

		ImGui::Checkbox("Render RC 3D", &rcs.renderRC3D);

		if (rcs.renderRC3D)
		{
			bool shouldGenerateRCResources = false;

			ImGui::Separator();

			ImGui::Checkbox("Use Depth Aware Merging (WIP)", &m_rcManager3D.useDepthAwareMerging);
			ImGui::Checkbox("Use Gather Filtering", &m_rcManager3D.useGatherFiltering);

			bool prevPreAverageUsage = m_rcManager3D.isUsingPreAveragedIntervals;
			if (ImGui::Checkbox("Use Pre Average Intervals", &m_rcManager3D.isUsingPreAveragedIntervals))
			{
				if (prevPreAverageUsage != m_rcManager3D.isUsingPreAveragedIntervals)
				{
					shouldGenerateRCResources = true;
				}
			}

			ImGui::SliderInt("Cascade Result Index", &rcs.cascadeVisResultIndex, -1, m_rcManager3D.GetCascadeIntervalCount() - 1);

			if (ImGui::SliderFloat("Ray Length", &rcs.rayLength0, 0.1f, 250.0f))
			{
				m_rcManager3D.SetRayLength(rcs.rayLength0);
			}

			// Rays per probe settings
			{
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Rays per probe 0:");
				ImGui::SameLine();

				ImGui::RadioButton("4", &rcs.raysPerProbe0, 4); ImGui::SameLine();
				ImGui::RadioButton("16", &rcs.raysPerProbe0, 16); ImGui::SameLine();
				ImGui::RadioButton("36", &rcs.raysPerProbe0, 36); ImGui::SameLine();
				ImGui::RadioButton("64", &rcs.raysPerProbe0, 64);

				if (m_rcManager3D.GetRaysPerProbe(0) != rcs.raysPerProbe0)
				{
					shouldGenerateRCResources = true;
				}
			}

			// Probe spacing settings
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.2f);
				if (ImGui::InputInt("Probe Spacing [1 - 16]", &rcs.probeSpacing0, 1, 0))
				{
					rcs.probeSpacing0 = int(Math::Clamp(float(rcs.probeSpacing0), 1.0f, 16.0f));

					if (m_rcManager3D.GetProbeSpacing() != rcs.probeSpacing0)
					{
						shouldGenerateRCResources = true;
					}
				}
			}
			
			// Max cascade count settings
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.2f);
				int prevMaxCascadeCount = rcs.maxCascadeCount;
				if (ImGui::InputInt("Max Cascade Count [1 - 10]", &rcs.maxCascadeCount, 1, 0))
				{
					rcs.maxCascadeCount = int(Math::Clamp(float(rcs.maxCascadeCount), 1.0f, 10.0f));

					if (prevMaxCascadeCount != rcs.maxCascadeCount)
					{
						shouldGenerateRCResources = true;
					}
				}
			}
			

			if (shouldGenerateRCResources)
			{
				m_rcManager3D.Generate(
					rcs.raysPerProbe0, 
					rcs.probeSpacing0, 
					::GetSceneColorWidth(), 
					::GetSceneColorHeight(),
					rcs.maxCascadeCount
				);
			}

			const ColorBuffer& cascade0Buffer = m_rcManager3D.GetCascadeIntervalBuffer(0);
			const uint32_t cascadeResolutionWidth = cascade0Buffer.GetWidth();
			const uint32_t cascadeResolutionHeight = cascade0Buffer.GetHeight();

			ImGui::Text("Cascade Count: %u", m_rcManager3D.GetCascadeIntervalCount());
			ImGui::Text("Using pre-averaging: %s", m_rcManager3D.UsesPreAveragedIntervals() ? "Yes" : "No");
			uint64_t rcVRAMUsage = m_rcManager3D.GetTotalVRAMUsage();
			ImGui::Text("Vram usage: %.1f MB", rcVRAMUsage / (float)MemoryUnit::MegaByte);

			// Create a table with 6 columns: Cascade, Buffer Resolution, Probe Count, Ray Count, Start Dist, Length
			uint32_t cascadeTableHeaderCount = 6;
			// One extra on the end if gather filtering is used.
			if (m_rcManager3D.useGatherFiltering) { cascadeTableHeaderCount++; }

			if (ImGui::BeginTable("CascadeTable", cascadeTableHeaderCount, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoHostExtendX))
			{
				ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Buffer Resolution", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Probe Count", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Rays Per Probe", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Ray Start Distance", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Ray Length", ImGuiTableColumnFlags_WidthFixed);
				if(m_rcManager3D.useGatherFiltering) { ImGui::TableSetupColumn("Filtered Rays", ImGuiTableColumnFlags_WidthFixed); }
				ImGui::TableHeadersRow();

				// Add a row for each cascade
				for (unsigned int i = 0; i < m_rcManager3D.GetCascadeIntervalCount(); i++) {
					ImGui::TableNextRow();

					// Cascade column
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", i);

					// Cascade Resolution column
					const ColorBuffer& cascadeBuffer = m_rcManager3D.GetCascadeIntervalBuffer(i);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u x %u", cascadeBuffer.GetWidth(), cascadeBuffer.GetHeight());

					// Probe Count column
					//uint32_t cascadeCountPerDim = m_rcManager3D.GetProbeCountPerDim(i);
					ProbeDims cascadeCountPerDim = m_rcManager3D.GetProbeDims(i);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%u x %u", cascadeCountPerDim.probesX, cascadeCountPerDim.probesY);

					// Rays Per Probe column
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", m_rcManager3D.GetRaysPerProbe(i));

					// Ray Start Distance column
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.1f", m_rcManager3D.GetStartT(i));

					// Ray Length column
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%.1f", m_rcManager3D.GetRayLength(i));

					if (m_rcManager3D.useGatherFiltering)
					{
						ImGui::TableSetColumnIndex(6);
						if (i == 0)
						{
							ImGui::Text("n/a");
						}
						else
						{
							int gatherFilterIndex = i - 1;
							ColorBuffer& gatherFilterTexture = m_rcManager3D.GetCascadeGatherFilterBuffer(gatherFilterIndex);
							uint32_t gatherFilterReductionSum = m_rcManager3D.GetFilteredRayCount(gatherFilterIndex);
							
							uint32_t totalRays = gatherFilterTexture.GetWidth() * gatherFilterTexture.GetHeight();
							uint32_t filteredRayCount = totalRays - gatherFilterReductionSum;

							ImGui::Text("%u (%.2f%%)", filteredRayCount, 100.0f * filteredRayCount / totalRays);
						}
					}
				}

				ImGui::EndTable();
			}

			ImGui::SeparatorText("Radiance Cascade Visualizations");

			ImGui::Checkbox("See Coalesce Result", &rcs.seeCoalesceResult);

			ImGui::Checkbox("Visualize Probes", &rcs.enableCascadeProbeVis);
			if (rcs.enableCascadeProbeVis)
			{
				ImGui::SliderInt("Cascade Interval", &rcs.cascadeVisProbeIntervalIndex, 0, m_rcManager3D.GetCascadeIntervalCount() - 1);
				ImGui::SliderInt("Probe Subset", &rcs.cascadeVisProbeSubset, 1, 256);
			}

			ImGui::Text("Cascade Texture Visualization:");
			int* visEnumToIntPtr = reinterpret_cast<int*>(&rcs.currentTextureVis);
			ImGui::RadioButton("None", visEnumToIntPtr, RadianceCascadesSettings::CascadeTextureVisNone); ImGui::SameLine();
			ImGui::RadioButton("Gather", visEnumToIntPtr, RadianceCascadesSettings::CascadeTextureVisGather); ImGui::SameLine();
			ImGui::RadioButton("Merge", visEnumToIntPtr, RadianceCascadesSettings::CascadeTextureVisMerge); ImGui::SameLine();
			ImGui::RadioButton("Gather Filter", visEnumToIntPtr, RadianceCascadesSettings::CascadeTextureVisGatherFilter);

			if (rcs.currentTextureVis != RadianceCascadesSettings::CascadeTextureVisNone)
			{
				if (rcs.currentTextureVis == RadianceCascadesSettings::CascadeTextureVisGatherFilter)
				{
					static int correspondingCascadeIndexForFilter = 1;
					ImGui::SliderInt("Cascade Index", &correspondingCascadeIndexForFilter, 1, m_rcManager3D.GetCascadeIntervalCount() - 1);
					rcs.cascadeFilterIndex = correspondingCascadeIndexForFilter - 1;
				}
				else
				{
					ImGui::SliderInt("Cascade Index", &rcs.cascadeVisIndex, 0, m_rcManager3D.GetCascadeIntervalCount() - 1);
				}
			}
		}
	}
#pragma endregion


	
	ImGui::End();
}

void RadianceCascades::ClearBuffers()
{
	ColorBuffer& sceneColorBuffer = Graphics::g_SceneColorBuffer;
	ColorBuffer& sceneNormalBuffer = Graphics::g_SceneNormalBuffer;
	DepthBuffer& sceneDepthBuffer = Graphics::g_SceneDepthBuffer;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Clear Pixel Buffers");

	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(sceneDepthBuffer);

		gfxContext.TransitionResource(sceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(sceneColorBuffer);

		gfxContext.TransitionResource(sceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(sceneNormalBuffer);
	}

	{
		gfxContext.TransitionResource(m_minMaxDepthMips, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(m_minMaxDepthMips);
	}

	{
		gfxContext.TransitionResource(m_debugCamDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(m_debugCamDepthBuffer);
	}

	{
		m_rcManager3D.ClearBuffers(gfxContext);
	}

	gfxContext.Finish(true);
}

void RadianceCascades::FullScreenCopyCompute(PixelBuffer& source, D3D12_CPU_DESCRIPTOR_HANDLE sourceSRV, ColorBuffer& dest)
{
	uint32_t destWidth = dest.GetWidth();
	uint32_t destHeight = dest.GetHeight();

	ComputeContext& cmptContext = ComputeContext::Begin(L"Full Screen Copy Compute");

	cmptContext.SetPipelineState(m_fullScreenCopyComputePSO);
	cmptContext.SetRootSignature(m_fullScreenCopyComputeRootSig);

	cmptContext.TransitionResource(dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.InsertUAVBarrier(dest);
	cmptContext.TransitionResource(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cmptContext.SetConstants(RootEntryFullScreenCopyComputeDestInfo, destWidth, destHeight);
	cmptContext.SetDynamicDescriptor(RootEntryFullScreenCopyComputeDest, 0, dest.GetUAV());
	cmptContext.SetDynamicDescriptor(RootEntryFullScreenCopyComputeSource, 0, sourceSRV);

	cmptContext.Dispatch2D(destWidth, destHeight);
	cmptContext.Finish(true);
}

void RadianceCascades::FullScreenCopyCompute(ColorBuffer& source, ColorBuffer& dest)
{
	FullScreenCopyCompute(source, source.GetSRV(), dest);
}

void RadianceCascades::EquilateralToCubemapCompute(TextureRef equilateralTexture, ColorBuffer cubemapTexture)
{
	// NOT IMPLEMENTED
}

InternalModelInstance* RadianceCascades::AddModelInstance(ModelID modelID)
{
	ASSERT(m_sceneModels.size() < MAX_INSTANCES);
	std::shared_ptr<Model> modelPtr = RuntimeResourceManager::GetModelPtr(modelID);

	if (modelPtr == nullptr)
	{
		modelPtr = Renderer::LoadModel(s_BackupModelPath, false);
		LOG_ERROR(L"Model was invalid. Using backup model instead. If Sponza model is missing, download a Sponza PBR gltf model online.");
	}

	return &m_sceneModels.emplace_back(modelPtr, modelID);
}

void RadianceCascades::AddSceneModel(ModelID modelID, const ModelInstanceDesc& modelInstanceDesc)
{
	InternalModelInstance* modelInstance = AddModelInstance(modelID);
	
	Math::UniformTransform& instanceTransform = modelInstance->GetTransform();
	instanceTransform.SetScale(modelInstanceDesc.scale);
	instanceTransform.SetRotation(modelInstanceDesc.rotation);
	instanceTransform.SetTranslation(modelInstanceDesc.position - modelInstance->GetBoundingBox().GetCenter());

	modelInstance->updateScript = modelInstanceDesc.updateScript;
}

std::unordered_map<ModelID, std::vector<Utils::GPUMatrix>> RadianceCascades::GetGroupedModelInstances()
{
	std::unordered_map<ModelID, std::vector<Utils::GPUMatrix>> groupedModelInsances = {};

	for (InternalModelInstance& modelInstance : m_sceneModels)
	{
		ModelID modelID = modelInstance.underlyingModelID;
		Math::Matrix4 mat = Math::Matrix4(modelInstance.GetTransform());
		groupedModelInsances[modelID].push_back(mat);
	}

	return groupedModelInsances;
}

std::vector<TLASInstanceGroup> RadianceCascades::GetTLASInstanceGroups()
{
	std::unordered_map<ModelID, std::vector<Utils::GPUMatrix>> groupedModelInstances = GetGroupedModelInstances();

	std::vector<TLASInstanceGroup> instanceGroups = {};
	for (auto& [modelID, transforms] : groupedModelInstances)
	{
		TLASInstanceGroup instanceGroup = {};
		instanceGroup.blasBuffer = &RuntimeResourceManager::GetModelBLAS(modelID);
		instanceGroup.instanceTransforms = transforms;

		instanceGroups.push_back(instanceGroup);
	}

	return instanceGroups;
}

void RadianceCascades::RegisterDisplayDependentTexture(PixelBuffer* pixelBuffer, TextureType textureType)
{
	m_displayDependentTextures.emplace_back(textureType, pixelBuffer);
}
