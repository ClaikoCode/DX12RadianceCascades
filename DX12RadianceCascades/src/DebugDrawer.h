#pragma once

#define DEBUGDRAW_MAX_LINES 4096

struct DebugRenderVertex
{
	float position[3];
	float color[3];
};

class DebugDrawer
{
public:

	static DebugDrawer& Get()
	{
		static DebugDrawer instance = DebugDrawer();

		return instance;
	}

	static StructuredBuffer& GetLineBuffer()
	{
		return Get().m_lineStructBuffer;
	}

	static ByteAddressBuffer& GetCounterBuffer()
	{
		return GetLineBuffer().GetCounterBuffer();
	}

private:
	DebugDrawer();

private:

	GraphicsPSO m_debugDrawPSO;
	StructuredBuffer m_lineStructBuffer;
};