#include "rcpch.h"
#include "D3D12RadianceCascades.h"
#include "DirectXRaytracingHelper.h"
#include "D3D12RadianceCascades.h"



constexpr DXGI_FORMAT c_DefaultBBFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr D3D_FEATURE_LEVEL c_DefaultFeatureLevel = D3D_FEATURE_LEVEL_12_0;
constexpr uint32_t c_BackBufferCount = 2u;

#define OUT_STR(message) message L"\n\n"
#define ERR_STR(message) L"ERROR: " OUT_STR(message)
#define DBG_STR(message) L"DEBUG: " OUT_STR(message)

D3D12RadianceCascades::D3D12RadianceCascades(UINT width, UINT height, std::wstring name)
	: DXSample(width, height, name)
{
	// Empty
}

void D3D12RadianceCascades::OnDeviceLost()
{

}

void D3D12RadianceCascades::OnDeviceRestored()
{

}

void D3D12RadianceCascades::OnInit()
{
	InitDeviceResources();

}

void D3D12RadianceCascades::OnUpdate()
{
}

void D3D12RadianceCascades::OnRender()
{
}

void D3D12RadianceCascades::OnSizeChanged(UINT width, UINT height, bool minimized)
{
	if (!m_deviceResources->WindowSizeChanged(width, height, minimized))
	{
		return; 
	}

	DXSample::UpdateForSizeChange(width, height);

	CreateWindowDependentResources();
}

void D3D12RadianceCascades::OnDestroy()
{
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

	ThrowIfFalse(IsDirectXRaytracingSupported(m_deviceResources->GetAdapter()), ERR_STR(L"Raytracing is not supported on your current hardware / drivers."));

	m_deviceResources->CreateDeviceResources();
	m_deviceResources->CreateWindowSizeDependentResources();
}

void D3D12RadianceCascades::CreateWindowDependentResources()
{
}
