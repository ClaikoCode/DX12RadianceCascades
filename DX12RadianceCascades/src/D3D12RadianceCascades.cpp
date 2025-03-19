#include "rcpch.h"

#include "Core\CommandListManager.h" // Included as the global variable was not accessible otherwise due to not having the definition.
#include "Core\CommandContext.h"
#include "Core\GraphicsCore.h"
#include "Core\GpuTimeManager.h"

#include "DirectXRaytracingHelper.h"
#include "ShaderCompilation\ShaderCompilationManager.h"
#include "D3D12RadianceCascades.h"

D3D12RadianceCascades::D3D12RadianceCascades(UINT width, UINT height, std::wstring name)
	: DXSample(width, height, name)
{
	// Empty
}

void D3D12RadianceCascades::OnDeviceLost()
{
	Graphics::g_CommandManager.IdleGPU();
	Graphics::g_CommandManager.Shutdown();
	GpuTimeManager::Shutdown();

	// Uncommented until a need is noticed to clear like this.
	//PSO::DestroyAll();
	//RootSignature::DestroyAll();
	//DescriptorAllocator::DestroyAll();
}

void D3D12RadianceCascades::OnDeviceRestored()
{
	// Empty
}

void D3D12RadianceCascades::OnInit()
{
	InitDeviceResources();

	m_sceneColorBuffer.Create(L"Scene Color Buffer", m_width, m_height, 1, DXGI_FORMAT_R8G8_UINT);
	m_sceneDepthBuffer.Create(L"Scene Depth Buffer", m_width, m_height, DXGI_FORMAT_D32_FLOAT);

	CreateWindowDependentResources();

	auto& shaderCompManager = ShaderCompilationManager::Get();
	shaderCompManager.RegisterShader(Shader::ShaderIDTest, L"VertexShaderTest.hlsl", Shader::ShaderTypeVS, true);

	GraphicsPSO testPipeline(L"TestPSO");
	void* binaryPtr = nullptr;
	size_t binarySize = 0;
	shaderCompManager.GetCompiledShaderData(Shader::ShaderIDTest, &binaryPtr, &binarySize);

	testPipeline.SetVertexShader(binaryPtr, binarySize);
}

void D3D12RadianceCascades::OnUpdate()
{

}

void D3D12RadianceCascades::OnRender()
{
	Prepare();

	Present();
}

void D3D12RadianceCascades::OnSizeChanged(UINT width, UINT height, bool minimized)
{
	// Free swapchain buffers.
	for (uint32_t i = 0; i < c_BackBufferCount; i++)
	{
		m_renderTargets[i].Destroy();
	}

	// Return value is ignore as this needs to run every time either way. 
	// This is because of the weird double graphics APIs this project is using.
	m_deviceResources->WindowSizeChanged(width, height, minimized);

	Graphics::g_CommandManager.IdleGPU();
	DXSample::UpdateForSizeChange(width, height);
	CreateWindowDependentResources();
}

void D3D12RadianceCascades::OnDestroy()
{
	m_deviceResources->WaitForGpu();
	OnDeviceLost();
}

void D3D12RadianceCascades::Prepare()
{
	m_deviceResources->Prepare();
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Prepare Back Buffer");
	
	ColorBuffer& renderTarget = GetCurrentBackBuffer();
	gfxContext.TransitionResource(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.ClearColor(renderTarget);

	gfxContext.Finish();
}

void D3D12RadianceCascades::Present()
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Present Back Buffer");

	gfxContext.TransitionResource(GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);

	gfxContext.Finish();
	m_deviceResources->Present();
}

void D3D12RadianceCascades::InitDeviceResources()
{
	m_deviceResources = std::make_unique<DX::DeviceResources>(
		c_DefaultBBFormat,
		DXGI_FORMAT_UNKNOWN,
		c_BackBufferCount,
		c_DefaultFeatureLevel
	);

	m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
	m_deviceResources->InitializeDXGIAdapter();

	ThrowIfFalse(IsDirectXRaytracingSupported(m_deviceResources->GetAdapter()), L"Raytracing is not supported on your current hardware / drivers.");

	m_deviceResources->CreateDeviceResources();
	
	// Manual setup of necessary global resources.
	Graphics::g_Device = m_deviceResources->GetD3DDevice();
	Graphics::g_CommandManager.Create(m_deviceResources->GetD3DDevice());
	GpuTimeManager::Initialize();

	// Override the command queue so that creation of swapchain is tied to Graphics:: global managers.
	m_deviceResources->OverrideCommandQueue(Graphics::g_CommandManager.GetQueue().GetCommandQueue());
	m_deviceResources->CreateWindowSizeDependentResources();
}

void D3D12RadianceCascades::CreateWindowDependentResources()
{
	for (uint32_t i = 0; i < c_BackBufferCount; i++)
	{
		ComPtr<ID3D12Resource> bbResource = nullptr;
		GetSwapchain()->GetBuffer(i, IID_PPV_ARGS(bbResource.GetAddressOf()));

		std::wstring bbName = std::wstring(L"Back Buffer ") + std::to_wstring(i);
		m_renderTargets[i].CreateFromSwapChain(bbName, bbResource.Detach());
		m_renderTargets[i].SetClearColor(c_BackBufferClearColor);
	}
}

ColorBuffer& D3D12RadianceCascades::GetCurrentBackBuffer()
{
	return m_renderTargets[m_deviceResources->GetCurrentFrameIndex()];
}
