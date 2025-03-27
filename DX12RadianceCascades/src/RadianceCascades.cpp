#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "Core\GameInput.h"

#include "ShaderCompilation\ShaderCompilationManager.h"

#include "RadianceCascades.h"

constexpr size_t MAX_INSTANCES = 256;
static const std::set<Shader::ShaderType> s_ValidShaderTypes = { Shader::ShaderTypeCS, Shader::ShaderTypeVS, Shader::ShaderTypePS };
static const DXGI_FORMAT s_flatlandSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

#define CASCADE_INDEX_VIS 0
#define SAMPLE_LEN_0 5.0f

namespace Math
{
	// Gives back the log of b with base of a.
	float LogAB(float a, float b)
	{
		return Math::Log(b) / Math::Log(a);
	}

	// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
	float GeometricSeriesSum(float a, float r, float n)
	{
		return a * (1.0f - Math::Pow(r, n)) / (1.0f - r);
	}
}

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
}

RadianceCascades::RadianceCascades()
	: m_mainSceneModelInstanceIndex(UINT32_MAX), m_mainViewport({}), m_mainScissor({}), m_usedPSOs({})
{
	m_sceneModels.reserve(MAX_INSTANCES);
}

RadianceCascades::~RadianceCascades()
{
}

void RadianceCascades::Startup()
{
	Renderer::Initialize();
	UpdateViewportAndScissor();

	InitializeScene();
	InitializeShaders();
	InitializePSOs();
	InitializeRCResources();
}

void RadianceCascades::Cleanup()
{
	Graphics::g_CommandManager.IdleGPU();

	{
		// Destroys all models in the scene by freeing their underlying pointers.
		for (ModelInstance& modelInstance : m_sceneModels)
		{
			modelInstance = nullptr;
		}

		m_flatlandScene.Destroy();
	}

	m_rcManager.Shutdown();

	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

	{	
		for (ModelInstance& modelInstance : m_sceneModels)
		{
			modelInstance.Update(gfxContext, deltaT);
		}
	}
	
	gfxContext.Finish();

	m_cameraController->Update(deltaT);
	m_camera.Update();

	UpdateViewportAndScissor();
	UpdateGraphicsPSOs();
}

void RadianceCascades::RenderScene()
{
	RenderSceneImpl(m_camera, m_mainViewport, m_mainScissor);
	RunComputeFlatlandScene();
	RunComputeRCGather();

	static uint32_t currentCascadeIndex = 0;
	for (int i = 0; i < m_rcManager.GetCascadeCount(); i++)
	{
		if (GameInput::IsFirstPressed((GameInput::DigitalInput)(i + 1)))
		{
			currentCascadeIndex = i;
		}
	}
	FullScreenCopyCompute(m_rcManager.GetCascadeInterval(currentCascadeIndex), Graphics::g_SceneColorBuffer);
	//FullScreenCopyCompute(m_flatlandScene, Graphics::g_SceneColorBuffer);
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	{
		ModelInstance& modelInstance = AddModelInstance(Renderer::LoadModel(L"assets\\Sponza\\PBR\\sponza2.gltf", false));
		m_mainSceneModelInstanceIndex = (uint32_t)m_sceneModels.size() - 1;
		modelInstance.Resize(100.0f * modelInstance.GetRadius());
	}

	{
		std::shared_ptr<Model> modelRef = Renderer::LoadModel(L"assets\\Testing\\SphereTest.gltf", false);
		ModelInstance& modelInstance = AddModelInstance(modelRef);
		modelInstance.Resize(10.0f);
		modelInstance.GetTransform().SetTranslation(m_sceneModels[m_mainSceneModelInstanceIndex].GetCenter());
	}

	// Setup camera
	{
		OrientedBox obb = m_sceneModels[m_mainSceneModelInstanceIndex].GetBoundingBox();
		float modelRadius = Length(obb.GetDimensions()) * 0.5f;
		const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
		m_camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));
		m_camera.SetZRange(0.5f, 10000.0f);

		m_cameraController.reset(new FlyingFPSCamera(m_camera, Vector3(kYUnitVector)));
	}
}

void RadianceCascades::InitializeShaders()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	// Pixel Shaders
	shaderCompManager.RegisterPixelShader(ShaderIDSceneRenderPS,		L"SceneRenderPS.hlsl",		true);
	shaderCompManager.RegisterPixelShader(ShaderIDFullScreenCopyPS,		L"DirectWritePS.hlsl",		true);

	// Vertex Shaders
	shaderCompManager.RegisterVertexShader(ShaderIDSceneRenderVS,		L"SceneRenderVS.hlsl",		true);
	shaderCompManager.RegisterVertexShader(ShaderIDFullScreenQuadVS,	L"FullScreenQuadVS.hlsl",	true);

	// Compute Shaders
	shaderCompManager.RegisterComputeShader(ShaderIDRCGatherCS,			L"RCGatherCS.hlsl",			true);
	shaderCompManager.RegisterComputeShader(ShaderIDFlatlandSceneCS,	L"FlatlandSceneCS.hlsl",	true);
	shaderCompManager.RegisterComputeShader(ShaderIDFullScreenCopyCS,	L"DirectCopyCS.hlsl",		true);
}

void RadianceCascades::InitializePSOs()
{
	ShaderCompilationManager& compManager = ShaderCompilationManager::Get();

	// Pointers to used PSOs
	{
		RegisterPSO(PSOIDFirstExternalPSO, &Renderer::sm_PSOs[9]);
		RegisterPSO(PSOIDSecondExternalPSO, &Renderer::sm_PSOs[11]);
		RegisterPSO(PSOIDComputeRCGatherPSO, &m_rcGatherPSO);
		RegisterPSO(PSOIDComputeFlatlandScenePSO, &m_flatlandScenePSO);
		RegisterPSO(PSOIDFullScreenCopyComputePSO, &m_fullScreenCopyComputePSO);
	}

	// Bind shader ids to specific PSOs for recompilation purposes.
	{
		// External PSO dependencies. These were found to be the PSOs that are used by opaque objects.
		std::vector<uint32_t> externalPSOs = { PSOIDFirstExternalPSO, PSOIDSecondExternalPSO };
		AddShaderDependency(ShaderIDSceneRenderPS, externalPSOs);
		AddShaderDependency(ShaderIDSceneRenderVS, externalPSOs);

		// Internal PSO dependencies.
		AddShaderDependency(ShaderIDRCGatherCS, { PSOIDComputeRCGatherPSO });
		AddShaderDependency(ShaderIDFlatlandSceneCS, { PSOIDComputeFlatlandScenePSO });
		AddShaderDependency(ShaderIDFullScreenCopyCS, { PSOIDFullScreenCopyComputePSO });
	}

	{
		ComputePSO& pso = m_rcGatherPSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDRCGatherCS));

		RootSignature& rootSig = m_computeGatherRootSig;
		rootSig.Reset(RootEntryRCGatherCount, 1);
		rootSig[RootEntryRCGatherGlobals].InitAsConstantBuffer(0);
		rootSig[RootEntryRCGatherCascadeInfo].InitAsConstantBuffer(1);
		rootSig[RootEntryRCGatherCascadeUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryRCGatherSceneSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);

		{
			SamplerDesc defaultSampler;
			rootSig.InitStaticSampler(0, defaultSampler);
		}

		rootSig.Finalize(L"Compute RC Gather");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = m_fullScreenCopyComputePSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDFullScreenCopyCS));

		RootSignature& rootSig = m_fullScreenCopyComputeRootSig;
		rootSig.Reset(RootEntryFullScreenCopyComputeCount, 1);
		rootSig[RootEntryFullScreenCopyComputeSource].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDest].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDestInfo].InitAsConstants(0, 2);

		{
			SamplerDesc sampler = Graphics::SamplerPointClampDesc;
			rootSig.InitStaticSampler(0, sampler);
		}

		rootSig.Finalize(L"Compute Full Screen Copy");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = m_flatlandScenePSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDFlatlandSceneCS));

		RootSignature& rootSig = m_computeFlatlandSceneRootSig;
		rootSig.Reset(RootEntryFlatlandCount, 0);
		rootSig[RootEntryFlatlandSceneInfo].InitAsConstants(0, 2);
		rootSig[RootEntryFlatlandSceneUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig.Finalize(L"Compute Flatland Scene");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}
}

void RadianceCascades::InitializeRCResources()
{
	m_flatlandScene.Create(L"Flatland Scene", ::GetSceneColorWidth(), ::GetSceneColorHeight(), 1, s_flatlandSceneFormat);

	float diag = Math::Length({ (float)::GetSceneColorWidth(), (float)::GetSceneColorHeight(), 0.0f });
	m_rcManager.Init(SAMPLE_LEN_0, diag);
}

void RadianceCascades::RenderSceneImpl(Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor)
{
	ColorBuffer& sceneColorBuffer = Graphics::g_SceneColorBuffer;
	DepthBuffer& sceneDepthBuffer = Graphics::g_SceneDepthBuffer;

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
	meshSorter.SetDepthStencilTarget(sceneDepthBuffer);
	meshSorter.AddRenderTarget(sceneColorBuffer);

	// Add models
	{
		for (ModelInstance& modelInstance : m_sceneModels)
		{
			modelInstance.Render(meshSorter);
		}

		meshSorter.Sort();
	}

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	gfxContext.TransitionResource(sceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.ClearColor(sceneColorBuffer);

	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(sceneDepthBuffer);

		meshSorter.RenderMeshes(Renderer::MeshSorter::kZPass, gfxContext, globals);
	}
	
	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

		gfxContext.SetRenderTarget(sceneColorBuffer.GetRTV(), sceneDepthBuffer.GetDSV());
		gfxContext.SetViewportAndScissor(viewPort, scissor);

		meshSorter.RenderMeshes(Renderer::MeshSorter::kOpaque, gfxContext, globals);

		gfxContext.Finish(true);
	}
}

void RadianceCascades::RunComputeFlatlandScene()
{
	ColorBuffer& targetScene = m_flatlandScene;
	uint32_t sceneWidth = targetScene.GetWidth();
	uint32_t sceneHeight = targetScene.GetHeight();

	ComputeContext& cmptContext = ComputeContext::Begin(L"Flatland Scene");

	cmptContext.TransitionResource(targetScene, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.ClearUAV(targetScene);

	cmptContext.SetPipelineState(m_flatlandScenePSO);
	cmptContext.SetRootSignature(m_computeFlatlandSceneRootSig);

	cmptContext.SetDynamicDescriptor(RootEntryFlatlandSceneUAV, 0, targetScene.GetUAV());
	cmptContext.SetConstants(RootEntryFlatlandSceneInfo, sceneWidth, sceneHeight);
	
	cmptContext.Dispatch2D(sceneWidth, sceneHeight);
	cmptContext.Finish(true);
}

void RadianceCascades::RunComputeRCGather()
{
	ColorBuffer& sceneBuffer = m_flatlandScene;
	RCGlobals rcGlobals = m_rcManager.FillRCGlobalsData(sceneBuffer.GetWidth(), sceneBuffer.GetHeight());

	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Gather Compute");

	cmptContext.SetRootSignature(m_computeGatherRootSig);
	cmptContext.SetPipelineState(m_rcGatherPSO);

	cmptContext.SetDynamicConstantBufferView(RootEntryRCGatherGlobals, sizeof(rcGlobals), &rcGlobals);

	cmptContext.TransitionResource(sceneBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cmptContext.SetDynamicDescriptor(RootEntryRCGatherSceneSRV, 0, sceneBuffer.GetSRV());

	for (uint32_t i = 0; i < m_rcManager.GetCascadeCount(); i++)
	{
		ColorBuffer& target = m_rcManager.GetCascadeInterval(i);

		CascadeInfo cascadeInfo = {};
		cascadeInfo.cascadeIndex = i;
		cascadeInfo.probePixelSize = m_rcManager.GetProbePixelSize(i);
		cmptContext.SetDynamicConstantBufferView(RootEntryRCGatherCascadeInfo, sizeof(cascadeInfo), &cascadeInfo);

		cmptContext.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.ClearUAV(target);
		cmptContext.SetDynamicDescriptor(RootEntryRCGatherCascadeUAV, 0, target.GetUAV());

		cmptContext.Dispatch2D(target.GetWidth(), target.GetHeight(), 16, 16);
	}

	cmptContext.Finish(true);
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

void RadianceCascades::UpdateGraphicsPSOs()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	if (shaderCompManager.HasRecentCompilations())
	{
		// Wait for all work to be done before changing PSOs.
		Graphics::g_CommandManager.IdleGPU();

		auto compSet = shaderCompManager.GetRecentCompilations();

		for (UUID64 shaderID : compSet)
		{
			Shader::ShaderType shaderType = shaderCompManager.GetShaderType(shaderID);

			if (s_ValidShaderTypes.find(shaderType) == s_ValidShaderTypes.end())
			{
				LOG_INFO(L"Invalid shader type: {}. Skipping.", (uint32_t)shaderType);
				continue;
			}

			void* binary = nullptr;
			size_t binarySize = 0;
			shaderCompManager.GetShaderDataBinary(shaderID, &binary, &binarySize);
			
			if (binary)
			{
				const auto& psoIds = m_shaderPSODependencyMap[shaderID];

				if (!psoIds.empty())
				{
					for (PSOIDType psoId : psoIds)
					{
						if (shaderType == Shader::ShaderTypeCS)
						{
							ComputePSO& pso = *(reinterpret_cast<ComputePSO*>(m_usedPSOs[psoId]));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								pso.SetComputeShader(binary, binarySize);
								pso.Finalize();
							}
						}
						else
						{
							GraphicsPSO& pso = *(reinterpret_cast<GraphicsPSO*>(m_usedPSOs[psoId]));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								if (shaderType == Shader::ShaderTypePS)
								{
									pso.SetPixelShader(binary, binarySize);
								}
								else if (shaderType == Shader::ShaderTypeVS)
								{
									pso.SetVertexShader(binary, binarySize);
								}

								pso.Finalize();
							}
						}
					}
				}
			}
		}

		shaderCompManager.ClearRecentCompilations();
	}
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

ModelInstance& RadianceCascades::AddModelInstance(std::shared_ptr<Model> modelPtr)
{
	ASSERT(m_sceneModels.size() < MAX_INSTANCES);

	m_sceneModels.push_back(ModelInstance(modelPtr));
	return m_sceneModels.back();
}

void RadianceCascades::AddShaderDependency(ShaderID shaderID, std::vector<uint32_t> psoIDs)
{
	for (uint32_t psoID : psoIDs)
	{
		m_shaderPSODependencyMap[shaderID].insert(psoID);
	}
}

void RadianceCascades::RegisterPSO(PSOID psoID, PSO* psoPtr)
{
	m_usedPSOs[psoID] = psoPtr;
}

RadianceCascadesManager::~RadianceCascadesManager()
{
	Shutdown();
}

void RadianceCascadesManager::Init(float _rayLength0, float _maxRayLength)
{
	const uint16_t rayScalingFactor = scalingFactor.rayScalingFactor;
	const uint16_t probeScalingFactor = scalingFactor.probeScalingFactor;
	const float rayScalingFactorFloat = (float)rayScalingFactor;
	const float probeScalingFactorFloat = (float)probeScalingFactor;

	// This is calculated by solving for factorCount in the following equation: rayLength0 * scalingFactor^(factorCount) = maxLength;
	// It calculates how many times the factor has to be applied to an initial ray length until the max length has been reached.
	float factorCount = Math::Ceiling(Math::LogAB(rayScalingFactorFloat, _maxRayLength / _rayLength0));
	float finalCascadeInvervalStart = Math::GeometricSeriesSum(_rayLength0, rayScalingFactorFloat, factorCount);
	uint32_t cascadeCount = (uint32_t)Math::Ceiling(Math::LogAB(rayScalingFactorFloat, finalCascadeInvervalStart)) - 1;

	const uint32_t probeCountPerDim0 = (uint32_t)Math::Pow(probeScalingFactorFloat, (float)cascadeCount);
	
	if (m_cascadeIntervals.size() < cascadeCount)
	{
		m_cascadeIntervals.resize((size_t)cascadeCount);
	}

	uint32_t probeCount = probeCountPerDim0 * probeCountPerDim0;
	uint32_t raysPerProbe = s_RaysPerProbe0; 
	for (uint32_t i = 0; i < (uint32_t)cascadeCount; i++)
	{
		std::wstring cascadeName = std::wstring(L"Cascade Interval ") + std::to_wstring(i);
	
		uint32_t probePixelLength = (uint32_t)Math::Sqrt((float)(probeCount * raysPerProbe));
		m_cascadeIntervals[i].Create(cascadeName, probePixelLength, probePixelLength, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);

		probeCount /= (uint32_t)Math::Pow(probeScalingFactorFloat, 2.0f);
		raysPerProbe *= rayScalingFactor;
	}

	// Set internal variables.
	rayLength0 = _rayLength0;
	probeDim0 = probeCountPerDim0;
}

void RadianceCascadesManager::Shutdown()
{
	for (ColorBuffer& cascadeInterval : m_cascadeIntervals)
	{
		cascadeInterval.Destroy();
	}
}

ColorBuffer& RadianceCascadesManager::GetCascadeInterval(uint32_t cascadeIndex)
{
	return m_cascadeIntervals[cascadeIndex];
}

uint32_t RadianceCascadesManager::GetCascadeCount()
{
	return (uint32_t)m_cascadeIntervals.size();
}

RCGlobals RadianceCascadesManager::FillRCGlobalsData(uint32_t scenePixelWidth, uint32_t scenePixelHeight)
{
	RCGlobals rcGlobals = {};
	rcGlobals.probeDim0 = probeDim0;
	rcGlobals.rayLength0 = rayLength0;
	rcGlobals.probeSpacing0 = GetProbeSpacing(0);
	rcGlobals.probeScalingFactor = scalingFactor.probeScalingFactor;
	rcGlobals.rayScalingFactor = scalingFactor.rayScalingFactor;
	rcGlobals.scenePixelWidth = scenePixelWidth;
	rcGlobals.scenePixelHeight = scenePixelHeight;

	return rcGlobals;
}

uint32_t RadianceCascadesManager::GetProbePixelSize(uint32_t cascadeIndex)
{
	return GetCascadeInterval(cascadeIndex).GetWidth() / GetProbeCount(cascadeIndex);
}

uint32_t RadianceCascadesManager::GetProbeCount(uint32_t cascadeIndex)
{
	return (uint32_t)(probeDim0 / Math::Pow(scalingFactor.probeScalingFactor, (float)cascadeIndex));
}

float RadianceCascadesManager::GetProbeSpacing(uint32_t cascadeIndex)
{
	float probeSpacing0 = GetCascadeInterval(0).GetWidth() / (float)(GetProbeCount(0));
	return probeSpacing0 * Math::Pow(scalingFactor.probeScalingFactor, (float)cascadeIndex);
}
