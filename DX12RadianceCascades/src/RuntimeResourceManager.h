#pragma once

// Forward declaration.
class Model;

enum ModelID : UUID64
{
	ModelIDSponza = 0,
	ModelIDSphereTest,
};

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
	ShaderIDDebugDrawPS,
	ShaderIDDebugDrawVS,

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
	PSOIDDebugDrawPSO,

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
	static void AddShaderDependency(ShaderID shaderID, std::vector<PSOIDType> psoIDs); // Not a reference so rvalues can be input. Inefficient but its not a hotpath.
	static void RegisterPSO(PSOID psoID, void* psoPtr, PSOType psoType);

	static std::shared_ptr<Model> GetModelPtr(ModelID modelID);
	static D3D12_SHADER_BYTECODE GetShader(ShaderID shaderID);

	static void Destroy() { Get().DestroyImpl(); }

private:
	RuntimeResourceManager();

	void UpdatePSOs();
	void AddShaderDependencyImpl(ShaderID shaderID, std::vector<PSOIDType>& psoIDs); 
	void RegisterPSOImpl(PSOID psoID, void* psoPtr, PSOType psoType);

	std::shared_ptr<Model> GetModelPtrImpl(ModelID modelID);

	void DestroyImpl();

private:
	std::unordered_map<UUID64, std::set<PSOIDType>> m_shaderPSODependencyMap;
	std::array<PSOPackage, PSOIDCount> m_usedPSOs;

	std::unordered_map<ModelID, std::shared_ptr<Model>> m_models;
};