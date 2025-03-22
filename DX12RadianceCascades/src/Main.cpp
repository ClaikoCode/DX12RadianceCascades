#include "rcpch.h"

#include "Core\PostEffects.h"
#include "Core\MotionBlur.h"
#include "Core\TemporalEffects.h"
#include "Core\FXAA.h"
#include "Core\SSAO.h"

#include "RadianceCascades.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow) 
{
    Logging::Initialize(false, L"runtime_logs.txt");

    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = false;
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = false;
    SSAO::Enable = false;


    RadianceCascades radianceCascades = RadianceCascades();
    return GameCore::RunApplication(radianceCascades, L"RadianceCascades", hInstance, nCmdShow);
};