#pragma once

#include "DirectoryWatcher.h"
#include <unordered_set>

namespace Shader
{
    // ID for a type of shader from the rendering pipeline.
    enum ShaderType : uint32_t
    {
        ShaderTypeNone  = 0,
        ShaderTypeVS    = 1 << 0,
        ShaderTypeHS    = 1 << 1,
        ShaderTypeDS    = 1 << 2,
        ShaderTypeGS    = 1 << 3,
        ShaderTypePS    = 1 << 4,
        ShaderTypeCS    = 1 << 5,
		ShaderTypeLib   = 1 << 6, // Library shader. Used for raytracing shaders.

        // Keep this after all the other shader types.
        ShaderTypeLast = 1 << 7,

        // Common combinations of shader types.
        ShaderTypeVS_PS = ShaderTypeVS | ShaderTypePS,
        ShaderTypeVS_PS_CS = ShaderTypeVS | ShaderTypePS | ShaderTypeCS,
        ShaderTypeHS_DS_GS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS,
        ShaderTypeHS_ALL_BUT_VS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS,
        ShaderTypeAll = ShaderTypeVS | ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS,

        // Alias for raytracing shaders.
        ShaderTypeRT = ShaderTypeLib 
    };

    enum ShaderModel
    {
        ShaderModel5_0 = 0,
        ShaderModel6_1,
        ShaderModel6_3,

        ShaderModelCount // Keep last!
    };

    static const std::array<std::wstring, ShaderModelCount> c_ShaderModelArg = {
        L"5_0",
        L"6_1",
        L"6_3"
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
}

class ShaderCompilationManager
{
public:

    static ShaderCompilationManager& Get();
    void CompileShader(UUID64 shaderID);
    void CompileDependencies(const std::wstring& shaderFilename);
    void CompileDependencies(const std::string& shaderFilename);
    void CompileDependencies(UUID64 shaderID);
    // Returns true if compilation was successful.
    bool CompileShaderPackageToBlob(Shader::ShaderCompilationPackage& shaderCompPackage, IDxcBlob** outBlob);

    void RegisterComputeShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    void RegisterVertexShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    void RegisterPixelShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
	void RegisterRaytracingShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile = false);
    void RegisterShader(UUID64 shaderID, const std::wstring shaderFilename, Shader::ShaderType shaderType, bool compile = false);
    void RegisterShader(UUID64 shaderID, const Shader::ShaderCompilationPackage& compPackage, bool compile);
    
    void GetShaderDataBinary(UUID64 shaderID, void** binaryOut, size_t* binarySizeOut);
    D3D12_SHADER_BYTECODE GetShaderByteCode(UUID64 shaderID);
    Shader::ShaderType GetShaderType(UUID64 shaderID);

    void AddRecentReCompilation(UUID64 shaderID);
    const std::set<UUID64>& GetRecentReCompilations();
    bool HasRecentReCompilations();
    void ClearRecentReCompilations();

private:

    ShaderCompilationManager(); // Private constructor
    ~ShaderCompilationManager() {}; // Private destructor


    Shader::ShaderData* GetShaderData(UUID64 shaderID);
	void AddShaderDependency(const std::wstring& shaderFilename, UUID64 shaderID);
    std::set<UUID64>* GetShaderDependencies(const std::wstring& shaderFilename);

private:
    Microsoft::WRL::ComPtr<IDxcLibrary> m_library;
    Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;
    Microsoft::WRL::ComPtr<IDxcUtils> m_utils;
    Microsoft::WRL::ComPtr<IDxcLinker> m_linker;

    // Maps unique shader ids to shader compilation objects.
    std::unordered_map<UUID64, Shader::ShaderData> m_shaderDataMap;

    // Maps shader filename (not path) to shader compilation objects that are dependent on it.
    // (maybe)TODO: Add capability to use path so that files with the same filename but in different dirs can be used.
    std::unordered_map<std::wstring, std::set<UUID64>> m_shaderDependencyMap;

    // New IDs are added every time a re-compilation of a shader is successful. Needs to be cleared manually.
    std::set<UUID64> m_recentReCompilations;

    DirectoryWatcher m_shaderDirWatcher;
};