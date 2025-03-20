#pragma once

#include "Core\GameCore.h"
#include "Core\Camera.h"
#include "Core\CameraController.h"
#include "Model\Model.h"

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

private:

	Camera m_camera;
	std::unique_ptr<CameraController> m_cameraController;

	ModelInstance m_sceneModelInstance;
};