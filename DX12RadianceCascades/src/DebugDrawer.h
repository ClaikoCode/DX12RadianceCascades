#pragma once

#include "Core\ReadbackBuffer.h"

#define DEBUGDRAW_MAX_LINES 2048 * 2048

struct DebugRenderCameraInfo;
class ColorBuffer;

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

	static void Draw(DebugRenderCameraInfo& cameraInfo, ColorBuffer& target, D3D12_VIEWPORT viewPort, D3D12_RECT scissor)
	{
		Get().DrawImpl(cameraInfo, target, viewPort, scissor);
	}

	static void Destroy()
	{
		Get().DestroyImpl();
	}

	static void BindDebugBuffers(GraphicsContext& gfxContext, UINT startRootIndex)
	{
		Get().BindDebugBuffersImpl(gfxContext, startRootIndex);
	}

	static void BindDebugBuffers(ComputeContext& cmptContext, UINT startRootIndex)
	{
		Get().BindDebugBuffersImpl(cmptContext, startRootIndex);
	}

private:
	DebugDrawer();

	void DrawImpl(DebugRenderCameraInfo& cameraInfo, ColorBuffer& target, D3D12_VIEWPORT viewPort, D3D12_RECT scissor);
	void DestroyImpl();
	void BindDebugBuffersImpl(GraphicsContext& gfxContext, UINT startRootIndex);
	void BindDebugBuffersImpl(ComputeContext& cmptContext, UINT startRootIndex);

private:

	GraphicsPSO m_debugDrawPSO;
	std::shared_ptr<RootSignature> m_debugDrawRootSig;
	StructuredBuffer m_lineStructBuffer;
	ByteAddressBuffer m_cameraBuffer;
	ReadbackBuffer m_countReadbackBuffer;
};