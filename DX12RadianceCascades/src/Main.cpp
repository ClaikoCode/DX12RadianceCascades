#include "rcpch.h"
#include "D3D12RadianceCascades.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
	Logging::Initialize(true, L"logs.txt");

	D3D12RadianceCascades radianceCascadesSample(1280, 720, L"D3D12 Radiance Cascades - Master Thesis Project");
	return Win32Application::Run(&radianceCascadesSample, hInstance, nCmdShow);

	Logging::Shutdown();
}