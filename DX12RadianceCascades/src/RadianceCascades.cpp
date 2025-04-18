#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "Core\GameInput.h"

#include "ShaderCompilation\ShaderCompilationManager.h"
#include "RadianceCascades.h"

using namespace Microsoft::WRL;

constexpr size_t MAX_INSTANCES = 256;
static const std::set<Shader::ShaderType> s_ValidShaderTypes = { Shader::ShaderTypeCS, Shader::ShaderTypeVS, Shader::ShaderTypePS, Shader::ShaderTypeRT };
static const DXGI_FORMAT s_FlatlandSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const std::wstring s_BackupModelPath = L"models\\Testing\\SphereTest.gltf";
static const std::wstring s_HitGroupName = L"HitGroup";
static const std::vector<std::wstring> s_DXILExports = { L"RayGenerationShader", L"AnyHitShader", L"ClosestHitShader", L"MissShader" };

#define SAMPLE_LEN_0 20.0f
#define RAYS_PER_PROBE_0 4.0f

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

	ShaderTable<LocalHitData> CreateModelShaderTable(const std::wstring& exportName, std::shared_ptr<const Model> modelPtr, DescriptorHandle geometrySRV, RaytracingPSO& rtPSO)
	{
		ASSERT(modelPtr != nullptr);

		ShaderTable<LocalHitData> hitShaderTable = {};

		const Model& model = *modelPtr;
		const Mesh* meshPtr = (const Mesh*)model.m_MeshData.get();
		void* shaderIdentifier = rtPSO.GetShaderIdentifier(exportName);
		for (int i = 0; i < (int)model.m_NumMeshes; i++)
		{
			const Mesh& mesh = meshPtr[i];
			ASSERT(mesh.numDraws == 1);

			auto& entry = hitShaderTable.emplace_back();
			entry.entryData.materialSRVs		= Renderer::s_TextureHeap[mesh.srvTable]; // Start of descriptor table.
			entry.entryData.geometrySRV			= geometrySRV;
			entry.entryData.indexByteOffset		= mesh.ibOffset;
			entry.entryData.vertexByteOffset	= mesh.vbOffset;

			entry.SetShaderIdentifier(shaderIdentifier);
		}

		return hitShaderTable;
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

	InitializeResources();
	InitializeHeaps();
	InitializeScene();
	InitializeShaders();
	InitializePSOs();
	InitializeRCResources();

	InitializeRT();

	{
		DescriptorHandle& handle = m_descCopies.sceneColorUAVHandle;
		handle = m_descCopies.descHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].Alloc();
		Graphics::g_Device->CopyDescriptorsSimple(1, handle, Graphics::g_SceneColorBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

void RadianceCascades::Cleanup()
{
	Graphics::g_CommandManager.IdleGPU();
	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
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

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

	{	
		for (ModelInstance& modelInstance : m_sceneModels)
		{
			modelInstance.Update(gfxContext, deltaT);
		}
	}
	
	gfxContext.Finish();

	UpdateViewportAndScissor();
	UpdateGraphicsPSOs();
}

void RadianceCascades::RenderScene()
{
	ClearPixelBuffers();

	if (m_settings.globalSettings.renderRaster)
	{
		RenderSceneImpl(m_camera, m_mainViewport, m_mainScissor);
	}

	if (m_settings.globalSettings.renderRaytracing)
	{
		RenderRaytracing(m_camera, Graphics::g_SceneColorBuffer);
	}

	if (m_settings.rcSettings.visualize2DCascades)
	{
		RunComputeFlatlandScene();
		RunComputeRCGather();
		RunComputeRCMerge();
		RunComputeRCRadianceField(Graphics::g_SceneColorBuffer);

		static int currentCascadeVisIndex = -1;
		for (int i = 0; i < (int)m_rcManager.GetCascadeCount(); i++)
		{
			if (GameInput::IsFirstPressed((GameInput::DigitalInput)(i + 1)))
			{
				currentCascadeVisIndex = i;
			}
		}

		if (GameInput::IsFirstPressed(GameInput::kKey_0))
		{
			currentCascadeVisIndex = -1;
		}

		if (currentCascadeVisIndex != -1)
		{
			FullScreenCopyCompute(m_rcManager.GetCascadeInterval(currentCascadeVisIndex), Graphics::g_SceneColorBuffer);
		}
	}
}

void RadianceCascades::InitializeResources()
{
	m_models[ModelIDSponza]			= Renderer::LoadModel(L"models\\Sponza\\PBR\\sponza2.gltf", false);
	m_models[ModelIDSphereTest]		= Renderer::LoadModel(L"models\\Testing\\SphereTest.gltf", false);
}

void RadianceCascades::InitializeHeaps()
{
	m_descCopies.Init();
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	Math::Vector3 sceneModelCenter = {};
	{
		ModelInstance& modelInstance = AddModelInstance(ModelIDSponza);
		m_mainSceneModelInstanceIndex = (uint32_t)m_sceneModels.size() - 1;
		modelInstance.Resize(100.0f * modelInstance.GetRadius());

		sceneModelCenter = modelInstance.GetBoundingBox().GetCenter();
	}

	{
		ModelInstance& modelInstance = AddModelInstance(ModelIDSponza);
		modelInstance.Resize(10.0f * modelInstance.GetRadius());

		Math::Vector3 instanceCenter = sceneModelCenter;
		instanceCenter.SetY(20.0f);
		modelInstance.GetTransform().SetTranslation(instanceCenter);
	}

	{
		ModelInstance& modelInstance = AddModelInstance(ModelIDSphereTest);
		modelInstance.Resize(100.0f * modelInstance.GetRadius());
		modelInstance.GetTransform().SetTranslation(sceneModelCenter);
	}

	// Setup camera
	{
		m_camera.SetAspectRatio((float)GetSceneColorHeight() / (float)GetSceneColorWidth());

		OrientedBox obb = GetMainSceneModelInstance().GetBoundingBox();
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
	shaderCompManager.RegisterComputeShader(ShaderIDRCMergeCS,			L"RCMergeCS.hlsl",			true);
	shaderCompManager.RegisterComputeShader(ShaderIDRCRadianceFieldCS,	L"RCRadianceFieldCS.hlsl",	true);

	// RT Shaders
	shaderCompManager.RegisterRaytracingShader(ShaderIDRaytracingTestRT, L"RaytracingTest.hlsl",	true);
}

void RadianceCascades::InitializePSOs()
{
	ShaderCompilationManager& compManager = ShaderCompilationManager::Get();

	// Pointers to used PSOs
	{
		RegisterPSO(PSOIDFirstExternalPSO,			&Renderer::sm_PSOs[9],			PSOTypeGraphics);
		RegisterPSO(PSOIDSecondExternalPSO,			&Renderer::sm_PSOs[11],			PSOTypeGraphics);
		RegisterPSO(PSOIDComputeRCGatherPSO,		&m_rcGatherPSO,					PSOTypeCompute);
		RegisterPSO(PSOIDComputeFlatlandScenePSO,	&m_flatlandScenePSO,			PSOTypeCompute);
		RegisterPSO(PSOIDComputeFullScreenCopyPSO,	&m_fullScreenCopyComputePSO,	PSOTypeCompute);
		RegisterPSO(PSOIDComputeRCMergePSO,			&m_rcMergePSO,					PSOTypeCompute);
		RegisterPSO(PSOIDComputeRCRadianceFieldPSO, &m_rcRadianceFieldPSO,			PSOTypeCompute);
		RegisterPSO(PSOIDRaytracingTestPSO,			&m_rtTestPSO,					PSOTypeRaytracing);

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
		AddShaderDependency(ShaderIDFullScreenCopyCS, { PSOIDComputeFullScreenCopyPSO });
		AddShaderDependency(ShaderIDRCMergeCS, { PSOIDComputeRCMergePSO });
		AddShaderDependency(ShaderIDRCRadianceFieldCS, { PSOIDComputeRCRadianceFieldPSO });
		AddShaderDependency(ShaderIDRaytracingTestRT, { PSOIDRaytracingTestPSO });
		
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
			SamplerDesc sampler = Graphics::SamplerPointBorderDesc;
			rootSig.InitStaticSampler(0, sampler);
		}

		rootSig.Finalize(L"Compute RC Gather");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}

	{
		ComputePSO& pso = m_fullScreenCopyComputePSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDFullScreenCopyCS));

		RootSignature& rootSig = m_fullScreenCopyComputeRootSig;
		rootSig.Reset(RootEntryFullScreenCopyComputeCount, 2);
		rootSig[RootEntryFullScreenCopyComputeSource].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDest].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryFullScreenCopyComputeDestInfo].InitAsConstants(0, 2);

		{
			SamplerDesc pointSampler = Graphics::SamplerPointClampDesc;
			rootSig.InitStaticSampler(0, pointSampler);

			SamplerDesc linearSampler = Graphics::SamplerLinearClampDesc;
			rootSig.InitStaticSampler(1, linearSampler);
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

	{
		ComputePSO& pso = m_rcMergePSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDRCMergeCS));

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
		ComputePSO& pso = m_rcRadianceFieldPSO;
		pso.SetComputeShader(compManager.GetShaderByteCode(ShaderIDRCRadianceFieldCS));
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
}

void RadianceCascades::InitializeRCResources()
{
	m_flatlandScene.Create(L"Flatland Scene", ::GetSceneColorWidth(), ::GetSceneColorHeight(), 1, s_FlatlandSceneFormat);

	float diag = Math::Length({ (float)::GetSceneColorWidth(), (float)::GetSceneColorHeight(), 0.0f });
	m_rcManager.Init(SAMPLE_LEN_0, RAYS_PER_PROBE_0, diag);
}

void RadianceCascades::InitializeRT()
{
	RaytracingPSO& pso = m_rtTestPSO;
	{
		RootSignature1& globalRootSig = m_rtTestGlobalRootSig;
		globalRootSig.Reset(RootEntryRTGCount, 1);
		globalRootSig[RootEntryRTGSRV].InitAsShaderResourceView(0);
		globalRootSig[RootEntryRTGUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		globalRootSig[RootEntryRTGParamCB].InitAsConstantBufferView(0);
		globalRootSig[RootEntryRTGInfoCB].InitAsConstantBufferView(1);
		{
			SamplerDesc sampler = Graphics::SamplerLinearWrapDesc;
			globalRootSig.InitStaticSampler(0, sampler);
		}

		globalRootSig.Finalize(L"Global Root Signature");
		pso.SetGlobalRootSignature(&globalRootSig);

		RootSignature1& localRootSig = m_rtTestLocalRootSig;
		const uint32_t localRootSigSpace = 1;
		localRootSig.Reset(RootEntryRTLCount, 0);
		localRootSig[RootEntryRTLGeometryDataSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRTLTextureSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, D3D12_SHADER_VISIBILITY_ALL, localRootSigSpace);
		localRootSig[RootEntryRTLOffsetConstants].InitAsConstants(2, 0, localRootSigSpace, D3D12_SHADER_VISIBILITY_ALL);
		localRootSig.Finalize(L"Local Root Signature", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		pso.SetLocalRootSignature(&localRootSig);

		D3D12_SHADER_BYTECODE byteCode = ShaderCompilationManager::Get().GetShaderByteCode(ShaderIDRaytracingTestRT);

		pso.SetDxilLibrary(s_DXILExports, byteCode);
		pso.SetPayloadAndAttributeSize(4, 8);

		pso.SetHitGroup(s_HitGroupName, D3D12_HIT_GROUP_TYPE_TRIANGLES);
		pso.SetClosestHitShader(L"ClosestHitShader");
		
		pso.SetMaxRayRecursionDepth(1);

		pso.Finalize();
	}

	// Initialize BLASEs
	{
		for (InternalModelInstance& modelInstance : m_sceneModels)
		{
			ModelID modelID = modelInstance.underlyingModelID;

			auto it = m_modelBLASes.find(modelID);
			if (it == m_modelBLASes.end())
			{
				m_modelBLASes[modelID].Init(GetModelPtr(modelID), m_descCopies.descHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
			}
		}
	}

	// Initialize TLASes.
	std::vector<TLASInstanceGroup> instanceGroups = {};
	std::unordered_map<ModelID, std::vector<Utils::GPUMatrix>> blasInstances = {};
	{
		for (InternalModelInstance& modelInstance : m_sceneModels)
		{
			ModelID modelID = modelInstance.underlyingModelID;
			Math::Matrix4 mat = Math::Matrix4(modelInstance.GetTransform());
			blasInstances[modelID].push_back(mat);
		}

		for (auto& [key, value] : blasInstances)
		{
			TLASInstanceGroup instanceGroup = {};
			instanceGroup.blasBuffer = &m_modelBLASes[key];
			instanceGroup.instanceTransforms = value;

			instanceGroups.push_back(instanceGroup);
		}

		m_sceneModelTLASInstance.Init(instanceGroups);
	}

	// Initialize shader tables.
	// Will append a shader table for each unique BLAS in the order of the rendered instances.
	{
		ShaderTable<LocalHitData> hitShaderTable = {};
		for (auto& [key, value] : blasInstances)
		{
			
			auto& blasBuffer = m_modelBLASes[key];
			ShaderTable<LocalHitData> blasSpecificHitTable = ::CreateModelShaderTable(s_HitGroupName, blasBuffer.GetModelPtr(), blasBuffer.GetGeometrySRV(), pso);

			hitShaderTable.insert(std::end(hitShaderTable), std::begin(blasSpecificHitTable), std::end(blasSpecificHitTable));
		}

		m_testRTDispatch.Init(pso, hitShaderTable, L"RayGenerationShader", L"MissShader");
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

	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		meshSorter.RenderMeshes(Renderer::MeshSorter::kZPass, gfxContext, globals);
	}
	
	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);
		gfxContext.TransitionResource(sceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

		gfxContext.SetRenderTarget(sceneColorBuffer.GetRTV(), sceneDepthBuffer.GetDSV());
		gfxContext.SetViewportAndScissor(viewPort, scissor);

		meshSorter.RenderMeshes(Renderer::MeshSorter::kOpaque, gfxContext, globals);

		gfxContext.Finish(true);
	}
}

void RadianceCascades::RenderRaytracing(Camera& camera, ColorBuffer& colorTarget)
{
	__declspec(align(16)) struct RTParams
	{
		uint32_t dispatchWidth;
		uint32_t dispatchHeight;
		uint32_t rayFlags;
		float holeSize;
	} rtParams;

	rtParams.dispatchHeight = colorTarget.GetHeight();
	rtParams.dispatchWidth = colorTarget.GetWidth();
	rtParams.rayFlags = D3D12_RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
	rtParams.holeSize = 0.0f;

	__declspec(align(16)) struct GlobalInfo
	{
		Utils::GPUMatrix viewProjMatrix;
		Utils::GPUMatrix invViewProjMatrix;
		Math::Vector3 cameraPos;
	} rtGlobalInfo;

	Math::Matrix4 viewProjMatrix = camera.GetViewProjMatrix();
	rtGlobalInfo.viewProjMatrix = viewProjMatrix;

	Math::Matrix4 invViewProjMatrix = Math::Matrix4(DirectX::XMMatrixInverse(nullptr, viewProjMatrix));
	rtGlobalInfo.invViewProjMatrix = invViewProjMatrix;

	rtGlobalInfo.cameraPos = camera.GetPosition();

	ComputeContext& cmptContext = ComputeContext::Begin(L"Render Raytracing");
	ComPtr<ID3D12GraphicsCommandList4> rtCommandList = nullptr;
	ThrowIfFailed(cmptContext.GetCommandList()->QueryInterface(rtCommandList.GetAddressOf()));

	// Transition resources
	{
		cmptContext.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmptContext.FlushResourceBarriers();
	}

	cmptContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Renderer::s_TextureHeap.GetHeapPointer());

	ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_descCopies.descHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].GetHeapPointer() };
	rtCommandList->SetDescriptorHeaps(1, pDescriptorHeaps);
	rtCommandList->SetComputeRootSignature(m_rtTestGlobalRootSig.GetSignature());
	rtCommandList->SetComputeRootShaderResourceView(RootEntryRTGSRV, m_sceneModelTLASInstance.GetBVH());
	rtCommandList->SetComputeRootDescriptorTable(RootEntryRTGUAV, m_descCopies.sceneColorUAVHandle);
	cmptContext.SetDynamicConstantBufferView(RootEntryRTGParamCB, sizeof(RTParams), &rtParams);
	cmptContext.SetDynamicConstantBufferView(RootEntryRTGInfoCB, sizeof(GlobalInfo), &rtGlobalInfo);

	D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = m_testRTDispatch.BuildDispatchRaysDesc(colorTarget.GetWidth(), colorTarget.GetHeight());
	rtCommandList->SetPipelineState1(m_rtTestPSO.GetStateObject().Get());
	rtCommandList->DispatchRays(&rayDispatchDesc);

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
	RCGlobals rcGlobals = m_rcManager.FillRCGlobalsData(sceneBuffer.GetWidth());

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

	RCGlobals rcGlobals = {};
	{
		ColorBuffer& cascade0 = m_rcManager.GetCascadeInterval(0);
		rcGlobals = m_rcManager.FillRCGlobalsData(cascade0.GetWidth());
	}
	 
	cmptContext.SetDynamicConstantBufferView(RootEntryRCMergeGlobals, sizeof(rcGlobals), &rcGlobals);

	// Start loop at second last cascade and go down to the first cascade.
	for (int i = m_rcManager.GetCascadeCount() - 2; i >= 0; i--)
	{
		ColorBuffer& target = m_rcManager.GetCascadeInterval(i);
		ColorBuffer& source = m_rcManager.GetCascadeInterval(i + 1);
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

void RadianceCascades::RunComputeRCRadianceField(ColorBuffer& outputBuffer)
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Radiance Field Compute");
	cmptContext.SetRootSignature(m_rcRadianceFieldRootSig);
	cmptContext.SetPipelineState(m_rcRadianceFieldPSO);

	ColorBuffer& radianceField = m_rcManager.GetRadianceField();
	ColorBuffer& targetCascade = m_rcManager.GetCascadeInterval(0);

	RCGlobals rcGlobals = m_rcManager.FillRCGlobalsData(targetCascade.GetWidth());
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

			D3D12_SHADER_BYTECODE shaderByteCode = shaderCompManager.GetShaderByteCode(shaderID);
			
			if (shaderByteCode.pShaderBytecode)
			{
				const auto& psoIds = m_shaderPSODependencyMap[shaderID];

				if (!psoIds.empty())
				{
					for (PSOIDType psoId : psoIds)
					{
						const PSOPackage& package = m_usedPSOs[psoId];

						if (package.psoType == PSOTypeCompute)
						{
							ComputePSO& pso = *(reinterpret_cast<ComputePSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								pso.SetComputeShader(shaderByteCode);
								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeGraphics)
						{
							GraphicsPSO& pso = *(reinterpret_cast<GraphicsPSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								if (shaderType == Shader::ShaderTypePS)
								{
									pso.SetPixelShader(shaderByteCode);
								}
								else if (shaderType == Shader::ShaderTypeVS)
								{
									pso.SetVertexShader(shaderByteCode);
								}

								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeRaytracing)
						{
							//LOG_INFO(L"Raytracing PSO Reloading is not implemented yet.");
							//continue;

							RaytracingPSO& pso = *(reinterpret_cast<RaytracingPSO*>(package.PSOPointer));

							if (pso.GetStateObject() != nullptr)
							{
								pso.SetDxilLibrary(s_DXILExports, shaderByteCode);
							
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

void RadianceCascades::ClearPixelBuffers()
{
	ColorBuffer& sceneColorBuffer = Graphics::g_SceneColorBuffer;
	DepthBuffer& sceneDepthBuffer = Graphics::g_SceneDepthBuffer;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Clear Pixel Buffers");

	{
		gfxContext.TransitionResource(sceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
		gfxContext.ClearDepth(sceneDepthBuffer);

		gfxContext.TransitionResource(sceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(sceneColorBuffer);
	}

	{
		gfxContext.TransitionResource(m_flatlandScene, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(m_flatlandScene);
	}
	
	m_rcManager.ClearBuffers(gfxContext);

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

InternalModelInstance& RadianceCascades::AddModelInstance(ModelID modelID)
{
	ASSERT(m_sceneModels.size() < MAX_INSTANCES);
	std::shared_ptr<Model> modelPtr = GetModelPtr(modelID);

	if (modelPtr == nullptr)
	{
		modelPtr = Renderer::LoadModel(s_BackupModelPath, false);
		LOG_ERROR(L"Model was invalid. Using backup model instead. If Sponza model is missing, download a Sponza PBR gltf model online.");
	}

	return m_sceneModels.emplace_back(modelPtr, modelID);
}

void RadianceCascades::AddShaderDependency(ShaderID shaderID, std::vector<uint32_t> psoIDs)
{
	for (uint32_t psoID : psoIDs)
	{
		m_shaderPSODependencyMap[shaderID].insert(psoID);
	}
}

void RadianceCascades::RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType)
{
	m_usedPSOs[psoID] = { psoPtr, psoType };
}

void DescriptorHeaps::Init()
{
	descHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].Create(L"Desc Copies CBV SRV UAV", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048);
}
