#pragma once

enum ShaderID : UUID64
{
	ShaderIDInvalid = 0,
	ShaderIDSceneRenderPS,
	ShaderIDSceneRenderVS,
	ShaderIDRCGatherCS,
	ShaderIDFlatlandSceneCS,
	ShaderIDFullScreenQuadVS,
	ShaderIDFullScreenCopyPS,
	ShaderIDFullScreenCopyCS,
	ShaderIDRCMergeCS,
	ShaderIDRCRadianceFieldCS,
	ShaderIDRaytracingTestRT,

	ShaderIDNone = NULL_ID
};

typedef uint32_t PSOIDType;
enum PSOID : PSOIDType
{
	PSOIDFirstExternalPSO = 0,
	PSOIDSecondExternalPSO,
	PSOIDComputeTestPSO,
	PSOIDComputeRCGatherPSO,
	PSOIDComputeFlatlandScenePSO,
	PSOIDComputeFullScreenCopyPSO,
	PSOIDComputeRCMergePSO,
	PSOIDComputeRCRadianceFieldPSO,
	PSOIDRaytracingTestPSO,

	PSOIDCount
};

enum PSOType : uint32_t
{
	PSOTypeCompute = 0,
	PSOTypeGraphics,
	PSOTypeRaytracing,

	PSOTypeCount
};

struct PSOPackage
{
	void* PSOPointer;
	PSOType psoType;
};

class RuntimeResourceManager
{
public:
	static RuntimeResourceManager& Get()
	{
		static RuntimeResourceManager instance = RuntimeResourceManager();

		return instance;
	}

	static void UpdateGraphicsPSOs();
	static void AddShaderDependency(ShaderID shaderID, std::vector<PSOIDType> psoIDs); // Not a reference so rvalues can be input.
	static void RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType);

	static D3D12_SHADER_BYTECODE GetShader(ShaderID shaderID);

protected:
	RuntimeResourceManager();

	void UpdateGraphicsPSOsImpl();
	void AddShaderDependencyImpl(ShaderID shaderID, std::vector<PSOIDType>& psoIDs); 
	void RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType);

private:
	std::unordered_map<UUID64, std::set<PSOIDType>> m_shaderPSODependencyMap;
	std::array<PSOPackage, PSOIDCount> m_usedPSOs;
};