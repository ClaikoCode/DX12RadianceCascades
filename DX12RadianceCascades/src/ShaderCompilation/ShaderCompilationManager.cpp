#include "rcpch.h"

#include <mutex>
#include <queue>
#include <chrono>
#include <condition_variable>

#include "ShaderCompilationManager.h"


std::wstring BuildShaderPath(const wchar_t* shaderFolder, const wchar_t* shaderName)
{
	return std::wstring(shaderFolder) + shaderName;
}

std::wstring Shader::BuildShaderPath(const wchar_t* shaderFolder, const wchar_t* shaderName)
{
	return std::wstring(shaderFolder) + shaderName + L".hlsl";
}
