#include "rcpch.h"
#include "Core\CommandContext.h"
#include "Core\BufferManager.h"
#include "Core\CommandSignature.h"
#include "Core\RootSignature.h"
#include "Core\UploadBuffer.h"
#include "Core\ColorBuffer.h"
#include "RuntimeResourceManager.h"
#include "GPUStructs.h"
#include "DebugDrawer.h"

#if defined(_DEBUGDRAWING)

DebugDrawer::DebugDrawer() : 
	m_debugDrawNoDepthPSO(L"Debug Draw No Depth PSO"), 
	m_debugDrawDepthPSO(L"Debug Draw Depth PSO"), 
	m_debugDrawRootSig(std::make_shared<RootSignature>())
{
	RuntimeResourceManager::RegisterPSO(PSOIDDebugDrawNoDepthPSO, &m_debugDrawNoDepthPSO, PSOTypeGraphics);
	RuntimeResourceManager::RegisterPSO(PSOIDDebugDrawDepthPSO, &m_debugDrawDepthPSO, PSOTypeGraphics);

	m_lineStructBuffer.Create(L"Debug Drawer Line Buffer", DEBUGDRAW_MAX_LINES * 2, sizeof(DebugRenderVertex));
	m_cameraBuffer.Create(L"Debug Drawer Camera Buffer", 1, sizeof(DebugRenderCameraInfo));
	m_countReadbackBuffer.Create(L"Debug Drawer Count Readback", 1, sizeof(uint32_t));
	
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	RootSignature& rootSig = *m_debugDrawRootSig;
	rootSig.Reset(1, 0, false);
	rootSig[0].InitAsConstantBuffer(0); // Camera const buffer.
	rootSig.Finalize(L"Debug Draw Root Signature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Creating debug PSO without depth.
	{
		GraphicsPSO& pso = RuntimeResourceManager::GetGraphicsPSO(PSOIDDebugDrawNoDepthPSO);
		RuntimeResourceManager::SetShadersForPSO(PSOIDDebugDrawNoDepthPSO, { ShaderIDDebugDrawVS, ShaderIDDebugDrawPS });

		pso.SetRootSignature(rootSig);
		pso.SetRasterizerState(Graphics::RasterizerTwoSided);
		pso.SetBlendState(Graphics::BlendAdditive);

		pso.SetDepthStencilState(Graphics::DepthStateDisabled);
		pso.SetRenderTargetFormat(Graphics::g_SceneColorBuffer.GetFormat(), DXGI_FORMAT_UNKNOWN);

		pso.SetInputLayout((UINT)inputLayout.size(), inputLayout.data());
		pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);

		pso.Finalize();
	}

	// Creating debug PSO with depth.
	{
		GraphicsPSO& pso = RuntimeResourceManager::GetGraphicsPSO(PSOIDDebugDrawDepthPSO);
		// Same as above, but with depth.
		pso = { RuntimeResourceManager::GetGraphicsPSO(PSOIDDebugDrawNoDepthPSO) };

		RuntimeResourceManager::SetShadersForPSO(PSOIDDebugDrawDepthPSO, { ShaderIDDebugDrawVS, ShaderIDDebugDrawPS });
		pso.SetDepthStencilState(Graphics::DepthStateReadOnly);
		pso.SetRenderTargetFormat(Graphics::g_SceneColorBuffer.GetFormat(), Graphics::g_SceneDepthBuffer.GetFormat());

		pso.Finalize();
	}

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Debug Draw Initial Transitions");
	
	gfxContext.TransitionResource(m_lineStructBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(m_lineStructBuffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	gfxContext.Finish(true);
}

void DebugDrawer::DrawImpl(DebugRenderCameraInfo& cameraInfo, ColorBuffer& targetColor, DepthBuffer& targetDepth, D3D12_VIEWPORT viewPort, D3D12_RECT scissor, bool useDepthCheck)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Debug Draw Context");

	uint32_t lineCount = 0;
	{
		gfxContext.CopyCounter(m_countReadbackBuffer, 0, m_lineStructBuffer);
		void* countReadBack = m_countReadbackBuffer.Map();
		memcpy(&lineCount, countReadBack, sizeof(uint32_t));
		m_countReadbackBuffer.Unmap();
	}

	if (lineCount > DEBUGDRAW_MAX_LINES)
	{
		lineCount = DEBUGDRAW_MAX_LINES;
	}

	gfxContext.TransitionResource(m_cameraBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(targetColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.TransitionResource(targetDepth, D3D12_RESOURCE_STATE_DEPTH_READ, true);

	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = m_lineStructBuffer.GetGpuVirtualAddress();
	vbView.SizeInBytes = (UINT)m_lineStructBuffer.GetBufferSize();
	vbView.StrideInBytes = m_lineStructBuffer.GetElementSize();

	if (useDepthCheck)
	{
		gfxContext.SetPipelineState(RuntimeResourceManager::GetGraphicsPSO(PSOIDDebugDrawDepthPSO));
	}
	else
	{
		gfxContext.SetPipelineState(RuntimeResourceManager::GetGraphicsPSO(PSOIDDebugDrawNoDepthPSO));
	}

	gfxContext.SetRootSignature(*m_debugDrawRootSig);
	gfxContext.SetVertexBuffer(0, vbView);
	gfxContext.SetViewportAndScissor(viewPort, scissor);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	gfxContext.SetRenderTarget(targetColor.GetRTV(), targetDepth.GetDSV());
	gfxContext.SetDynamicConstantBufferView(0, sizeof(cameraInfo), &cameraInfo);
	gfxContext.Draw(lineCount * 2);

	// Clear counter for next frame.
	gfxContext.ResetCounter(m_lineStructBuffer);

	gfxContext.Finish(true);
}

void DebugDrawer::DestroyImpl()
{
	Graphics::g_CommandManager.IdleGPU();

	m_debugDrawNoDepthPSO.DestroyAll();
	m_debugDrawDepthPSO.DestroyAll();
	m_lineStructBuffer.Destroy();
	m_cameraBuffer.Destroy();
	m_countReadbackBuffer.Destroy();

	m_debugDrawRootSig = nullptr;
}

void DebugDrawer::BindDebugBuffersImpl(GraphicsContext& gfxContext, UINT startRootIndex)
{
	StructuredBuffer& lineStructBuff = DebugDrawer::GetLineBuffer();
	ByteAddressBuffer& counterBuffer = DebugDrawer::GetCounterBuffer();

	gfxContext.TransitionResource(lineStructBuff, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(counterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	gfxContext.SetBufferUAV(startRootIndex, lineStructBuff);
	gfxContext.SetBufferUAV(startRootIndex + 1, counterBuffer);
}

void DebugDrawer::BindDebugBuffersImpl(ComputeContext& cmptContext, UINT startRootIndex)
{
	StructuredBuffer& lineStructBuff = DebugDrawer::GetLineBuffer();
	ByteAddressBuffer& counterBuffer = DebugDrawer::GetCounterBuffer();

	cmptContext.TransitionResource(lineStructBuff, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmptContext.TransitionResource(counterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	cmptContext.SetBufferUAV(startRootIndex, lineStructBuff);
	cmptContext.SetBufferUAV(startRootIndex + 1, counterBuffer);
}

#endif // if defined(_DEBUGDRAWING)