#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"
#include "Core\ColorBuffer.h"
#include "Core\DepthBuffer.h"

constexpr DXGI_FORMAT c_DefaultBBFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr D3D_FEATURE_LEVEL c_DefaultFeatureLevel = D3D_FEATURE_LEVEL_12_0;
constexpr uint32_t c_BackBufferCount = 2u;
static const Color c_BackBufferClearColor = Color(1.0f, 0.0f, 1.0f, 1.0f);

class D3D12RadianceCascades : public DXSample
{
public:
    D3D12RadianceCascades(UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // DXSample
    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
    virtual void OnDestroy();
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }

private:
    // Prepare back buffer for rendering.
    void Prepare();
    // Present back buffer.
    void Present();

    void InitDeviceResources();
    void CreateWindowDependentResources();

    ColorBuffer& GetCurrentBackBuffer();
  

private:
    ColorBuffer m_renderTargets[c_BackBufferCount];
    ColorBuffer m_sceneColorBuffer;
    DepthBuffer m_sceneDepthBuffer;
};