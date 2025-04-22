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

struct IndirectCommand
{
	D3D12_GPU_VIRTUAL_ADDRESS cameraCBV;
	D3D12_DRAW_ARGUMENTS drawArgs;
};

DebugDrawer::DebugDrawer() : m_debugDrawPSO(L"Debug Draw PSO"), m_debugDrawRootSig(std::make_shared<RootSignature>())
{
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

	// Creating PSO
	{
		GraphicsPSO& pso = m_debugDrawPSO;
		pso.SetRootSignature(rootSig);
		pso.SetVertexShader(RuntimeResourceManager::GetShader(ShaderIDDebugDrawVS));
		pso.SetPixelShader(RuntimeResourceManager::GetShader(ShaderIDDebugDrawPS));
		pso.SetRasterizerState(Graphics::RasterizerTwoSided);
		pso.SetBlendState(Graphics::BlendAdditive);
		pso.SetDepthStencilState(Graphics::DepthStateReadOnly);
		pso.SetInputLayout((UINT)inputLayout.size(), inputLayout.data());
		pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
		pso.SetRenderTargetFormat(Graphics::g_SceneColorBuffer.GetFormat(), Graphics::g_SceneDepthBuffer.GetFormat());
		
		pso.Finalize();
	}

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Debug Draw Initial Transitions");
	
	gfxContext.TransitionResource(m_lineStructBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(m_lineStructBuffer.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	gfxContext.Finish(true);

	RuntimeResourceManager::RegisterPSO(PSOIDDebugDrawPSO, &m_debugDrawPSO, PSOTypeGraphics);
	RuntimeResourceManager::AddShaderDependency(ShaderIDDebugDrawPS, { PSOIDDebugDrawPSO });
	RuntimeResourceManager::AddShaderDependency(ShaderIDDebugDrawVS, { PSOIDDebugDrawPSO });
}

void DebugDrawer::DrawImpl(DebugRenderCameraInfo& cameraInfo, ColorBuffer& target, D3D12_VIEWPORT viewPort, D3D12_RECT scissor)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Debug Draw Context");

	uint32_t count = 0;
	{
		gfxContext.CopyCounter(m_countReadbackBuffer, 0, m_lineStructBuffer);
		void* countReadBack = m_countReadbackBuffer.Map();
		memcpy(&count, countReadBack, sizeof(count));
		m_countReadbackBuffer.Unmap();
	}

	if (count > DEBUGDRAW_MAX_LINES)
	{
		count = DEBUGDRAW_MAX_LINES;
	}

	gfxContext.TransitionResource(m_cameraBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	gfxContext.TransitionResource(target, D3D12_RESOURCE_STATE_RENDER_TARGET);
	gfxContext.TransitionResource(Graphics::g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

	D3D12_VERTEX_BUFFER_VIEW vbView = {};
	vbView.BufferLocation = m_lineStructBuffer.GetGpuVirtualAddress();
	vbView.SizeInBytes = (UINT)m_lineStructBuffer.GetBufferSize();
	vbView.StrideInBytes = m_lineStructBuffer.GetElementSize();

	gfxContext.SetPipelineState(m_debugDrawPSO);
	gfxContext.SetRootSignature(*m_debugDrawRootSig);
	gfxContext.SetVertexBuffer(0, vbView);
	gfxContext.SetViewportAndScissor(viewPort, scissor);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	gfxContext.SetRenderTarget(target.GetRTV(), Graphics::g_SceneDepthBuffer.GetDSV());
	gfxContext.SetDynamicConstantBufferView(0, sizeof(cameraInfo), &cameraInfo);
	gfxContext.Draw(count * 2);

	// Clear counter for next frame.
	gfxContext.ResetCounter(m_lineStructBuffer);

	gfxContext.Finish(true);
}

void DebugDrawer::DestroyImpl()
{
	Graphics::g_CommandManager.IdleGPU();

	m_debugDrawPSO.DestroyAll();
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
