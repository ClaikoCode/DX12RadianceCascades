#include "rcpch.h"
#include "Core\CommandContext.h"
#include "DebugDrawer.h"

DebugDrawer::DebugDrawer()
{
	m_lineStructBuffer.Create(L"Debug Drawer Line Buffer", DEBUGDRAW_MAX_LINES * 2, sizeof(DebugRenderVertex));
}