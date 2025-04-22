#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "Core\GameInput.h"

#include "Core\DynamicDescriptorHeap.h"
#include "GPUStructs.h"
#include "DebugDrawer.h"
#include "ShaderCompilation\ShaderCompilationManager.h"

#include "RadianceCascades.h"

using namespace Microsoft::WRL;

constexpr size_t MAX_INSTANCES = 256;
static const DXGI_FORMAT s_FlatlandSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const std::wstring s_BackupModelPath = L"models\\Testing\\SphereTest.gltf";

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

	ShaderTable<LocalHitData> CreateModelShaderTable(const std::wstring& exportName, const InternalModel& internalModel, RaytracingPSO& rtPSO)
	{
		ASSERT(internalModel.modelPtr != nullptr);

		ShaderTable<LocalHitData> hitShaderTable = {};
		
		const Model& model = *internalModel.modelPtr;
		const Mesh* meshPtr = (const Mesh*)model.m_MeshData.get();
		void* shaderIdentifier = rtPSO.GetShaderIdentifier(exportName);
		for (int i = 0; i < (int)model.m_NumMeshes; i++)
		{
			const Mesh& mesh = meshPtr[i];
			ASSERT(mesh.numDraws == 1);

			auto& entry = hitShaderTable.emplace_back();
			entry.entryData.materialSRVs		= Renderer::s_TextureHeap[mesh.srvTable]; // Start of descriptor table.
			entry.entryData.geometrySRV			= internalModel.geometryDataSRVHandle;
			entry.entryData.indexByteOffset		= mesh.ibOffset;
			entry.entryData.vertexByteOffset	= mesh.vbOffset;

			entry.SetShaderIdentifier(shaderIdentifier);
		}

		return hitShaderTable;
	}
}

RadianceCascades::RadianceCascades()
	: m_mainViewport({}), m_mainScissor({})
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
	InitializePSOs();
	InitializeRCResources();

	InitializeRT();

	{
		DepthBuffer& sceneDepthBuff = Graphics::g_SceneDepthBuffer;

		m_minMaxDepthCopy.Create(L"Min Max Depth Copy", sceneDepthBuff.GetWidth(), sceneDepthBuff.GetHeight(), 1, DXGI_FORMAT_R32_FLOAT);
		m_minMaxDepthMips.Create(L"Min Max Depth Mips", sceneDepthBuff.GetWidth() / 2, sceneDepthBuff.GetHeight() / 2, 0, DXGI_FORMAT_R32G32_FLOAT);
	}
}

void RadianceCascades::Cleanup()
{
	Graphics::g_CommandManager.IdleGPU();

	// TODO: It might make more sense for an outer scope to execute this as this class is not responsible for their initialization.
	DebugDrawer::Destroy();
	RuntimeResourceManager::Destroy();

	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
{
	RuntimeResourceManager::UpdateGraphicsPSOs();

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
}

void RadianceCascades::RenderScene()
{
	ClearPixelBuffers();

	if (m_settings.globalSettings.renderRaster)
	{
		RenderSceneImpl(m_camera, m_mainViewport, m_mainScissor);
		RunMinMaxDepth();
	}

	if (m_settings.globalSettings.renderRaytracing)
	{
		RenderRaytracing(m_camera);
	}

	if (m_settings.globalSettings.renderDebug)
	{
		DebugRenderCameraInfo camInfo;
		camInfo.viewProjMatrix = m_camera.GetViewProjMatrix();
		DebugDrawer::Draw(camInfo, Graphics::g_SceneColorBuffer, m_mainViewport, m_mainScissor);
	}

	if (m_settings.rcSettings.visualize2DCascades)
	{
		RunComputeFlatlandScene();
		RunComputeRCGather();
		RunComputeRCMerge();
		RunComputeRCRadianceField(Graphics::g_SceneColorBuffer);

		static int currentCascadeVisIndex = -1;
		for (int i = 0; i < (int)m_rcManager2D.GetCascadeCount(); i++)
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
			FullScreenCopyCompute(m_rcManager2D.GetCascadeInterval(currentCascadeVisIndex), Graphics::g_SceneColorBuffer);
		}
	}
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	{
		ModelInstance& modelInstance = AddModelInstance(ModelIDSponza);
		modelInstance.Resize(100.0f * modelInstance.GetRadius());
	}

	{
		//ModelInstance& modelInstance = AddModelInstance(ModelIDSponza);
		//modelInstance.Resize(10.0f * modelInstance.GetRadius());
		//
		//Math::Vector3 instanceCenter = GetMainSceneModelCenter();
		//instanceCenter.SetY(20.0f);
		//modelInstance.GetTransform().SetTranslation(instanceCenter);
	}

	{
		ModelInstance& modelInstance = AddModelInstance(ModelIDSphereTest);
		modelInstance.Resize(100.0f * modelInstance.GetRadius());
		modelInstance.GetTransform().SetTranslation(GetMainSceneModelCenter());
	}

	// Setup camera
	{
		m_camera.SetAspectRatio((float)GetSceneColorHeight() / (float)GetSceneColorWidth());

		OrientedBox obb = GetMainSceneModelInstance().GetBoundingBox();
		float modelRadius = Length(obb.GetDimensions()) * 0.5f;
		const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
		m_camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));
		m_camera.SetZRange(0.5f, 5000.0f);
		
		
		m_cameraController.reset(new FlyingFPSCamera(m_camera, Vector3(kYUnitVector)));
	}
}

void RadianceCascades::InitializePSOs()
{
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
	}

	// Bind shader ids to specific PSOs for recompilation purposes.
	{
		// External PSO dependencies. These were found to be the PSOs that are used by opaque objects.
		std::vector<uint32_t> externalPSOs = { PSOIDFirstExternalPSO, PSOIDSecondExternalPSO };
		RuntimeResourceManager::AddShaderDependency(ShaderIDSceneRenderPS, externalPSOs);
		RuntimeResourceManager::AddShaderDependency(ShaderIDSceneRenderVS, externalPSOs);

		// This is a hack to rebuild the external PSOs instantly.
		// TODO: Make it so this hack does not have to be done :)
		{
			ShaderCompilationManager::Get().AddRecentReCompilation(ShaderIDSceneRenderPS);
			ShaderCompilationManager::Get().AddRecentReCompilation(ShaderIDSceneRenderVS);
		}
		

		// Internal PSO dependencies.
		RuntimeResourceManager::AddShaderDependency(ShaderIDRCGatherCS, { PSOIDComputeRCGatherPSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDFlatlandSceneCS, { PSOIDComputeFlatlandScenePSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDFullScreenCopyCS, { PSOIDComputeFullScreenCopyPSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDRCMergeCS, { PSOIDComputeRCMergePSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDRCRadianceFieldCS, { PSOIDComputeRCRadianceFieldPSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDRaytracingTestRT, { PSOIDRaytracingTestPSO });
		RuntimeResourceManager::AddShaderDependency(ShaderIDMinMaxDepthCS, { PSOIDComputeMinMaxDepthPSO });
	}

	{
		ComputePSO& pso = m_rcGatherPSO;
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDRCGatherCS));

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
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDFullScreenCopyCS));

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
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDFlatlandSceneCS));

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
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDRCMergeCS));

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
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDRCRadianceFieldCS));
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
		ComputePSO& pso = m_minMaxDepthPSO;
		pso.SetComputeShader(RuntimeResourceManager::GetShader(ShaderIDMinMaxDepthCS));

		RootSignature& rootSig = m_minMaxDepthrootSig;
		rootSig.Reset(RootEntryMinMaxDepthCount);
		rootSig[RootEntryMinMaxDepthSourceInfo].InitAsConstantBuffer(0);
		rootSig[RootEntryMinMaxDepthSourceDepthUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
		rootSig[RootEntryMinMaxDepthTargetDepthUAV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		rootSig.Finalize(L"Min Max Depth");

		pso.SetRootSignature(rootSig);
		pso.Finalize();
	}
}

void RadianceCascades::InitializeRCResources()
{
	m_flatlandScene.Create(L"Flatland Scene", ::GetSceneColorWidth(), ::GetSceneColorHeight(), 1, s_FlatlandSceneFormat);

	float diag = Math::Length({ (float)::GetSceneColorWidth(), (float)::GetSceneColorHeight(), 0.0f });
	m_rcManager2D.Init(SAMPLE_LEN_0, RAYS_PER_PROBE_0, diag);
}

void RadianceCascades::InitializeRT()
{
	RaytracingPSO& pso = m_rtTestPSO;
	{
		RootSignature1& globalRootSig = m_rtTestGlobalRootSig;
		globalRootSig.Reset(RootEntryRTGCount, 1, true);
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

		D3D12_SHADER_BYTECODE byteCode = RuntimeResourceManager::GetShader(ShaderIDRaytracingTestRT);

		pso.SetDxilLibrary(s_DXILExports, byteCode);
		pso.SetPayloadAndAttributeSize(4, 8);

		pso.SetHitGroup(s_HitGroupName, D3D12_HIT_GROUP_TYPE_TRIANGLES);
		pso.SetClosestHitShader(L"ClosestHitShader");
		
		pso.SetMaxRayRecursionDepth(1);

		pso.Finalize();
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
			instanceGroup.blasBuffer = &RuntimeResourceManager::GetModelBLAS(key);
			instanceGroup.instanceTransforms = value;

			instanceGroups.push_back(instanceGroup);
		}

		m_sceneModelTLASInstance.Init(instanceGroups);
	}

	// Initialize RT dispatch inputs.
	{
		std::set<ModelID> modelIDs = {};
		for (auto& [key, _] : blasInstances)
		{
			modelIDs.insert(key);
		}

		RuntimeResourceManager::BuildRaytracingDispatchInputs(PSOIDRaytracingTestPSO, modelIDs, RayDispatchIDTest);
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

#if defined(_DEBUGDRAWING)
		DebugDrawer::BindDebugBuffers(gfxContext, Renderer::kNumRootBindings);
#endif

		meshSorter.RenderMeshes(Renderer::MeshSorter::kOpaque, gfxContext, globals);

		gfxContext.Finish(true);
	}
}

void RadianceCascades::RenderRaytracing(Camera& camera)
{
	// TODO: Make any target viable. This would require a more sophisticated descriptor copy methodology.
	ColorBuffer& colorTarget = Graphics::g_SceneColorBuffer;
	DescriptorHandle colorDescriptorHandle = RuntimeResourceManager::GetSceneColorDescHandle();

	RTParams rtParams = {};
	rtParams.dispatchHeight = colorTarget.GetHeight();
	rtParams.dispatchWidth = colorTarget.GetWidth();
	rtParams.rayFlags = D3D12_RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
	rtParams.holeSize = 0.0f;

	GlobalInfo rtGlobalInfo = {};
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

	ID3D12DescriptorHeap* pDescriptorHeaps[] = { RuntimeResourceManager::GetDescriptorHeapPtr() };
	rtCommandList->SetDescriptorHeaps(1, pDescriptorHeaps);
	rtCommandList->SetComputeRootSignature(m_rtTestGlobalRootSig.GetSignature());
	rtCommandList->SetComputeRootShaderResourceView(RootEntryRTGSRV, m_sceneModelTLASInstance.GetBVH());
	rtCommandList->SetComputeRootDescriptorTable(RootEntryRTGUAV, colorDescriptorHandle);
	cmptContext.SetDynamicConstantBufferView(RootEntryRTGParamCB, sizeof(RTParams), &rtParams);
	cmptContext.SetDynamicConstantBufferView(RootEntryRTGInfoCB, sizeof(GlobalInfo), &rtGlobalInfo);
	DebugDrawer::BindDebugBuffers(cmptContext, RootEntryRTGCount);

	RaytracingDispatchRayInputs& rayDispatch = RuntimeResourceManager::GetRaytracingDispatch(RayDispatchIDTest);
	D3D12_DISPATCH_RAYS_DESC rayDispatchDesc = rayDispatch.BuildDispatchRaysDesc(colorTarget.GetWidth(), colorTarget.GetHeight());
	rtCommandList->SetPipelineState1(m_rtTestPSO.GetStateObject().Get());
	rtCommandList->DispatchRays(&rayDispatchDesc);

	cmptContext.Finish(true);
}

void RadianceCascades::RunMinMaxDepth()
{
	DepthBuffer& inputDepthBuffer = Graphics::g_SceneDepthBuffer;
	ColorBuffer& minMaxDepthCopy = m_minMaxDepthCopy;
	ColorBuffer& minMaxMipMaps = m_minMaxDepthMips;

	ComputeContext& cmptContext = ComputeContext::Begin(L"Min Max Depth");

	const ComputePSO& cmptPSO = RuntimeResourceManager::GetComputePSO(PSOIDComputeMinMaxDepthPSO);
	cmptContext.SetPipelineState(cmptPSO);
	cmptContext.SetRootSignature(cmptPSO.GetRootSignature());

	// Copy the depth buffer to a color buffer that can be read from as a UAV.
	{
		cmptContext.TransitionResource(inputDepthBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
		cmptContext.TransitionResource(minMaxDepthCopy, D3D12_RESOURCE_STATE_COPY_DEST);
		cmptContext.CopySubresource(minMaxDepthCopy, 0, inputDepthBuffer, 0);
	}
	
	cmptContext.TransitionResource(minMaxMipMaps, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.TransitionResource(minMaxDepthCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

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

		cmptContext.Dispatch2D(depthSourceInfo.sourceWidth >> 1, depthSourceInfo.sourceHeight >> 1);
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
	RCGlobals rcGlobals = m_rcManager2D.FillRCGlobalsData(sceneBuffer.GetWidth());

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

	RCGlobals rcGlobals = {};
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

void RadianceCascades::RunComputeRCRadianceField(ColorBuffer& outputBuffer)
{
	ComputeContext& cmptContext = ComputeContext::Begin(L"RC Radiance Field Compute");
	cmptContext.SetRootSignature(m_rcRadianceFieldRootSig);
	cmptContext.SetPipelineState(m_rcRadianceFieldPSO);

	ColorBuffer& radianceField = m_rcManager2D.GetRadianceField();
	ColorBuffer& targetCascade = m_rcManager2D.GetCascadeInterval(0);

	RCGlobals rcGlobals = m_rcManager2D.FillRCGlobalsData(targetCascade.GetWidth());
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
	
	m_rcManager2D.ClearBuffers(gfxContext);

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
	std::shared_ptr<Model> modelPtr = RuntimeResourceManager::GetModelPtr(modelID);

	if (modelPtr == nullptr)
	{
		modelPtr = Renderer::LoadModel(s_BackupModelPath, false);
		LOG_ERROR(L"Model was invalid. Using backup model instead. If Sponza model is missing, download a Sponza PBR gltf model online.");
	}

	return m_sceneModels.emplace_back(modelPtr, modelID);
}
