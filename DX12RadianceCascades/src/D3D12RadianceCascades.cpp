#include "rcpch.h"
#include "D3D12RadianceCascades.h"
#include "DirectXRaytracingHelper.h"

D3D12RadianceCascades::D3D12RadianceCascades(UINT width, UINT height, std::wstring name)
	: DXSample(width, height, name)
{
	m_deviceResources = std::make_unique<DX::DeviceResources>(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_UNKNOWN,
		2,
		D3D_FEATURE_LEVEL_12_0
	);

	m_deviceResources->SetWindow(Win32Application::GetHwnd(), width, height);
	m_deviceResources->InitializeDXGIAdapter();

	bool rtSupported = IsDirectXRaytracingSupported(m_deviceResources->GetAdapter());
}

void D3D12RadianceCascades::OnDeviceLost()
{

}

void D3D12RadianceCascades::OnDeviceRestored()
{

}

void D3D12RadianceCascades::OnInit()
{

}

void D3D12RadianceCascades::OnUpdate()
{
}

void D3D12RadianceCascades::OnRender()
{
}

void D3D12RadianceCascades::OnSizeChanged(UINT width, UINT height, bool minimized)
{
}

void D3D12RadianceCascades::OnDestroy()
{
}
