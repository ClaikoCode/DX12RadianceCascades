#include "rcpch.h"

#include "ShaderCompilationManager.h"

using namespace Microsoft::WRL;

#if defined(_DEBUG)
static const std::wstring c_ShaderFolder = L"..\\DX12RadianceCascades\\Assets\\shaders\\";
#else
static const std::wstring c_ShaderFolder = L"shaders\\";
#endif

// Contains the preprocessor defines to be added to all files (note ALL). The vector is allowed to be empty.
static const std::vector<std::wstring> s_ppDefines = {
	// Used to give any file included in the compilation process to know if it being compiled for shaders.
	// Can be useful if the same file is intended to be used for both C++ and HLSL compilation.
	L"_HLSL", 

#if defined(_DEBUG)
	L"_DEBUG",
#endif

#if defined(_DEBUGDRAWING)
	L"_DEBUGDRAWING",
#endif

};

static const std::wstring c_IncludeDir = c_ShaderFolder;

typedef std::vector<std::wstring> ShaderCompilationArgs;

static std::mutex s_shaderCompMutex;
#define MUTEX_LOCK() std::lock_guard<std::mutex> guard(s_shaderCompMutex)

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
		LOG_ERROR(L"Shader compilation failed:\n\n{}", errorOut);
	}
}

// Saves all the filenames of included files.
class DependencyTrackingIncludeHandler : public IDxcIncludeHandler {

public:
	DependencyTrackingIncludeHandler(IDxcUtils* pUtils) 
	{
		pUtils->CreateDefaultIncludeHandler(&m_defaultIncludeHandler);
	}

	// IDxcIncludeHandler implementation
	HRESULT STDMETHODCALLTYPE LoadSource(_In_z_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override 
	{
		std::wstring filename = pFilename;

		// Remove the first three characters from filename (".\\")
		if (filename.size() > 2)
		{
			filename = filename.substr(2);
		}

		// Delegate to the default handler
		HRESULT hr = m_defaultIncludeHandler->LoadSource(filename.c_str(), ppIncludeSource);

		if (SUCCEEDED(hr))
		{
			// Add to our tracking set if everything was ok.
			m_includedFiles.insert(filename);
		}

		return hr;
	}

	const std::unordered_set<std::wstring>& GetIncludedFiles() const 
	{
		return m_includedFiles;
	}

	// IUnknown implementation
	ULONG STDMETHODCALLTYPE AddRef() override { return 0; }
	ULONG STDMETHODCALLTYPE Release() override { return 0; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override { return E_NOINTERFACE; }

private:

	// The default include handler to delegate actual file loading
	ComPtr<IDxcIncludeHandler> m_defaultIncludeHandler;

	// Set of included files for the current compilation
	std::unordered_set<std::wstring> m_includedFiles;
};

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
	case Shader::ShaderTypeLib:
		shaderTypeStr += L"lib"; // Library shader.
		break;
	default:
		LOG_ERROR(L"Unknown shader type: {}", (uint32_t)shaderType);
		break;
	}
	
	return shaderTypeStr + L"_" + shaderModelStr;
}

// A token in this context is defined as the whitespace-separated elements of a command string. 
// This can be options or parameters/arguments.
void AppendCompilationToken(ShaderCompilationArgs& compArgs, const std::wstring& token)
{
	compArgs.push_back(token);
}

void AddArgFilename(ShaderCompilationArgs& compArgs, const std::wstring& filename)
{
	AppendCompilationToken(compArgs, filename);
}

void AddArgShaderModel(ShaderCompilationArgs& compArgs, Shader::ShaderModel shaderModel, Shader::ShaderType shaderType)
{
	AppendCompilationToken(compArgs, L"-T");
	AppendCompilationToken(compArgs, ShaderModelArg(shaderModel, shaderType));
}

void AddArgEntryPoint(ShaderCompilationArgs& compArgs, const std::wstring& entryPoint)
{
	AppendCompilationToken(compArgs, L"-E");
	AppendCompilationToken(compArgs, entryPoint);
}

void AddArgDebugInfo(ShaderCompilationArgs& compArgs)
{
	AppendCompilationToken(compArgs, DXC_ARG_DEBUG);
}

void AddArgEmbedDebug(ShaderCompilationArgs& compArgs)
{
	AppendCompilationToken(compArgs, L"-Qembed_debug");
}

void AddArgOptimizationLevel(ShaderCompilationArgs& compArgs, uint16_t level)
{
	if (level > 3)
	{
		level = 3; // Defualt to highest available.
	}

	AppendCompilationToken(compArgs, std::wstring(L"-O") + std::to_wstring(level));
}

void AddArgIncludeDirectory(ShaderCompilationArgs& compArgs, const std::wstring& includeDir)
{
	AppendCompilationToken(compArgs, L"-I");
	AppendCompilationToken(compArgs, includeDir);
}

void AddArgDefine(ShaderCompilationArgs& compArgs, const std::wstring& define)
{
	AppendCompilationToken(compArgs, L"-D");
	AppendCompilationToken(compArgs, define);
}

ShaderCompilationArgs BuildArgsFromShaderPackage(const Shader::ShaderCompilationPackage& compPackage)
{
	ShaderCompilationArgs args = {};

	AddArgFilename(args, compPackage.shaderFilename);

	if (compPackage.shaderType != Shader::ShaderTypeRT)
	{
		AddArgEntryPoint(args, compPackage.entryPoint);
	}
	AddArgShaderModel(args, compPackage.shaderModel, compPackage.shaderType);
	AddArgIncludeDirectory(args, c_IncludeDir);
	
#if defined(_DEBUG)
	AddArgDebugInfo(args);
	AddArgEmbedDebug(args); // Important for graphics debugger to get the most info possible without separate PDBs.
	AddArgOptimizationLevel(args, 0);
#else
	AddArgOptimizationLevel(args, 3);
#endif

	for (const std::wstring& def : s_ppDefines)
	{
		AddArgDefine(args, def);
	}
	
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
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(m_utils.GetAddressOf())), L"Could not create utils instance");

	m_shaderDirWatcher.AddExtensionFilter(".hlsl");
	m_shaderDirWatcher.AddExtensionFilter(".hlsli");
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

void ShaderCompilationManager::AddShaderDependency(const std::wstring& shaderFilename, UUID64 shaderID)
{
	std::set<UUID64>& dependencies = m_shaderDependencyMap[shaderFilename];
	dependencies.insert(shaderID);
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
	// Has to be registered already.
	Shader::ShaderData* shaderData = GetShaderData(shaderID);

	if (shaderData)
	{
		bool isFirstCompilation = shaderData->shaderBlob == nullptr;

		if (CompileShaderPackageToBlob(shaderData->shaderCompPackage, shaderData->shaderBlob.GetAddressOf()))
		{
			// Add include files to dependency map.
			/*
				TODO: 
				Add the capability to see if the same include files are used.If not, dependencies need to be removed for the files that are no longer used.
			*/
			for (const std::wstring& includeFile : shaderData->shaderCompPackage.includeFiles)
			{
				AddShaderDependency(includeFile, shaderID);
			}

			if (!isFirstCompilation)
			{
				AddRecentReCompilation(shaderID);
			}
		}
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

bool ShaderCompilationManager::CompileShaderPackageToBlob(Shader::ShaderCompilationPackage& shaderCompPackage, IDxcBlob** outBlob)
{
	ShaderCompilationArgs args = BuildArgsFromShaderPackage(shaderCompPackage);

	Shader::ComDxcBuffer comDxcBuffer = {};
	{
		ComPtr<IDxcBlobEncoding> source = nullptr;
		const std::wstring shaderPath = ::BuildShaderPath(shaderCompPackage.shaderFilename);
		ThrowIfFailed(m_library->CreateBlobFromFile(shaderPath.c_str(), nullptr, source.GetAddressOf()), L"Failed creating blob from file.");
		comDxcBuffer = BlobEncodingToBuffer(source);
	}

	DependencyTrackingIncludeHandler includeHandler = DependencyTrackingIncludeHandler(m_utils.Get());
	
	ComPtr<IDxcOperationResult> compResult = nullptr;
	{
		std::vector<WCHAR*> argPtrs = ConvertArgsToInputArgs(args);
		ThrowIfFailed(m_compiler->Compile(
			&comDxcBuffer.dxcBuffer,
			(LPCWSTR*)argPtrs.data(),
			(UINT32)argPtrs.size(),
			&includeHandler,
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

		return false;
	}

	if(SUCCEEDED(status))
	{
		// Write result to out blob.
		compResult->GetResult(outBlob);

		// Overwrite any previous include files.
		shaderCompPackage.includeFiles = includeHandler.GetIncludedFiles();

		LOG_DEBUG(L"Sucessfully compiled '{}'.", ::BuildShaderPath(shaderCompPackage.shaderFilename));
	}

	return true;
}

void ShaderCompilationManager::RegisterComputeShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile)
{
	RegisterShader(shaderID, shaderFilename, Shader::ShaderTypeCS, compile);
}

void ShaderCompilationManager::RegisterVertexShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile)
{
	RegisterShader(shaderID, shaderFilename, Shader::ShaderTypeVS, compile);
}

void ShaderCompilationManager::RegisterPixelShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile)
{
	RegisterShader(shaderID, shaderFilename, Shader::ShaderTypePS, compile);
}

void ShaderCompilationManager::RegisterRaytracingShader(UUID64 shaderID, const std::wstring shaderFilename, bool compile)
{
	RegisterShader(shaderID, shaderFilename, Shader::ShaderTypeRT, compile);
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
		const std::wstring shaderPath = ::BuildShaderPath(shaderFilename);
		AddShaderDependency(shaderPath, shaderID);
	}
	
	if (compile)
	{
		CompileShader(shaderID);
	}
}

void ShaderCompilationManager::GetShaderDataBinary(UUID64 shaderID, void** binaryOut, size_t* binarySizeOut)
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

D3D12_SHADER_BYTECODE ShaderCompilationManager::GetShaderByteCode(UUID64 shaderID)
{
	void* binary = nullptr;
	size_t binarySize = 0;

	GetShaderDataBinary(shaderID, &binary, &binarySize);

	D3D12_SHADER_BYTECODE byteCode = {};
	byteCode.pShaderBytecode = binary;
	byteCode.BytecodeLength = binarySize;

	return byteCode;
}

Shader::ShaderType ShaderCompilationManager::GetShaderType(UUID64 shaderID)
{
	Shader::ShaderData* shaderData = GetShaderData(shaderID);
	if (shaderData)
	{
		return shaderData->shaderCompPackage.shaderType;
	}

	return Shader::ShaderTypeNone;
}

void ShaderCompilationManager::AddRecentReCompilation(UUID64 shaderID)
{
	MUTEX_LOCK();
	m_recentReCompilations.insert(shaderID);
}

const std::set<UUID64>& ShaderCompilationManager::GetRecentReCompilations()
{
	MUTEX_LOCK();
	return m_recentReCompilations;
}

bool ShaderCompilationManager::HasRecentReCompilations()
{
	MUTEX_LOCK();
	return !m_recentReCompilations.empty();
}

void ShaderCompilationManager::ClearRecentReCompilations()
{
	MUTEX_LOCK();
	m_recentReCompilations.clear();
}

ShaderCompilationManager& ShaderCompilationManager::Get()
{
	static ShaderCompilationManager instance = {};

	return instance;
}

