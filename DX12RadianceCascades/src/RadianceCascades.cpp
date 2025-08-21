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

using namespace Microsoft::WRL;

// Mimicing how Microsoft exposes their global variables (example in Display.cpp)
namespace GameCore { extern HWND g_hWnd; }

constexpr size_t MAX_INSTANCES = 256;
static const DXGI_FORMAT s_FlatlandSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const std::wstring s_BackupModelPath = L"models\\Testing\\SphereTest.gltf";

#define SAMPLE_LEN_0 20.0f
#define RAYS_PER_PROBE_0 4.0f
#define CAM_FOV 90.0f

enum TestSuite
{
	// Test suite indices.
	TestSuiteRaysPerProbe0 = 0,
	TestSuiteProbeSpacing0,
	TestSuiteMaxAllowedCascadeLevels,

	// Total number of test suites.
	TestSuiteCount
};

struct TestSuiteData
{
	std::array<uint32_t, TestSuiteCount> testIndices;

	std::vector<std::pair<const char*, float>> averageFrametimes; // Pair of profile name and average time in ms.
	uint64_t totalVRAMSize = 0; // Total VRAM size of resources relevant to the test suite.
};

struct TestSetup
{
	uint32_t framesBetweenTests;

	std::vector<uint32_t> raysPerProbe0Vals;
	std::vector<uint32_t> probeSpacing0Vals;
	std::vector<uint32_t> maxAllowedCascadeLevelsVals;

	std::vector<TestSuiteData> testSuites;

	// Updates every frame and resets when frames between tests is reached.
	uint32_t currentFrameCount = 0;

	// Increments every frame and resets when frames between tests is reached.
	uint32_t currentTestSuiteIndex = 0;

	bool needMoreFrames = true;

	void WriteTestSuiteToCSVFile()
	{
		std::wstring fileName = std::format(L"RadianceCascadesTestResult_{}x{}", Graphics::g_DisplayWidth, Graphics::g_DisplayHeight);
		std::wofstream fileStream(fileName);
		if (!fileStream.is_open())
		{
			throw std::runtime_error("Failed to open file for writing test suite data.");
		}

		LOG_INFO(L"Writing test suite data to file: {}", fileName);

		// Headers
		{
			// Add names for each column in the CSV file.
			fileStream << L"RaysPerProbe,ProbeSpacing,MaxCascadeLevels";

			// Add names of each average frame time as headers.
			for (const auto& averageFrameTime : testSuites[0].averageFrametimes)
			{
				fileStream << L"," << averageFrameTime.first;
			}

			// Add VRAM size column header.
			fileStream << L",VRAM";

			fileStream << L"\n";
		}

		// Data
		for(size_t i = 0; i < testSuites.size(); i++)
		{
			TestSuiteData& testSuite = testSuites[i];

			uint32_t raysPerProbeValueIndex = testSuite.testIndices[TestSuiteRaysPerProbe0];
			uint32_t probeSpacingValueIndex = testSuite.testIndices[TestSuiteProbeSpacing0];
			uint32_t cascadeLevelValueIndex = testSuite.testIndices[TestSuiteMaxAllowedCascadeLevels];

			fileStream
				<< raysPerProbe0Vals[raysPerProbeValueIndex] << L","
				<< probeSpacing0Vals[probeSpacingValueIndex] << L","
				<< maxAllowedCascadeLevelsVals[cascadeLevelValueIndex] << L",";

			for (const auto& averageFrameTime : testSuite.averageFrametimes)
			{
				fileStream << averageFrameTime.second << L",";
			}

			fileStream << testSuite.totalVRAMSize;

			fileStream << L"\n";
		}

		fileStream.close();
	}
};

static TestSetup sTestSetup;


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

	void FillGlobalInfo(GlobalInfo& globalInfo, const Camera& camera)
	{
		globalInfo.viewProjMatrix = camera.GetViewProjMatrix();

		globalInfo.invViewProjMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetViewProjMatrix()));
		globalInfo.invProjMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetProjMatrix()));
		globalInfo.invViewMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, camera.GetViewMatrix()));

		globalInfo.cameraPos = camera.GetPosition();
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
		float yPos = transform.GetTranslation().GetY();
		Vector3 centerPoint = Vector3(0.0f, yPos, 0.0f);
		float amplitude = 1000.0f;      
		float frequency = 1.0f;

		Vector3 position = centerPoint;
		position += Vector3(amplitude * sin(frequency * time + yPos), 0.0f, 0.0f);

		transform.SetTranslation(position);
	}

	void InitTestSetup()
	{
		// Make sure that the frames between tests is greater than the maximum number of frames that can be sampled.
		// This is to ensure that every test suite has enough frames to collect the average frame time data.
		sTestSetup.framesBetweenTests = MaxFrametimeSampleCount + 10;

#if (TEST_TO_RUN == 0)
		// Full test setup
		sTestSetup.maxAllowedCascadeLevelsVals = { 5, 6, 7, 8 };
		sTestSetup.probeSpacing0Vals = { 1, 2, 3, 4 };
		sTestSetup.raysPerProbe0Vals = { 16, 64 };
#elif (TEST_TO_RUN == 1)
		// Half test setup
		sTestSetup.maxAllowedCascadeLevelsVals = { 5, 6, 7 };
		sTestSetup.probeSpacing0Vals = { 1, 2, 3 };
		sTestSetup.raysPerProbe0Vals = { 16 };
#elif (TEST_TO_RUN == 2)
		// Simple test setup
		sTestSetup.maxAllowedCascadeLevelsVals = { 5 };
		sTestSetup.probeSpacing0Vals = { 2 };
		sTestSetup.raysPerProbe0Vals = { 16 };
#endif 

		

		for (size_t clI = 0; clI < sTestSetup.maxAllowedCascadeLevelsVals.size(); clI++)
		{
			for(size_t psI = 0; psI < sTestSetup.probeSpacing0Vals.size(); psI++)
			{
				for (size_t rppI = 0; rppI < sTestSetup.raysPerProbe0Vals.size(); rppI++)
				{
					TestSuiteData testSuite;
					testSuite.testIndices[TestSuiteRaysPerProbe0] = uint32_t(rppI);
					testSuite.testIndices[TestSuiteProbeSpacing0] = uint32_t(psI);
					testSuite.testIndices[TestSuiteMaxAllowedCascadeLevels] = uint32_t(clI);

					sTestSetup.testSuites.push_back(testSuite);
				}
			}
		}
	}

	void EnableDriverBackgroundOptimizations()
	{
		ComPtr<ID3D12Device6> device6;
		ThrowIfFailedHR(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&device6)));

		// This will force the GPU driver to collect measurements in any way it wants to collect the data necessary for dynamic compilations.
		// This will be queried each frame until it says it has had enough frames and will stop collecting measurements.
		// Then these optimizations will be applied and disabled for the rest of the testing session.
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
}

RadianceCascades::RadianceCascades()
	: m_mainViewport({}), 
	m_mainScissor({}), 
	m_rcManager3D(
		m_settings.rcSettings.rayLength0, 
		true, 
		m_settings.rcSettings.useDepthAwareMerging
	)
{
	m_sceneModels.reserve(MAX_INSTANCES);

#if defined(RUN_TESTS)
	::InitTestSetup();
#endif
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

			m_albedoBuffer.Create(L"Albedo Buffer", ::GetSceneColorWidth(), ::GetSceneColorHeight(), 1, ::GetSceneColorFormat());

			DepthBuffer& sceneDepthBuff = Graphics::g_SceneDepthBuffer;
			m_debugCamDepthBuffer.Create(L"Debug Cam Depth Buffer", sceneDepthBuff.GetWidth(), sceneDepthBuff.GetHeight(), sceneDepthBuff.GetFormat());
		}
	}
	
	UpdateViewportAndScissor();

#if defined(RUN_TESTS)
	::EnableStablePowerState();
	::EnableDriverBackgroundOptimizations();
#endif
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

#if defined(RUN_TESTS)
	if(sTestSetup.currentFrameCount >= sTestSetup.framesBetweenTests || sTestSetup.currentTestSuiteIndex == 0)
	{
		if (sTestSetup.currentTestSuiteIndex > 0)
		{
			TestSuiteData& previousTestSuite = sTestSetup.testSuites[sTestSetup.currentTestSuiteIndex - 1];
			// Calculate and write frametime info for the previous test suite.
			auto& profiles = GPUProfiler::Get().GetProfiles();
			for (const auto& profile : profiles)
			{
				if (profile.name == nullptr)
				{
					continue; // Skip null profiles.
				}

				float frametimeSum = 0.0f;
				for (float frameTime : profile.timeSamples)
				{
					frametimeSum += frameTime;
				}

				const float frametimeAverage = frametimeSum / profile.timeSamples.size();
				previousTestSuite.averageFrametimes.push_back({ profile.name, frametimeAverage });
			}

			// Save VRAM usage before generating new RC resources.
			previousTestSuite.totalVRAMSize = m_rcManager3D.GetTotalVRAMUsage();
		}

		if (sTestSetup.currentTestSuiteIndex < sTestSetup.testSuites.size())
		{
			TestSuiteData& currentTestSuite = sTestSetup.testSuites[sTestSetup.currentTestSuiteIndex];
			const auto& testIndices = currentTestSuite.testIndices;
			
			uint32_t raysPerProbe0 = sTestSetup.raysPerProbe0Vals[testIndices[TestSuiteRaysPerProbe0]];
			uint32_t probeSpacing0 = sTestSetup.probeSpacing0Vals[testIndices[TestSuiteProbeSpacing0]];
			uint32_t maxAllowedCascadeLevels = sTestSetup.maxAllowedCascadeLevelsVals[testIndices[TestSuiteMaxAllowedCascadeLevels]];

			m_rcManager3D.Generate(
				raysPerProbe0,
				probeSpacing0,
				::GetSceneColorWidth(),
				::GetSceneColorHeight(),
				maxAllowedCascadeLevels
			);

			// Update settings
			m_settings.rcSettings.raysPerProbe0 = raysPerProbe0;
			m_settings.rcSettings.probeSpacing0 = probeSpacing0;

			LOG_INFO(
				L"Running test suite {}/{} ({}%): RaysPerProbe0 = {}, ProbeSpacing0 = {}, MaxAllowedCascadeLevels = {}",
				sTestSetup.currentTestSuiteIndex + 1,
				sTestSetup.testSuites.size(),
				(sTestSetup.currentTestSuiteIndex + 1) * 100 / sTestSetup.testSuites.size(),
				raysPerProbe0,
				probeSpacing0,
				maxAllowedCascadeLevels
			);

			sTestSetup.currentTestSuiteIndex++;
			sTestSetup.currentFrameCount = 0;

			// Assume that change of settings requires more frames to be rendered for the optimizations to take effect.
			sTestSetup.needMoreFrames = TRUE;
			::EnableDriverBackgroundOptimizations();
		}
		else
		{
			sTestSetup.WriteTestSuiteToCSVFile();

			// All tests are done, quit the application.
			m_shouldQuit = true;
		}
	}
	else
	{
		// If the test setup is not waiting for further driver optimizations then the frame counting can begin.
		if (!sTestSetup.needMoreFrames)
		{
			sTestSetup.currentFrameCount++;
			LOG_DEBUG(L"Current frame count: {} ({}%)", sTestSetup.currentFrameCount, 100 * sTestSetup.currentFrameCount / sTestSetup.framesBetweenTests);
		}
		else
		{
			LOG_DEBUG(L"Waiting for GPU driver optimizations to finish before continuing with the tests.");
		}
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
	ClearPixelBuffers();

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

			if (m_settings.rcSettings.visualizeRC3DGatherCascades)
			{
				FullScreenCopyCompute(m_rcManager3D.GetCascadeIntervalBuffer(m_settings.rcSettings.cascadeVisIndex), Graphics::g_SceneColorBuffer);
			}
			else
			{
				RunRCMerge(renderCamera, m_minMaxDepthMips);

				if (m_settings.rcSettings.visualizeRC3DMergeCascades)
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
	
	if (sTestSetup.needMoreFrames)
	{
		sTestSetup.needMoreFrames = NeedsMoreFramesForOptimization();

		if (sTestSetup.needMoreFrames == false)
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

			LOG_INFO(L"GPU driver optimizations have been applied and disabled for the rest of the testing suite.");
		}
	}
#endif
}

void RadianceCascades::RenderUI(GraphicsContext& uiContext)
{

#if defined(RUN_TESTS)
	return; // Skip UI rendering when test running is enabled.
#endif

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

void RadianceCascades::InitializeScene()
{
	GPU_MEMORY_BLOCK("Scene");

	int sceneIndex = 3;

	if (sceneIndex == 0) // Default scene
	{
		{
			AddSceneModel(ModelIDSponza, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });
		}

		{
			Math::Vector3 modelCenter = GetMainSceneModelCenter();
			
			for (int i = 0; i < 5; i++)
			{
				float yPos = (100.0f * i) - 500.0f;
				AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(0.0f, yPos, 0.0f) + modelCenter, {}, ::BScriptPosOscillation });
			}
			
			//AddSceneModel(ModelIDSphereTest, { 50.0f, Vector3(200.0f, -300.0f, 500.0f) + modelCenter });
			//AddSceneModel(ModelIDSphereTest, { 30.0f, Vector3(200.0f, -300.0f, -500.0f) + modelCenter });
			//
			//AddSceneModel(ModelIDLantern, { 25.0f, Vector3(1100.0f, -500.0f, 0.0f) + modelCenter, Math::Quaternion(0.0f, Math::XM_PI, 0.0f)});
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
	else if (sceneIndex == 3)
	{
		AddSceneModel(ModelIDSponza, { 100.0f, Vector3(0.0f, 0.0f, 0.0f) });

		Math::Vector3 modelCenter = GetMainSceneModelCenter();
		float yPos = -100.0f;
		AddSceneModel(ModelIDSphereTest, { 130.0f, Vector3(0.0f, yPos, 0.0f) + modelCenter, {}, ::BScriptPosOscillation });
	}

	// Setup camera
	{
		float heightOverWidth = (float)::GetSceneColorHeight() / (float)::GetSceneColorWidth();
		m_camera.SetAspectRatio(heightOverWidth);
		m_camera.SetFOV(Utils::HorizontalFovToVerticalFov(Math::XMConvertToRadians(CAM_FOV), 1.0f / heightOverWidth));

		
		Vector3 modelCenter = GetMainSceneModelCenter();
		m_camera.SetEyeAtUp(modelCenter + Vector3(500.0f, -80.0f, -150.0f), modelCenter, Vector3(kYUnitVector));
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
		RuntimeResourceManager::RegisterPSO(PSOIDComputeFullScreenCopyPSO,	&m_fullScreenCopyComputePSO,	PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeRCMergePSO,			&m_rcMergePSO,					PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeRCRadianceFieldPSO, &m_rcRadianceFieldPSO,			PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRaytracingTestPSO,			&m_rtTestPSO,					PSOTypeRaytracing);
		RuntimeResourceManager::RegisterPSO(PSOIDComputeMinMaxDepthPSO,		&m_minMaxDepthPSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRCRaytracingPSO,			&m_rcRaytracePSO,				PSOTypeRaytracing);
		RuntimeResourceManager::RegisterPSO(PSOIDRC3DMergePSO,				&m_rc3dMergePSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDRC3DCoalescePSO,			&m_rc3dCoalescePSO,				PSOTypeCompute);
		RuntimeResourceManager::RegisterPSO(PSOIDDeferredLightingPSO,		&m_deferredLightingPSO,			PSOTypeGraphics);
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
		rootSig[RootEntryDeferredLightingGlobalInfoCB].InitAsConstantBuffer(0);
		rootSig[RootEntryDeferredLightingRCGlobalsCB].InitAsConstantBuffer(1);
		{
			SamplerDesc samplerState = Graphics::SamplerLinearClampDesc;
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
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeRCMergePSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeRCMergePSO, ShaderIDRCMergeCS);

		RootSignature& rootSig = m_rcMergeRootSig;
		rootSig.Reset(RootEntryRCMergeCount, 1);
		rootSig[RootEntryRCMergeCascadeNUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRCMergeCascadeN1SRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryRCMergeGlobals].InitAsConstantBuffer(0);
		rootSig[RootEntryRCMergeCascadeInfo].InitAsConstantBuffer(1);

		{
			SamplerDesc sampler = Graphics::SamplerPointClampDesc;
			rootSig.InitStaticSampler(0, sampler);
		}

		rootSig.Finalize(L"Compute RC Merge");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = RuntimeResourceManager::GetComputePSO(PSOIDComputeRCRadianceFieldPSO);
		RuntimeResourceManager::SetShaderForPSO(PSOIDComputeRCRadianceFieldPSO, ShaderIDRCRadianceFieldCS);

		RootSignature& rootSig = m_rcRadianceFieldRootSig;
		rootSig.Reset(RootEntryRCRadianceFieldCount, 1);
		rootSig[RootEntryRCRadianceFieldGlobals].InitAsConstantBuffer(0);
		rootSig[RootEntryRCRadianceFieldCascadeInfo].InitAsConstantBuffer(1);
		rootSig[RootEntryRCRadianceFieldUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRCRadianceFieldCascadeSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryRCRadianceFieldInfo].InitAsConstants(2, 2);
		{
			SamplerDesc sampler = Graphics::SamplerPointBorderDesc;
			rootSig.InitStaticSampler(0, sampler);
		}
		rootSig.Finalize(L"Compute RC Radiance Field");

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
		globalRootSig[RootEntryRCRaytracingRTGOutputUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		globalRootSig[RootEntryRCRaytracingRTGGlobalInfoCB].InitAsConstantBufferView(0);
		globalRootSig[RootEntryRCRaytracingRTGRCGlobalsCB].InitAsConstantBufferView(1);
		globalRootSig[RootEntryRCRaytracingRTGCascadeInfoCB].InitAsConstantBufferView(2);
#if defined(_DEBUG)
		globalRootSig[RootEntryRCRaytracingRTGRCVisCB].InitAsConstantBufferView(127);
#endif
		globalRootSig[RootEntryRCRaytracingRTGDepthTextureUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
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

	// RC 2D
	{
		GPU_MEMORY_BLOCK("RC 2D");
		
		m_flatlandScene.Create(L"Flatland Scene", ::GetSceneColorWidth(), ::GetSceneColorHeight(), 1, s_FlatlandSceneFormat);

		float diag = Math::Length({ (float)::GetSceneColorWidth(), (float)::GetSceneColorHeight(), 0.0f });
		m_rcManager2D.Init(SAMPLE_LEN_0, RAYS_PER_PROBE_0, diag);
	}
	
	// RC 3D
	{
		GPU_MEMORY_BLOCK("RC 3D");

		DepthBuffer& sceneDepthBuff = Graphics::g_SceneDepthBuffer;
		m_depthBufferCopy.Create(L"Depth Copy", sceneDepthBuff.GetWidth(), sceneDepthBuff.GetHeight(), 1, DXGI_FORMAT_R32_FLOAT);
		m_minMaxDepthMips.Create(L"Min Max Depth Mips", sceneDepthBuff.GetWidth() / 2, sceneDepthBuff.GetHeight() / 2, 0, DXGI_FORMAT_R32G32_FLOAT);

		m_rcManager3D.Generate(
			m_settings.rcSettings.raysPerProbe0, 
			m_settings.rcSettings.probeSpacing0, 
			::GetSceneColorWidth(), 
			::GetSceneColorHeight()
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
	::FillGlobalInfo(rtGlobalInfo, camera);

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
	::FillGlobalInfo(globalInfo, camera);

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

		for (uint32_t cascadeIndex = 0; cascadeIndex < m_rcManager3D.GetCascadeIntervalCount(); cascadeIndex++)
		{
			CascadeInfo cascadeInfo = {};
			cascadeInfo.cascadeIndex = cascadeIndex;

			rtContext.SetDynamicConstantBufferView(RootEntryRCRaytracingRTGCascadeInfoCB, sizeof(CascadeInfo), &cascadeInfo);

			ColorBuffer& cascadeBuffer = m_rcManager3D.GetCascadeIntervalBuffer(cascadeIndex);
			rtContext.TransitionResource(cascadeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			const DescriptorHandle& rcBufferUAV = RuntimeResourceManager::GetDescCopy(cascadeBuffer.GetUAV());
			rtCommandList->SetComputeRootDescriptorTable(RootEntryRCRaytracingRTGOutputUAV, rcBufferUAV);

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
		::FillGlobalInfo(globalInfo, cam);

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

void RadianceCascades::RunComputeFlatlandScene()
{
	ColorBuffer& targetScene = m_flatlandScene;
	uint32_t sceneWidth = targetScene.GetWidth();
	uint32_t sceneHeight = targetScene.GetHeight();

	ComputeContext& cmptContext = ComputeContext::Begin(L"Flatland Scene");

	cmptContext.SetPipelineState(m_flatlandScenePSO);
	cmptContext.SetRootSignature(m_computeFlatlandSceneRootSig);

	cmptContext.TransitionResource(targetScene, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.SetDynamicDescriptor(RootEntryFlatlandSceneUAV, 0, targetScene.GetUAV());
	cmptContext.SetConstants(RootEntryFlatlandSceneInfo, sceneWidth, sceneHeight);
	
	cmptContext.Dispatch2D(sceneWidth, sceneHeight);
	cmptContext.Finish(true);
}

void RadianceCascades::RunComputeRCGather()
{
	ColorBuffer& sceneBuffer = m_flatlandScene;
	RC2DGlobals rcGlobals = m_rcManager2D.FillRCGlobalsData(sceneBuffer.GetWidth());

	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Gather Compute");

	cmptContext.SetRootSignature(m_computeGatherRootSig);
	cmptContext.SetPipelineState(m_rcGatherPSO);

	cmptContext.SetDynamicConstantBufferView(RootEntryRCGatherGlobals, sizeof(rcGlobals), &rcGlobals);

	cmptContext.TransitionResource(sceneBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cmptContext.SetDynamicDescriptor(RootEntryRCGatherSceneSRV, 0, sceneBuffer.GetSRV());

	for (uint32_t i = 0; i < m_rcManager2D.GetCascadeCount(); i++)
	{
		ColorBuffer& target = m_rcManager2D.GetCascadeInterval(i);

		CascadeInfo cascadeInfo = {};
		cascadeInfo.cascadeIndex = i;
		cmptContext.SetDynamicConstantBufferView(RootEntryRCGatherCascadeInfo, sizeof(cascadeInfo), &cascadeInfo);

		cmptContext.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.SetDynamicDescriptor(RootEntryRCGatherCascadeUAV, 0, target.GetUAV());

		cmptContext.Dispatch2D(target.GetWidth(), target.GetHeight(), 16, 16);
	}

	cmptContext.Finish(true);
}

void RadianceCascades::RunComputeRCMerge()
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Merge Compute");

	cmptContext.SetRootSignature(m_rcMergeRootSig);
	cmptContext.SetPipelineState(m_rcMergePSO);

	RC2DGlobals rcGlobals = {};
	{
		ColorBuffer& cascade0 = m_rcManager2D.GetCascadeInterval(0);
		rcGlobals = m_rcManager2D.FillRCGlobalsData(cascade0.GetWidth());
	}
	 
	cmptContext.SetDynamicConstantBufferView(RootEntryRCMergeGlobals, sizeof(rcGlobals), &rcGlobals);

	// Start loop at second last cascade and go down to the first cascade.
	for (int i = m_rcManager2D.GetCascadeCount() - 2; i >= 0; i--)
	{
		ColorBuffer& target = m_rcManager2D.GetCascadeInterval(i);
		ColorBuffer& source = m_rcManager2D.GetCascadeInterval(i + 1);
		CascadeInfo cascadeInfo = {};
		cascadeInfo.cascadeIndex = i;
		cmptContext.SetDynamicConstantBufferView(RootEntryRCMergeCascadeInfo, sizeof(cascadeInfo), &cascadeInfo);

		cmptContext.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.TransitionResource(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		cmptContext.SetDynamicDescriptor(RootEntryRCMergeCascadeNUAV, 0, target.GetUAV());
		cmptContext.SetDynamicDescriptor(RootEntryRCMergeCascadeN1SRV, 0, source.GetSRV());

		cmptContext.Dispatch2D(target.GetWidth(), target.GetHeight(), 16, 16);
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

void RadianceCascades::RunComputeRCRadianceField(ColorBuffer& outputBuffer)
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Radiance Field Compute");
	cmptContext.SetRootSignature(m_rcRadianceFieldRootSig);
	cmptContext.SetPipelineState(m_rcRadianceFieldPSO);

	ColorBuffer& radianceField = m_rcManager2D.GetRadianceField();
	ColorBuffer& targetCascade = m_rcManager2D.GetCascadeInterval(0);

	RC2DGlobals rcGlobals = m_rcManager2D.FillRCGlobalsData(targetCascade.GetWidth());
	cmptContext.SetDynamicConstantBufferView(RootEntryRCRadianceFieldGlobals, sizeof(rcGlobals), &rcGlobals);

	{
		CascadeInfo cascadeInfo = {};
		cascadeInfo.cascadeIndex = 0;
		cmptContext.SetDynamicConstantBufferView(RootEntryRCRadianceFieldCascadeInfo, sizeof(cascadeInfo), &cascadeInfo);

		cmptContext.SetConstants(RootEntryRCRadianceFieldInfo, radianceField.GetWidth(), radianceField.GetHeight());

		cmptContext.TransitionResource(radianceField, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.TransitionResource(targetCascade, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		cmptContext.SetDynamicDescriptor(RootEntryRCRadianceFieldUAV, 0, radianceField.GetUAV());
		cmptContext.SetDynamicDescriptor(RootEntryRCRadianceFieldCascadeSRV, 0, targetCascade.GetSRV());
		cmptContext.Dispatch2D(radianceField.GetWidth(), radianceField.GetHeight());
	}

	cmptContext.Finish(true);

	// Copy over the result to the output buffer.
	FullScreenCopyCompute(radianceField, outputBuffer);
}

void RadianceCascades::RunDeferredLightingPass(ColorBuffer& albedoBuffer, ColorBuffer& normalBuffer, ColorBuffer& diffuseRadianceBuffer, ColorBuffer& outputBuffer)
{
	// TODO: Have these as input parameters. Better yet, create an input struct because its getting very long.
	ColorBuffer& depthBuffer = m_depthBufferCopy;
	ColorBuffer& minMaxDepthBuffer = m_minMaxDepthMips;

	GlobalInfo globalInfo = {};
	::FillGlobalInfo(globalInfo, m_camera); // TODO: Make camera as input.

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
	if (ImGui::CollapsingHeader("App Info", ImGuiTreeNodeFlags_DefaultOpen))
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

		ImGui::SeparatorText("Rendering Mode");
		int* renderMode = reinterpret_cast<int*>(&gs.renderMode);
		ImGui::RadioButton("Raster", renderMode, GlobalSettings::RenderModeRaster); ImGui::SameLine();
		ImGui::RadioButton("Raytracing", renderMode, GlobalSettings::RenderModeRT);

#if defined(_DEBUGDRAWING)
		ImGui::SeparatorText("Debug Drawing");
		ImGui::Checkbox("Use Debug Cam", &gs.useDebugCam);
		ImGui::Checkbox("Draw Debug Lines", &gs.renderDebugLines);
		if (gs.renderDebugLines)
		{
			ImGui::Checkbox("Use Depth For Debug Lines", &gs.useDepthCheckForDebugLines);
		}
#endif
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
			ImGui::Separator();

			if (ImGui::Checkbox("Use Depth Aware Merging", &rcs.useDepthAwareMerging))
			{
				m_rcManager3D.SetDepthAwareMerging(rcs.useDepthAwareMerging);
			}

			if (ImGui::SliderFloat("Ray Length", &rcs.rayLength0, 0.1f, 150.0f))
			{
				m_rcManager3D.SetRayLength(rcs.rayLength0);
			}

			if (ImGui::InputInt("Probe Spacing", &rcs.probeSpacing0, 1, 0))
			{
				rcs.probeSpacing0 = int(Math::Clamp(float(rcs.probeSpacing0), 1.0f, 16.0f));

				if (m_rcManager3D.GetProbeSpacing() != rcs.probeSpacing0)
				{
					m_rcManager3D.SetProbeSpacing(rcs.probeSpacing0);
					m_rcManager3D.Generate(rcs.raysPerProbe0, rcs.probeSpacing0, ::GetSceneColorWidth(), ::GetSceneColorHeight());
				}
			}

			const ColorBuffer& cascade0Buffer = m_rcManager3D.GetCascadeIntervalBuffer(0);
			const uint32_t cascadeResolutionWidth = cascade0Buffer.GetWidth();
			const uint32_t cascadeResolutionHeight = cascade0Buffer.GetHeight();

			ImGui::Text("Cascade Count: %u", m_rcManager3D.GetCascadeIntervalCount());
			ImGui::Text("Using pre-averaging: %s", m_rcManager3D.UsesPreAveragedIntervals() ? "Yes" : "No");

			// Create a table with 5 columns: Cascade, Probe Count, Ray Count, Start Dist, Length
			if (ImGui::BeginTable("CascadeTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoHostExtendX))
			{
				ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Buffer Resolution", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Probe Count", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Rays Per Probe", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Ray Start Distance", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Ray Length", ImGuiTableColumnFlags_WidthFixed);
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
				}

				ImGui::EndTable();
			}

			ImGui::SeparatorText("Radiance Cascade Visualizations");
			ImGui::Checkbox("See Coalesce Result", &rcs.seeCoalesceResult);
			ImGui::Checkbox("Visualize Gather Cascades", &rcs.visualizeRC3DGatherCascades);
			ImGui::Checkbox("Visualize Merge Cascades", &rcs.visualizeRC3DMergeCascades);
			if (rcs.visualizeRC3DGatherCascades || rcs.visualizeRC3DMergeCascades)
			{
				ImGui::SliderInt("Cascade Index", &rcs.cascadeVisIndex, 0, m_rcManager3D.GetCascadeIntervalCount() - 1);
			}

			ImGui::Checkbox("Visualize Probes", &rcs.enableCascadeProbeVis);
			if (rcs.enableCascadeProbeVis)
			{
				ImGui::SliderInt("Cascade Interval", &rcs.cascadeVisProbeIntervalIndex, 0, m_rcManager3D.GetCascadeIntervalCount() - 1);
				ImGui::SliderInt("Probe Subset", &rcs.cascadeVisProbeSubset, 1, 256);
			}
		}
	}
#pragma endregion


	
	ImGui::End();
}

void RadianceCascades::ClearPixelBuffers()
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
		gfxContext.TransitionResource(m_flatlandScene, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(m_flatlandScene);
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
		m_rcManager2D.ClearBuffers(gfxContext);
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

	cmptContext.TransitionResource(dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.InsertUAVBarrier(source);
	cmptContext.TransitionResource(source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cmptContext.SetPipelineState(m_fullScreenCopyComputePSO);
	cmptContext.SetRootSignature(m_fullScreenCopyComputeRootSig);

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
