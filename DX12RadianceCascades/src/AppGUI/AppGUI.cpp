#include "rcpch.h"

#include "Core\DescriptorHeap.h"
#include "Core\GraphicsCore.h"
#include "Core\BufferManager.h"
#include "Core\CommandContext.h"

#include "AppGUI\imgui_impl_win32.h"
#include "AppGUI\imgui_impl_dx12.h"

#include "AppGUI.h"

namespace Graphics { extern IDXGISwapChain1* s_SwapChain1; }

namespace AppGUI
{
	DescriptorHeap g_UIDescHeap;
}

namespace AppGUI
{
	void PreInit()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		{
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.FontGlobalScale = 1.5f;
		}

		ImGui::StyleColorsDark();
	}

	void Initialize(HWND hwnd)
	{
		// Create desc heap for imgui.
		g_UIDescHeap.Create(L"ImGui Desc Heap", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);

		// Init win32 impl.
		ImGui_ImplWin32_Init(hwnd);

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		ThrowIfFailed(Graphics::s_SwapChain1->GetDesc1(&swapChainDesc));

		ImGui_ImplDX12_Init(
			Graphics::g_Device,
			swapChainDesc.BufferCount,
			Graphics::g_OverlayBuffer.GetFormat(),
			g_UIDescHeap.GetHeapPointer(),
			(D3D12_CPU_DESCRIPTOR_HANDLE)g_UIDescHeap[0],
			(D3D12_GPU_DESCRIPTOR_HANDLE)g_UIDescHeap[0]
		);
	}

	void NewFrame()
	{
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void AppGUI::Render(GraphicsContext& uiContext)
	{
		// Set descriptor heaps for UI textures.
		uiContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, g_UIDescHeap.GetHeapPointer());

		// Fill draw data.
		ImGui::Render();

		// Render the UI.
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), uiContext.GetCommandList());
	}

	void Shutdown()
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();

		g_UIDescHeap.Destroy();

		ImGui::DestroyContext();
	}
}


