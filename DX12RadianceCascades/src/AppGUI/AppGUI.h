#pragma once

#include "imgui.h"

namespace AppGUI
{
    // Top level initialization in main.cpp
    void PreInit();
	void Initialize(HWND hwnd);
    void NewFrame();
    void Render(GraphicsContext& uiContext);
    void SetFontScale(float fontScale);
    void Shutdown();
}

