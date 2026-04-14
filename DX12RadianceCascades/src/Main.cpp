#include "rcpch.h"

#include "Core\PostEffects.h"
#include "Core\MotionBlur.h"
#include "Core\TemporalEffects.h"
#include "Core\FXAA.h"
#include "Core\SSAO.h"

#include "AppGUI\AppGUI.h"

#include "RadianceCascades.h"

#if defined(_DEBUG)
#define CONFIG_NAME L"Debug"
#else
#define CONFIG_NAME L"Release"
#endif

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
    Logging::Initialize(false, L"runtime_logs.txt");
    AppGUI::PreInit();

    // Disable unwanted effects.
    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = false;
    PostEffects::EnableAdaptation = false;
    PostEffects::BloomEnable = false;
    SSAO::Enable = false;

    int returnVal = 0;
    {
        RadianceCascades radianceCascades = RadianceCascades();

        const wchar_t* appName = L"DX12 Radiance Cascades (" CONFIG_NAME L")";
        returnVal = GameCore::RunApplication(radianceCascades, appName, hInstance, nCmdShow);
    }
    
    Graphics::Shutdown();
    return returnVal;
};