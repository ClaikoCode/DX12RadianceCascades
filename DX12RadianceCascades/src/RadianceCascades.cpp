#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"

#include "ShaderCompilation\ShaderCompilationManager.h"

#include "RadianceCascades.h"

RadianceCascades::RadianceCascades()
{
	
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
	m_sceneModelInstance = nullptr; // Destroys the scene.
	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");
	m_sceneModelInstance.Update(gfxContext, deltaT);
	gfxContext.Finish();

	m_cameraController->Update(deltaT);
	m_camera.Update();

	UpdateViewportAndScissor();
	UpdatePSOs();
}

void RadianceCascades::RenderScene()
{
	RenderSceneImpl(m_camera, m_mainViewport, m_mainScissor);
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	{
		m_sceneModelInstance = Renderer::LoadModel(L"assets\\Sponza\\PBR\\sponza2.gltf", false);
		m_sceneModelInstance.Resize(100.0f * m_sceneModelInstance.GetRadius());

		OrientedBox obb = m_sceneModelInstance.GetBoundingBox();
		float modelRadius = Length(obb.GetDimensions()) * 0.5f;
		const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
		m_camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));
		m_camera.SetZRange(0.5f, 10000.0f);
	}

	m_cameraController.reset(new FlyingFPSCamera(m_camera, Vector3(kYUnitVector)));
	
}

void RadianceCascades::InitializeShaders()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	shaderCompManager.RegisterShader(ShaderIDTest, L"VertexShaderTest.hlsl", Shader::ShaderTypeVS, true);
	shaderCompManager.RegisterShader(ShaderIDSceneRenderPS, L"SceneRenderPS.hlsl", Shader::ShaderTypePS, true);
	shaderCompManager.RegisterShader(ShaderIDSceneRenderVS, L"SceneRenderVS.hlsl", Shader::ShaderTypeVS, true);
}

void RadianceCascades::InitializePSOs()
{
	m_shaderPSOMap[ShaderIDSceneRenderPS].insert(9);
	m_shaderPSOMap[ShaderIDSceneRenderPS].insert(11);

	m_shaderPSOMap[ShaderIDSceneRenderVS].insert(9);
	m_shaderPSOMap[ShaderIDSceneRenderVS].insert(11);
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
		m_sceneModelInstance.Render(meshSorter);

		meshSorter.Sort();
	}

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	gfxContext.TransitionResource(sceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.ClearColor(sceneColorBuffer);

	gfxContext.TransitionResource(Graphics::g_SSAOFullScreen, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.ClearColor(Graphics::g_SSAOFullScreen);

	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(sceneDepthBuffer);

		meshSorter.RenderMeshes(Renderer::MeshSorter::kZPass, gfxContext, globals);
	}
	
	{
		gfxContext.TransitionResource(Graphics::g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

		gfxContext.SetRenderTarget(sceneColorBuffer.GetRTV(), sceneDepthBuffer.GetDSV());
		gfxContext.SetViewportAndScissor(viewPort, scissor);

		meshSorter.RenderMeshes(Renderer::MeshSorter::kOpaque, gfxContext, globals);

		gfxContext.Finish(true);
	}
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

void RadianceCascades::UpdatePSOs()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	if (shaderCompManager.HasRecentCompilations())
	{
		auto compSet = shaderCompManager.GetRecentCompilations();

		std::set<uint16_t> changedPSOs = {};

		for (UUID64 shaderID : compSet)
		{
			Shader::ShaderType shaderType = shaderCompManager.GetShaderType(shaderID);

			if (shaderType != Shader::ShaderTypePS && shaderType != Shader::ShaderTypeVS)
			{
				LOG_ERROR(L"Invalid shader type: {}", (uint32_t)shaderType);
				continue;
			}

			void* binary = nullptr;
			size_t binarySize = 0;
			shaderCompManager.GetShaderDataBinary(shaderID, &binary, &binarySize);
			
			if (binary)
			{
				const std::set<uint16_t>& psoIds = m_shaderPSOMap[shaderID];

				if (!psoIds.empty())
				{
					for (uint16_t psoId : psoIds)
					{
						GraphicsPSO& pso = Renderer::sm_PSOs[psoId];

						if (shaderType == Shader::ShaderTypePS)
						{
							pso.SetPixelShader(binary, binarySize);
						}
						else if (shaderType == Shader::ShaderTypeVS)
						{
							pso.SetVertexShader(binary, binarySize);
						}

						changedPSOs.insert(psoId);
					}
				}
			}
		}

		for (uint16_t changedPSOId : changedPSOs)
		{
			Renderer::sm_PSOs[changedPSOId].Finalize();
		}

		shaderCompManager.ClearRecentCompilations();
	}
}
