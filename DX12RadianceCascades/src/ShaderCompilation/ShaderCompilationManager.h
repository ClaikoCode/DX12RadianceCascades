#pragma once

namespace Shader
{
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
    };

    // Holds all information necessary for dynamically compiling a shader.
    struct ShaderCompilationPackage
    {
        // Name for the shader (excluding path).
        const wchar_t* shaderFilename = nullptr;
        const char* entryPoint = "main";

        ShaderType shaderType = ShaderTypeNone;
        ShaderModel shaderModel = ShaderModel6_1;

        // Holds macro defines inserted into the shader.
        // NOTE: Null terminator for these will be added automatically.
        std::vector<D3D_SHADER_MACRO> defines = {};
    };

    // Shader name should not contain the .hlsl file extension.
    std::wstring BuildShaderPath(const wchar_t* shaderFolder, const wchar_t* shaderName);
}