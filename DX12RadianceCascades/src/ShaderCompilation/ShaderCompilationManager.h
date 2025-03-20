#pragma once

#include "DirectoryWatcher.h"

namespace Shader
{
    enum ShaderID : UUID64
    {
        ShaderIDNone = 0u,
        ShaderIDTest
    };

    // ID for a type of shader from the rendering pipeline.
    enum ShaderType : uint32_t
    {
        ShaderTypeNone = 0,
        ShaderTypeVS = 1,
        ShaderTypeHS = 1 << 1,
        ShaderTypeDS = 1 << 2,
        ShaderTypeGS = 1 << 3,
        ShaderTypePS = 1 << 4,
        ShaderTypeCS = 1 << 5,

        // Keep this after all the other shader types.
        ShaderTypeLast = 1 << 6,

        // Common combinations of shader types.
        ShaderTypeVS_PS = ShaderTypeVS | ShaderTypePS,
        ShaderTypeVS_PS_CS = ShaderTypeVS | ShaderTypePS | ShaderTypeCS,
        ShaderTypeHS_DS_GS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS,
        ShaderTypeHS_ALL_BUT_VS = ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS,
        ShaderTypeAll = ShaderTypeVS | ShaderTypeHS | ShaderTypeDS | ShaderTypeGS | ShaderTypePS | ShaderTypeCS
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
    };

    struct ShaderData
    {
        ShaderCompilationPackage shaderCompPackage;
        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;

    };

    
    std::wstring BuildShaderPath(const std::wstring& shaderFolder, const std::wstring& shaderName);
    std::wstring BuildShaderPath(const std::wstring& shaderFile);

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
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShaderPackageToBlob(const Shader::ShaderCompilationPackage& shaderCompPackage);

    void RegisterShader(UUID64 shaderID, const std::wstring shaderFilename, Shader::ShaderType shaderType, bool compile = false);
    void RegisterShader(UUID64 shaderID, const Shader::ShaderCompilationPackage& compPackage, bool compile);
    
    void GetCompiledShaderData(Shader::ShaderID shaderID, void** binaryOut, size_t* binarySizeOut);

private:

    ShaderCompilationManager(); // Private constructor
    ~ShaderCompilationManager() {}; // Private destructor

    Shader::ShaderData* GetShaderData(UUID64 shaderID);
    std::set<UUID64>* GetShaderDependencies(const std::wstring& shaderFilename);


private:
    Microsoft::WRL::ComPtr<IDxcLibrary> m_library;
    Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;

    // Maps unique shader ids to shader compilation objects.
    std::unordered_map<UUID64, Shader::ShaderData> m_shaderDataMap;

    // Maps shader filename (not path) to shader compilation objects that are dependent on it.
    std::unordered_map<std::wstring, std::set<UUID64>> m_shaderDependencyMap;

    DirectoryWatcher m_shaderDirWatcher;
};