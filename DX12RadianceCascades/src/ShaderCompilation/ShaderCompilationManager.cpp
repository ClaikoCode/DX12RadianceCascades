#include "rcpch.h"

#include <locale> // For wstring convert
#include "ShaderCompilationManager.h"

using namespace Microsoft::WRL;

static const std::wstring c_ShaderFolder = L"shaders\\";

typedef std::vector<std::wstring> ShaderCompilationArgs;

namespace Utils
{
	// These conversion functions were written by AI, not by me.

	std::string WstringToString(const std::wstring& wstr) {
		// Initialize the converter for the current locale
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(std::locale());

		std::mbstate_t state = std::mbstate_t();
		std::string result(wstr.size() * codecvt.max_length(), '\0');

		const wchar_t* from_next;
		char* to_next;

		// Perform the conversion
		codecvt.out(state,
			wstr.data(), wstr.data() + wstr.size(), from_next,
			result.data(), result.data() + result.size(), to_next);

		// Resize the result to the actual converted size
		result.resize(to_next - result.data());
		return result;
	}

	std::wstring StringToWstring(const std::string& str) {
		// Initialize the converter for the current locale
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(std::locale());

		std::mbstate_t state = std::mbstate_t();
		std::wstring result(str.size(), L'\0');

		const char* from_next;
		wchar_t* to_next;

		// Perform the conversion
		codecvt.in(state,
			str.data(), str.data() + str.size(), from_next,
			result.data(), result.data() + result.size(), to_next);

		// Resize the result to the actual converted size
		result.resize(to_next - result.data());
		return result;
	}
}

namespace
{
	void HandleCompilationError(ComPtr<IDxcBlobEncoding> errorBlob)
	{
		std::wstring errorStringW = L"";

		LPVOID bufferPtr = errorBlob->GetBufferPointer();
		SIZE_T bufferSize = errorBlob->GetBufferSize();

		// Check encoding.
		{
			UINT32 encodingCodePage;
			BOOL encodingKnown;
			errorBlob->GetEncoding(&encodingKnown, &encodingCodePage);

			if (encodingKnown)
			{
				if (encodingCodePage == DXC_CP_UTF8)
				{
					const char* errorPtr = reinterpret_cast<const char*>(bufferPtr);
					errorStringW = Utils::StringToWstring(std::string(errorPtr, bufferSize));
				}
				else if (encodingCodePage == DXC_CP_UTF16)
				{
					const wchar_t* errorPtr = reinterpret_cast<const wchar_t*>(bufferPtr);
					errorStringW = std::wstring(errorPtr, bufferSize);
				}
			}
		}

		LOG_ERROR(L"Compilation failed:\n\n{}", errorStringW);
	}
}



namespace Shader
{
	std::wstring BuildShaderPath(const std::wstring& shaderFolder, const std::wstring& shaderName)
	{
		return shaderFolder + shaderName;
	}

	std::wstring BuildShaderPath(const std::wstring& shaderFile)
	{
		return BuildShaderPath(c_ShaderFolder, shaderFile);
	}
}

std::wstring ShaderModelArg(Shader::ShaderModel shaderModel, Shader::ShaderType shaderType)
{
	const std::wstring& shaderModelStr = Shader::c_ShaderModelArg[shaderModel];

	std::wstring shaderTypeStr = L"";

	switch (shaderType)
	{
	case Shader::ShaderTypeVS:
		shaderTypeStr += L"vs";
		break;
	case Shader::ShaderTypeHS:
		shaderTypeStr += L"hs";
		break;
	case Shader::ShaderTypeDS:
		shaderTypeStr += L"ds";
		break;
	case Shader::ShaderTypeGS:
		shaderTypeStr += L"gs";
		break;
	case Shader::ShaderTypePS:
		shaderTypeStr += L"ps";
		break;
	case Shader::ShaderTypeCS:
		shaderTypeStr += L"cs";
		break;
	default:
		LOG_ERROR(L"Unknown shader type: {}", (uint32_t)shaderType);
		break;
	}
	
	return shaderTypeStr + L"_" + shaderModelStr;
}


void AddCompArg(ShaderCompilationArgs& compArgs, const std::wstring& arg)
{
	compArgs.push_back(arg);
}

void AddFilenameArg(ShaderCompilationArgs& compArgs, const std::wstring& filename)
{
	AddCompArg(compArgs, filename);
}

void AddShaderModelArg(ShaderCompilationArgs& compArgs, Shader::ShaderModel shaderModel, Shader::ShaderType shaderType)
{
	AddCompArg(compArgs, L"-T");
	AddCompArg(compArgs, ShaderModelArg(shaderModel, shaderType));
}

void AddEntryPointArg(ShaderCompilationArgs& compArgs, const std::wstring& entryPoint)
{
	AddCompArg(compArgs, L"-E");
	AddCompArg(compArgs, entryPoint);
}

void AddDebugInfoArg(ShaderCompilationArgs& compArgs)
{
	AddCompArg(compArgs, DXC_ARG_DEBUG);
}

void AddOptimizationLevel(ShaderCompilationArgs& compArgs, uint16_t level)
{
	if (level > 3)
	{
		level = 3; // Defualt to highest available.
	}

	AddCompArg(compArgs, std::wstring(L"-O") + std::to_wstring(level));
}

ShaderCompilationArgs BuildArgsFromShaderPackage(const Shader::ShaderCompilationPackage& compPackage)
{
	ShaderCompilationArgs args = {};

	AddFilenameArg(args, compPackage.shaderFilename);
	AddEntryPointArg(args, compPackage.entryPoint);
	AddShaderModelArg(args, compPackage.shaderModel, compPackage.shaderType);

#if defined(_DEBUG)
	AddDebugInfoArg(args);
	AddOptimizationLevel(args, 0);
#else
	AddOptimizationLevel(args, 3);
#endif
	
	return args;
}

std::vector<WCHAR*> ConvertArgsToInputArgs(const ShaderCompilationArgs& compArgs)
{
	std::vector<WCHAR*> argPtrs = {};

	for (auto& arg : compArgs)
	{
		WCHAR* wstrPtr = const_cast<WCHAR*>(arg.c_str());
		argPtrs.push_back(wstrPtr);
	}

	return argPtrs;
}

DxcBuffer BlobEncodingToBuffer(ComPtr<IDxcBlobEncoding> source)
{
	DxcBuffer dxcBuffer;
	

	dxcBuffer.Ptr = source->GetBufferPointer();
	BOOL known;
	UINT32 encoding;
	dxcBuffer.Encoding = source->GetEncoding(&known, &encoding);
	dxcBuffer.Size = source->GetBufferSize();

	return dxcBuffer;
}

ShaderCompilationManager::ShaderCompilationManager() :
	m_shaderDirWatcher(
		Utils::WstringToString(c_ShaderFolder), 
		std::chrono::milliseconds(300), 
		[this](const std::string& fileName) { CompileDependencies(fileName); }
	)
{
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(m_library.GetAddressOf())));
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_compiler.GetAddressOf())));

	m_shaderDirWatcher.AddExtensionFilter(".hlsl");
	m_shaderDirWatcher.Start();
}

Shader::ShaderData* ShaderCompilationManager::GetShaderData(UUID64 shaderID)
{
	Shader::ShaderData* shaderData = nullptr;

	auto it = m_shaderDataMap.find(shaderID);
	if (it == m_shaderDataMap.end())
	{
		LOG_WARNING(L"No shader was registered with given UUID64: {}", shaderID);
	}
	else
	{
		shaderData = &it->second;
	}

	return shaderData;
}

std::set<UUID64>* ShaderCompilationManager::GetShaderDependencies(const std::wstring& shaderFilename)
{
	std::set<UUID64>* shaderDependencies = nullptr;

	auto it = m_shaderDependencyMap.find(shaderFilename);
	if (it == m_shaderDependencyMap.end())
	{
		//LOG_WARNING(L"No dependencies has been registered for the shader '{}'.", shaderFilename);
	}
	else
	{
		shaderDependencies = &it->second;
	}

	return shaderDependencies;
}

void ShaderCompilationManager::CompileShader(UUID64 shaderID)
{
	Shader::ShaderData* shaderData = GetShaderData(shaderID);
	if (shaderData)
	{
		shaderData->shaderBlob = CompileShaderPackageToBlob(shaderData->shaderCompPackage);
	}	
}

void ShaderCompilationManager::CompileDependencies(const std::wstring& shaderFilename)
{
	std::set<UUID64>* dependencies = GetShaderDependencies(shaderFilename);

	if (dependencies)
	{
		for (UUID64 dependency : *dependencies)
		{
			CompileShader(dependency);
		}
	}
}

void ShaderCompilationManager::CompileDependencies(const std::string& shaderFilename)
{
	CompileDependencies(Utils::StringToWstring(shaderFilename));
}

void ShaderCompilationManager::CompileDependencies(UUID64 shaderID)
{
	Shader::ShaderData* shaderData = GetShaderData(shaderID);
	if (shaderData)
	{
		CompileDependencies(shaderData->shaderCompPackage.shaderFilename);
	}
}

ComPtr<IDxcBlob> ShaderCompilationManager::CompileShaderPackageToBlob(const Shader::ShaderCompilationPackage& shaderCompPackage)
{
	ShaderCompilationArgs args = BuildArgsFromShaderPackage(shaderCompPackage);

	DxcBuffer dxcBuffer = {};
	{
		ComPtr<IDxcBlobEncoding> source = nullptr;
		const std::wstring shaderPath = Shader::BuildShaderPath(shaderCompPackage.shaderFilename);
		ThrowIfFailed(m_library->CreateBlobFromFile(shaderPath.c_str(), nullptr, source.GetAddressOf()));
		dxcBuffer = BlobEncodingToBuffer(source);
	}
	
	ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
	ThrowIfFailed(m_library->CreateIncludeHandler(includeHandler.GetAddressOf()));

	ComPtr<IDxcOperationResult> compResult = nullptr;
	{
		std::vector<WCHAR*> argPtrs = ConvertArgsToInputArgs(args);
		ThrowIfFailed(m_compiler->Compile(
			&dxcBuffer,
			(LPCWSTR*)argPtrs.data(),
			(UINT32)argPtrs.size(),
			includeHandler.Get(),
			IID_PPV_ARGS(compResult.GetAddressOf())
		));
	}
	
	HRESULT status;
	compResult->GetStatus(&status);
	if (FAILED(status))
	{
		ComPtr<IDxcBlobEncoding> error = nullptr;
		compResult->GetErrorBuffer(error.GetAddressOf());

		if (error)
		{
			::HandleCompilationError(error);
		}
	}

	ComPtr<IDxcBlob> shaderBlob = nullptr;
	if(SUCCEEDED(status))
	{
		compResult->GetResult(shaderBlob.GetAddressOf());
	}

	return shaderBlob;
}

void ShaderCompilationManager::RegisterShader(UUID64 shaderID, const std::wstring shaderFilename, Shader::ShaderType shaderType)
{
	Shader::ShaderCompilationPackage compPackage = {};
	compPackage.shaderFilename = shaderFilename;
	compPackage.shaderType = shaderType;

	RegisterShader(shaderID, compPackage);
}

void ShaderCompilationManager::RegisterShader(UUID64 shaderID, const Shader::ShaderCompilationPackage& compPackage)
{
	if (compPackage.shaderFilename.empty())
	{
		LOG_ERROR(L"Cannot register shader '{}' without a path.", shaderID);
		return;
	}

	if (shaderID == Shader::ShaderIDNone)
	{
		LOG_ERROR(L"Invalid shader ID of '{}'.", shaderID);
		return;
	}

	Shader::ShaderData shaderData = {};
	shaderData.shaderCompPackage = compPackage;
	m_shaderDataMap[shaderID] = std::move(shaderData);

	m_shaderDependencyMap[compPackage.shaderFilename].insert(shaderID);
}



ShaderCompilationManager& ShaderCompilationManager::Get()
{
	static ShaderCompilationManager instance = {};

	return instance;
}

