#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"

#include "ShaderCompilation\ShaderCompilationManager.h"

#include "RadianceCascades.h"

constexpr size_t MAX_INSTANCES = 256;
static const std::set<Shader::ShaderType> s_ValidShaderTypes = { Shader::ShaderTypeCS, Shader::ShaderTypeVS, Shader::ShaderTypePS };

RadianceCascades::RadianceCascades()
	: m_mainSceneIndex(UINT32_MAX), m_mainViewport({}), m_mainScissor({})
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
}

void RadianceCascades::Cleanup()
{
	// Destroys all models in the scene by freeing their underlying pointers.
	for (ModelInstance& modelInstance : m_sceneModels)
	{
		modelInstance = nullptr;
	}

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
	RunCompute();
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	{
		ModelInstance& modelInstance = AddModelInstance(Renderer::LoadModel(L"assets\\Sponza\\PBR\\sponza2.gltf", false));
		m_mainSceneIndex = (uint32_t)m_sceneModels.size() - 1;
		modelInstance.Resize(100.0f * modelInstance.GetRadius());
	}

	{
		std::shared_ptr<Model> modelRef = Renderer::LoadModel(L"assets\\Testing\\SphereTest.gltf", false);
		ModelInstance& modelInstance = AddModelInstance(modelRef);
		modelInstance.Resize(10.0f);
		modelInstance.GetTransform().SetTranslation(m_sceneModels[m_mainSceneIndex].GetCenter());
	}

	// Setup camera
	{
		OrientedBox obb = m_sceneModels[m_mainSceneIndex].GetBoundingBox();
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

	shaderCompManager.RegisterShader(ShaderIDSceneRenderPS, L"SceneRenderPS.hlsl", Shader::ShaderTypePS, true);
	shaderCompManager.RegisterShader(ShaderIDSceneRenderVS, L"SceneRenderVS.hlsl", Shader::ShaderTypeVS, true);
	shaderCompManager.RegisterShader(ShaderIDTestCS, L"TestCS.hlsl", Shader::ShaderTypeCS, true);
}

void RadianceCascades::InitializePSOs()
{
	
	{
		// Outside PSO registers. These were found to be the PSOs that are used by opaque objects.
		m_usedPSOs[PSOIDFirstOutsidePSO] = &Renderer::sm_PSOs[9];
		m_usedPSOs[PSOIDSecondOutsidePSO] = &Renderer::sm_PSOs[11];

		// Internal PSO registers.
		m_usedPSOs[PSOIDComputeTestPSO] = &m_psoTest;
	}

	{
		m_shaderPSODependencyMap[ShaderIDSceneRenderPS].insert(PSOIDFirstOutsidePSO);
		m_shaderPSODependencyMap[ShaderIDSceneRenderPS].insert(PSOIDSecondOutsidePSO);

		m_shaderPSODependencyMap[ShaderIDSceneRenderVS].insert(PSOIDFirstOutsidePSO);
		m_shaderPSODependencyMap[ShaderIDSceneRenderVS].insert(PSOIDSecondOutsidePSO);

		m_shaderPSODependencyMap[ShaderIDTestCS].insert(PSOIDComputeTestPSO);
	}
	
	{
		ComputePSO& computePSO = m_psoTest;

		void* binary = nullptr;
		size_t binarySize = 0;
		ShaderCompilationManager::Get().GetShaderDataBinary(ShaderIDTestCS, &binary, &binarySize);
		computePSO.SetComputeShader(binary, binarySize);

		RootSignature& rootSig = m_rootSigTest;
		rootSig.Reset(2, 0);
		rootSig[0].InitAsConstants(0, 1);
		rootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);

		rootSig.Finalize(L"Compute Test Rootsig");

		computePSO.SetRootSignature(rootSig);
		computePSO.Finalize();
	}
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

void RadianceCascades::RunCompute()
{
	ColorBuffer& target = Graphics::g_SceneColorBuffer;

	ComputeContext& cmptContext = ComputeContext::Begin(L"Run Compute");

	cmptContext.SetRootSignature(m_rootSigTest);
	cmptContext.SetPipelineState(m_psoTest);

	cmptContext.SetConstants(0, 10.0f);

	cmptContext.TransitionResource(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	cmptContext.SetDynamicDescriptor(1, 0, target.GetUAV());
	cmptContext.Dispatch2D(target.GetWidth(), target.GetHeight());

	cmptContext.Finish();
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
				const std::set<uint16_t>& psoIds = m_shaderPSODependencyMap[shaderID];

				if (!psoIds.empty())
				{
					for (uint16_t psoId : psoIds)
					{
						if (shaderType == Shader::ShaderTypeCS)
						{
							ComputePSO& pso = *(reinterpret_cast<ComputePSO*>(m_usedPSOs[psoId]));

							pso.SetComputeShader(binary, binarySize);

							pso.Finalize();
						}
						else
						{
							GraphicsPSO& pso = *(reinterpret_cast<GraphicsPSO*>(m_usedPSOs[psoId]));

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

		shaderCompManager.ClearRecentCompilations();
	}
}

ModelInstance& RadianceCascades::AddModelInstance(std::shared_ptr<Model> modelPtr)
{
	ASSERT(m_sceneModels.size() < MAX_INSTANCES);

	m_sceneModels.push_back(ModelInstance(modelPtr));
	return m_sceneModels.back();
}
