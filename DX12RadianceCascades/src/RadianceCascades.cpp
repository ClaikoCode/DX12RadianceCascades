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

#if 0
	const Model& model = m_sceneModelInstance.GetModel();
	const uint8_t* meshPtr = model.m_MeshData.get();
	const Mesh& mesh = *(const Mesh*)(meshPtr);

	uint16_t psoFlags = mesh.psoFlags; // Grab the PSO flags for the first mesh.
	std::vector<D3D12_INPUT_ELEMENT_DESC> vertexLayout;
	if (psoFlags & PSOFlags::kHasPosition)
		vertexLayout.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT });
	if (psoFlags & PSOFlags::kHasNormal)
		vertexLayout.push_back({ "NORMAL",   0, DXGI_FORMAT_R10G10B10A2_UNORM,  0, D3D12_APPEND_ALIGNED_ELEMENT });
	if (psoFlags & PSOFlags::kHasTangent)
		vertexLayout.push_back({ "TANGENT",  0, DXGI_FORMAT_R10G10B10A2_UNORM,  0, D3D12_APPEND_ALIGNED_ELEMENT });
	if (psoFlags & PSOFlags::kHasUV0)
		vertexLayout.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT });
	else
		vertexLayout.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT,       1, D3D12_APPEND_ALIGNED_ELEMENT });
	if (psoFlags & PSOFlags::kHasUV1)
		vertexLayout.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R16G16_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT });
	if (psoFlags & PSOFlags::kHasSkin)
	{
		vertexLayout.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
		vertexLayout.push_back({ "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	}

	SamplerDesc defaultSamplerDesc = {};
	defaultSamplerDesc.MaxAnisotropy = 8;
	RootSignature& rootSig = m_rootSig;
	rootSig.Reset(Renderer::kNumRootBindings, 1);
	rootSig.InitStaticSampler(10, defaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSig[Renderer::kMeshConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	rootSig[Renderer::kMaterialConstants].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSig[Renderer::kMaterialSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSig[Renderer::kMaterialSamplers].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0, 10, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSig[Renderer::kCommonSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 10, D3D12_SHADER_VISIBILITY_PIXEL);
	rootSig[Renderer::kCommonCBV].InitAsConstantBuffer(1);
	rootSig[Renderer::kSkinMatrices].InitAsBufferSRV(20, D3D12_SHADER_VISIBILITY_VERTEX);
	rootSig.Finalize(L"TestRootSig", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	std::array<DXGI_FORMAT, 2> rtvFormats = {};
	rtvFormats[0] = Graphics::g_SceneColorBuffer.GetFormat();
	rtvFormats[1] = Graphics::g_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT depthFormat = Graphics::g_SceneDepthBuffer.GetFormat();

	GraphicsPSO pso(L"TestPSO");
	pso.SetRootSignature(rootSig);
	pso.SetInputLayout((UINT)vertexLayout.size(), vertexLayout.data());
	pso.SetRasterizerState(Graphics::RasterizerDefault);
	pso.SetBlendState(Graphics::BlendDisable);
	pso.SetDepthStencilState(Graphics::DepthStateReadWrite);
	pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	pso.SetRenderTargetFormats((UINT)rtvFormats.size(), rtvFormats.data(), depthFormat);

	void* binary = nullptr;
	size_t binarySize = 0;
	
	shaderCompManager.GetCompiledShaderData(ShaderIDSceneRenderVS, &binary, &binarySize);
	pso.SetVertexShader(binary, binarySize);
	
	shaderCompManager.GetCompiledShaderData(ShaderIDSceneRenderPS, &binary, &binarySize);
	pso.SetPixelShader(binary, binarySize);
	
	pso.Finalize();
#else

	GraphicsPSO pso = Renderer::sm_PSOs[9];
	
	void* binary = nullptr;
	size_t binarySize = 0;

	shaderCompManager.GetCompiledShaderData(ShaderIDSceneRenderVS, &binary, &binarySize);
	pso.SetVertexShader(binary, binarySize);

	shaderCompManager.GetCompiledShaderData(ShaderIDSceneRenderPS, &binary, &binarySize);
	pso.SetPixelShader(binary, binarySize);

	pso.Finalize();

#endif

	Renderer::sm_PSOs[9] = pso;
	Renderer::sm_PSOs[11] = pso;
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
