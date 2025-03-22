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
	std::wstring BuildShaderPath(const std::wstring& shaderFolder, const std::wstring& shaderName)
	{
		return shaderFolder + shaderName;
	}

	std::wstring BuildShaderPath(const std::wstring& shaderFile)
	{
		return BuildShaderPath(c_ShaderFolder, shaderFile);
	}

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

		if (!errorStringW.empty())
		{
			errorStringW.pop_back(); // Remove null terminator.
		}
			
		std::wstring separator = L"-----------------";
		std::wstring header = separator + L" START OF COMPILATION ERROR " + separator;
		std::wstring footer = separator + L" END OF COMPILATION ERROR " + separator;

		std::wstring errorOut = header + L"\n" + errorStringW + L"\n" + footer;
		LOG_ERROR(L"Compilation failed:\n\n{}", errorOut);
	}

	void AddToIncludeManager(ComPtr<IDxcIncludeHandler> includeHandler, const std::wstring& filename)
	{
		ComPtr<IDxcBlob> includeBlob = nullptr;
		std::wstring filePath = BuildShaderPath(filename);
		ThrowIfFailed(includeHandler->LoadSource(filePath.c_str(), includeBlob.GetAddressOf()));

#if defined(_DEBUG)
		if (includeBlob == nullptr)
		{
			LOG_ERROR(L"Failed to load '{}' for shader includes.", filePath);
		}
		else
		{
			LOG_DEBUG(L"Successfully added '{}' to shader includes", filePath);
		}
#endif
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

void AddArgFilename(ShaderCompilationArgs& compArgs, const std::wstring& filename)
{
	AddCompArg(compArgs, filename);
}

void AddArgShaderModel(ShaderCompilationArgs& compArgs, Shader::ShaderModel shaderModel, Shader::ShaderType shaderType)
{
	AddCompArg(compArgs, L"-T");
	AddCompArg(compArgs, ShaderModelArg(shaderModel, shaderType));
}

void AddArgEntryPoint(ShaderCompilationArgs& compArgs, const std::wstring& entryPoint)
{
	AddCompArg(compArgs, L"-E");
	AddCompArg(compArgs, entryPoint);
}

void AddArgDebugInfo(ShaderCompilationArgs& compArgs)
{
	AddCompArg(compArgs, DXC_ARG_DEBUG);
}

void AddArgOptimizationLevel(ShaderCompilationArgs& compArgs, uint16_t level)
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

	AddArgFilename(args, compPackage.shaderFilename);
	AddArgEntryPoint(args, compPackage.entryPoint);
	AddArgShaderModel(args, compPackage.shaderModel, compPackage.shaderType);

#if defined(_DEBUG)
	AddArgDebugInfo(args);
	AddArgOptimizationLevel(args, 0);
#else
	AddArgOptimizationLevel(args, 3);
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

Shader::ComDxcBuffer BlobEncodingToBuffer(ComPtr<IDxcBlobEncoding> source)
{
	Shader::ComDxcBuffer comDxcBuffer = {};
	comDxcBuffer.sourcePtr = source;
	DxcBuffer& dxcBuffer = comDxcBuffer.dxcBuffer;

	{
		dxcBuffer.Ptr = source->GetBufferPointer();
		BOOL known;
		UINT32 encoding;
		ThrowIfFailed(source->GetEncoding(&known, &encoding), L"Could not get encoding.");
		dxcBuffer.Encoding = encoding;
		dxcBuffer.Size = source->GetBufferSize();
	}

	return comDxcBuffer;
}

ShaderCompilationManager::ShaderCompilationManager() :
	m_shaderDirWatcher(
		Utils::WstringToString(c_ShaderFolder), 
		std::chrono::milliseconds(300), 
		[this](const std::string& fileName) { CompileDependencies(fileName); }
	)
{
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(m_library.GetAddressOf())), L"Could not create library instance");
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_compiler.GetAddressOf())), L"Could not create compiler instance");
	ThrowIfFailed(m_library->CreateIncludeHandler(m_includeHandler.GetAddressOf()), L"Could not create include handler");

	m_shaderDirWatcher.AddExtensionFilter(".hlsl");
	m_shaderDirWatcher.Start();

	InitializeShaderIncludes();
}

void ShaderCompilationManager::InitializeShaderIncludes()
{
	::AddToIncludeManager(m_includeHandler, L"Common.hlsli");
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
		LOG_WARNING(L"No dependencies has been registered for the shader '{}'.", shaderFilename);
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
			m_recentCompilations.insert(dependency);
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

	Shader::ComDxcBuffer comDxcBuffer = {};
	{
		ComPtr<IDxcBlobEncoding> source = nullptr;
		const std::wstring shaderPath = ::BuildShaderPath(shaderCompPackage.shaderFilename);
		ThrowIfFailed(m_library->CreateBlobFromFile(shaderPath.c_str(), nullptr, source.GetAddressOf()));
		comDxcBuffer = BlobEncodingToBuffer(source);
	}
	
	ComPtr<IDxcOperationResult> compResult = nullptr;
	{
		std::vector<WCHAR*> argPtrs = ConvertArgsToInputArgs(args);
		ThrowIfFailed(m_compiler->Compile(
			&comDxcBuffer.dxcBuffer,
			(LPCWSTR*)argPtrs.data(),
			(UINT32)argPtrs.size(),
			m_includeHandler.Get(),
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

#if defined(_DEBUG)
		LOG_DEBUG(L"Sucessfully compiled shader '{}'.", ::BuildShaderPath(shaderCompPackage.shaderFilename));
#endif
	}

	return shaderBlob;
}

void ShaderCompilationManager::RegisterShader(UUID64 shaderID, const std::wstring shaderFilename, Shader::ShaderType shaderType, bool compile /*= false*/)
{
	Shader::ShaderCompilationPackage compPackage = {};
	compPackage.shaderFilename = shaderFilename;
	compPackage.shaderType = shaderType;

	RegisterShader(shaderID, compPackage, compile);
}

void ShaderCompilationManager::RegisterShader(UUID64 shaderID, const Shader::ShaderCompilationPackage& compPackage, bool compile)
{
	if (compPackage.shaderFilename.empty())
	{
		LOG_ERROR(L"Cannot register shader '{}' without a path.", shaderID);
		return;
	}

	if (shaderID == NULL_ID)
	{
		LOG_ERROR(L"Invalid shader ID of '{}'.", shaderID);
		return;
	}

	{
		Shader::ShaderData shaderData = {};
		shaderData.shaderCompPackage = compPackage;
		m_shaderDataMap[shaderID] = std::move(shaderData);
	}

	{
		const std::wstring& shaderFilename = compPackage.shaderFilename;
		m_shaderDependencyMap[shaderFilename].insert(shaderID);
	}
	
	if (compile)
	{
		CompileShader(shaderID);
	}
}

void ShaderCompilationManager::GetCompiledShaderData(UUID64 shaderID, void** binaryOut, size_t* binarySizeOut)
{
	if (binaryOut == nullptr || binarySizeOut == nullptr)
	{
		LOG_ERROR(L"Cannot write shader data info to nullptrs.");
		return;
	}

	Shader::ShaderData* shaderData = GetShaderData(shaderID);
	if (shaderData && shaderData->shaderBlob)
	{
		*binaryOut = shaderData->shaderBlob->GetBufferPointer();
		*binarySizeOut = shaderData->shaderBlob->GetBufferSize();
	}
#if defined(_DEBUG)
	else
	{
		LOG_ERROR(L"No shader data was found.");
	}
#endif

}

const std::set<UUID64>& ShaderCompilationManager::GetRecentCompilations()
{
	return m_recentCompilations;
}

bool ShaderCompilationManager::HasRecentCompilations()
{
	return !m_recentCompilations.empty();
}

void ShaderCompilationManager::ClearRecentCompilations()
{
	m_recentCompilations.clear();
}



ShaderCompilationManager& ShaderCompilationManager::Get()
{
	static ShaderCompilationManager instance = {};

	return instance;
}

