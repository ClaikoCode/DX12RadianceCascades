#include "rcpch.h"

#include "Model\Renderer.h"
#include "Model\ModelLoader.h"

#include "Core\BufferManager.h"

#include "ShaderCompilation\ShaderCompilationManager.h"

#include "RadianceCascades.h"

RadianceCascades::RadianceCascades()
{
	
}

RadianceCascades::~RadianceCascades()
{
}

void RadianceCascades::Startup()
{
	Renderer::Initialize();

	InitializeScene();
	InitializeShaders();
	
}

void RadianceCascades::Cleanup()
{
	m_sceneModelInstance = nullptr; // Destroys the scene.
	Renderer::Shutdown();
}

void RadianceCascades::Update(float deltaT)
{

}

void RadianceCascades::RenderScene()
{
	
}

void RadianceCascades::InitializeScene()
{
	// Setup scene.
	{
		m_sceneModelInstance = Renderer::LoadModel(L"assets\\Sponza\\PBR\\sponza2.gltf", false);
		m_sceneModelInstance.Resize(100.0f * m_sceneModelInstance.GetRadius());

		OrientedBox obb = m_sceneModelInstance.GetBoundingBox();
		float modelRadius = Length(obb.GetDimensions()) * 0.5f;
		const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
		m_camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));
	}

	m_cameraController.reset(new FlyingFPSCamera(m_camera, Vector3(kYUnitVector)));
}

void RadianceCascades::InitializeShaders()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	shaderCompManager.RegisterShader(Shader::ShaderIDTest, L"VertexShaderTest.hlsl", Shader::ShaderTypeVS, true);
}
