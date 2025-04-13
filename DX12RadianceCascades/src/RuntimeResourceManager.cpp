#include "rcpch.h"
#include "Core\BufferManager.h"
#include "Core\CommandContext.h"
#include "ShaderCompilation\ShaderCompilationManager.h"
#include "RaytracingUtils.h"
#include "RuntimeResourceManager.h"

static const std::set<Shader::ShaderType> s_ValidShaderTypes = { Shader::ShaderTypeCS, Shader::ShaderTypeVS, Shader::ShaderTypePS, Shader::ShaderTypeRT };

void RuntimeResourceManager::UpdateGraphicsPSOs()
{
	RuntimeResourceManager::Get().UpdateGraphicsPSOsImpl();
}

void RuntimeResourceManager::AddShaderDependency(ShaderID shaderID, std::vector<PSOIDType> psoIDs)
{
	RuntimeResourceManager::Get().AddShaderDependencyImpl(shaderID, psoIDs);
}

void RuntimeResourceManager::RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType)
{
	RuntimeResourceManager::Get().RegisterPSOImpl(psoID, psoPtr, psoType);
}

D3D12_SHADER_BYTECODE RuntimeResourceManager::GetShader(ShaderID shaderID)
{
	return ShaderCompilationManager::Get().GetShaderByteCode(shaderID);
}

RuntimeResourceManager::RuntimeResourceManager()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	{
		// Pixel Shaders
		shaderCompManager.RegisterPixelShader(ShaderIDSceneRenderPS, L"SceneRenderPS.hlsl", true);
		shaderCompManager.RegisterPixelShader(ShaderIDFullScreenCopyPS, L"DirectWritePS.hlsl", true);

		// Vertex Shaders
		shaderCompManager.RegisterVertexShader(ShaderIDSceneRenderVS, L"SceneRenderVS.hlsl", true);
		shaderCompManager.RegisterVertexShader(ShaderIDFullScreenQuadVS, L"FullScreenQuadVS.hlsl", true);

		// Compute Shaders
		shaderCompManager.RegisterComputeShader(ShaderIDRCGatherCS, L"RCGatherCS.hlsl", true);
		shaderCompManager.RegisterComputeShader(ShaderIDFlatlandSceneCS, L"FlatlandSceneCS.hlsl", true);
		shaderCompManager.RegisterComputeShader(ShaderIDFullScreenCopyCS, L"DirectCopyCS.hlsl", true);
		shaderCompManager.RegisterComputeShader(ShaderIDRCMergeCS, L"RCMergeCS.hlsl", true);
		shaderCompManager.RegisterComputeShader(ShaderIDRCRadianceFieldCS, L"RCRadianceFieldCS.hlsl", true);

		// RT Shaders
		shaderCompManager.RegisterRaytracingShader(ShaderIDRaytracingTestRT, L"RaytracingTest.hlsl", true);
	}
}

void RuntimeResourceManager::UpdateGraphicsPSOsImpl()
{
	auto& shaderCompManager = ShaderCompilationManager::Get();

	if (shaderCompManager.HasRecentCompilations())
	{
		// Wait for all work to be done before changing PSOs.
		Graphics::g_CommandManager.IdleGPU();

		auto compSet = shaderCompManager.GetRecentCompilations();

		for (UUID64 shaderID : compSet)
		{
			Shader::ShaderType shaderType = shaderCompManager.GetShaderType(shaderID);

			if (s_ValidShaderTypes.find(shaderType) == s_ValidShaderTypes.end())
			{
				LOG_INFO(L"Invalid shader type: {}. Skipping.", (uint32_t)shaderType);
				continue;
			}

			D3D12_SHADER_BYTECODE shaderByteCode = shaderCompManager.GetShaderByteCode(shaderID);

			if (shaderByteCode.pShaderBytecode)
			{
				const auto& psoIds = m_shaderPSODependencyMap[shaderID];

				if (!psoIds.empty())
				{
					for (PSOIDType psoId : psoIds)
					{
						const PSOPackage& package = m_usedPSOs[psoId];

						if (package.psoType == PSOTypeCompute)
						{
							ComputePSO& pso = *(reinterpret_cast<ComputePSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								pso.SetComputeShader(shaderByteCode);
								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeGraphics)
						{
							GraphicsPSO& pso = *(reinterpret_cast<GraphicsPSO*>(package.PSOPointer));

							if (pso.GetPipelineStateObject() != nullptr)
							{
								if (shaderType == Shader::ShaderTypePS)
								{
									pso.SetPixelShader(shaderByteCode);
								}
								else if (shaderType == Shader::ShaderTypeVS)
								{
									pso.SetVertexShader(shaderByteCode);
								}

								pso.Finalize();
							}
						}
						else if (package.psoType == PSOTypeRaytracing)
						{
							//LOG_INFO(L"Raytracing PSO Reloading is not implemented yet.");
							//continue;

							RaytracingPSO& pso = *(reinterpret_cast<RaytracingPSO*>(package.PSOPointer));

							if (pso.GetStateObject() != nullptr)
							{
								pso.SetDxilLibrary(s_DXILExports, shaderByteCode);

								pso.Finalize();
							}
						}
					}
				}
			}
		}

		shaderCompManager.ClearRecentCompilations();
	}
}

void RuntimeResourceManager::AddShaderDependencyImpl(ShaderID shaderID, std::vector<PSOIDType>& psoIDs)
{
	for (uint32_t psoID : psoIDs)
	{
		m_shaderPSODependencyMap[shaderID].insert(psoID);
	}
}

void RuntimeResourceManager::RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType)
{
	m_usedPSOs[psoID] = { psoPtr, psoType };
}
