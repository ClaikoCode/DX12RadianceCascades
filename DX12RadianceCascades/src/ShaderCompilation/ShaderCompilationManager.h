#pragma once

#include "DirectoryWatcher.h"
#include <unordered_set>

// ID for a type of shader from the rendering pipeline.
enum ShaderType : uint32_t
{
    ShaderTypeNone = 0,
    ShaderTypeVS = 1 << 0,
    ShaderTypeHS = 1 << 1,
    ShaderTypeDS = 1 << 2,
    ShaderTypeGS = 1 << 3,
    ShaderTypePS = 1 << 4,
    ShaderTypeCS = 1 << 5,
    ShaderTypeLib = 1 << 6, // Library shader. Used for raytracing shaders.

    // Keep this after all the other shader types.
    ShaderTypeLast = 1 << 7,

    // Common combinations of shader types.
    ShaderTypeVS_PS = ShaderTypeVS | ShaderTypePS,
    ShaderTypeVS_PS_CS = ShaderTypeVS | ShaderTypePS | ShaderTypeCS,
    ShaderTypeHS_DS_GS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS,
    ShaderTypeHS_ALL_BUT_VS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS,
    ShaderTypeAll = ShaderTypeVS | ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS,

    // All shader types allowed in a graphics pipeline.
    ShaderTypeGraphics = ShaderTypeVS | ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS,

    // Alias for raytracing shaders.
    ShaderTypeRT = ShaderTypeLib
};

static std::wstring ShaderTypeToString(ShaderType shaderType)
{
	switch (shaderType)
	{
	case ShaderTypeVS: return L"Vertex Shader";
	case ShaderTypeHS: return L"Hull Shader";
	case ShaderTypeDS: return L"Domain Shader";
	case ShaderTypeGS: return L"Geometry Shader";
	case ShaderTypePS: return L"Pixel Shader";
	case ShaderTypeCS: return L"Compute Shader";
	case ShaderTypeLib: return L"Library Shader";
    default: return L"Unknown Shader Type";
	}
}

// Checks if shader type A is of shader type B. Shader type B can be a bitwise combination.
#define IS_OF_SHADER_TYPE(shaderTypeA, shaderTypeB) ((shaderTypeA & (shaderTypeB)) == shaderTypeA)

enum ShaderModel
{
    ShaderModel5_0 = 0,
    ShaderModel6_1,
    ShaderModel6_3,

    ShaderModelCount // Keep last!
};

// Holds all information necessary for dynamically compiling a shader.
struct ShaderCompilationPackage
{
    // Not path.
    std::wstring shaderFilename = L"";
    std::wstring entryPoint = L"main";

    ShaderType shaderType = ShaderTypeNone;
    ShaderModel shaderModel = ShaderModel6_3;

    // Holds macro defines inserted into the shader.
    // NOTE: Null terminator for these will be added automatically.
    // TODO: Make use of this instead of current way of adding defines.
    std::vector<DxcDefine> defines = {};

    // Files that are included in the shader. Is overwritten every compilation.
    std::unordered_set<std::wstring> includeFiles = {};
};

struct ShaderData
{
    ShaderCompilationPackage shaderCompPackage;
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
};

// Special struct to make sure that source ptr lifetime is kept for lifetime of the DxcBuffer.
struct ComDxcBuffer
{
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourcePtr = nullptr;
    DxcBuffer dxcBuffer;
};

class ShaderCompilationManager
{
public:

    static ShaderCompilationManager& Get();
    std::string GetShaderDirectory();

    void CompileShader(UUID64 shaderID);
    void CompileDependencies(const std::wstring& shaderFilename);
    void CompileDependencies(const std::string& shaderFilename);
    void CompileDependencies(UUID64 shaderID);
    // Returns true if compilation was successful.
    bool CompileShaderPackageToBlob(ShaderCompilationPackage& shaderCompPackage, IDxcBlob** outBlob);

    void RegisterComputeShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    void RegisterVertexShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    void RegisterPixelShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
	void RegisterRaytracingShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    // Will try to deduce shader type from filename.
    void RegisterShader(UUID64 shaderID, const std::wstring& shaderFilename, bool compile = false);
    void RegisterShader(UUID64 shaderID, const std::wstring shaderFilename, ShaderType shaderType, bool compile = false);
    void RegisterShader(UUID64 shaderID, const ShaderCompilationPackage& compPackage, bool compile);
    
    const ShaderData* GetShaderData(UUID64 shaderID);
    void GetShaderDataBinary(UUID64 shaderID, void** binaryOut, size_t* binarySizeOut);
    D3D12_SHADER_BYTECODE GetShaderByteCode(UUID64 shaderID);
    ShaderType GetShaderType(UUID64 shaderID);

    void AddRecentReCompilation(UUID64 shaderID);
    const std::set<UUID64>& GetRecentReCompilations();
    bool HasRecentReCompilations();
    void ClearRecentReCompilations();

private:

    ShaderCompilationManager(); // Private constructor
    ~ShaderCompilationManager() {}; // Private destructor
   
	void AddShaderDependency(const std::wstring& shaderFilename, UUID64 shaderID);
    std::set<UUID64>* GetShaderDependencies(const std::wstring& shaderFilename);

private:
    Microsoft::WRL::ComPtr<IDxcLibrary> m_library;
    Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;
    Microsoft::WRL::ComPtr<IDxcUtils> m_utils;
    Microsoft::WRL::ComPtr<IDxcLinker> m_linker;

    // Maps unique shader ids to shader compilation objects.
    std::unordered_map<UUID64, ShaderData> m_shaderDataMap;

    // Maps shader filename (not path) to shader compilation objects that are dependent on it.
    // (maybe)TODO: Add capability to use path so that files with the same filename but in different dirs can be used.
    std::unordered_map<std::wstring, std::set<UUID64>> m_shaderDependencyMap;

    // New IDs are added every time a re-compilation of a shader is successful. Needs to be cleared manually.
    // A set is used to avoid reconstructing PSOs several times if several re-compilations oif the same shader had occured before a clear.
    std::set<UUID64> m_recentReCompilations;

    DirectoryWatcher m_shaderDirWatcher;
};