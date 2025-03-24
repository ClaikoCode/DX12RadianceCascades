#pragma once

#include "Core\GameCore.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"

enum ShaderID : UUID64
{
	ShaderIDInvalid = 0,
	ShaderIDSceneRenderPS,
	ShaderIDSceneRenderVS,

	ShaderIDNone = NULL_ID
};

class RadianceCascades : public GameCore::IGameApp
{
public:
	RadianceCascades();
	~RadianceCascades();

	virtual void Startup() override;
	virtual void Cleanup() override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene() override;

	virtual bool RequiresRaytracingSupport() const override { return true; }

private:

	void InitializeScene();
	void InitializeShaders();
	void InitializePSOs();

	void RenderSceneImpl(Camera& camera, D3D12_VIEWPORT viewPort, D3D12_RECT scissor);
	void UpdateViewportAndScissor();
	void UpdatePSOs();

	ModelInstance& AddModelInstance(std::shared_ptr<Model> modelPtr);

private:

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

	uint32_t m_mainSceneIndex;
	std::vector<ModelInstance> m_sceneModels;

	D3D12_VIEWPORT m_mainViewport;
	D3D12_RECT m_mainScissor;

	std::unordered_map<UUID64, std::set<uint16_t>> m_shaderPSOMap;
};