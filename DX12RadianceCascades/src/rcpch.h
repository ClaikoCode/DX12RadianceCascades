//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// rcpch.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <sstream>
#include <iomanip>

#include <list>
#include <string>
#include <wrl.h>
#include <shellapi.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include <set>
#include <chrono>
#include <assert.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include "d3dx12.h"
#include "dxcapi.h"

#include <DirectXMath.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#define D3D12_GPU_VIRTUAL_ADDRESS_NULL      ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN   ((D3D12_GPU_VIRTUAL_ADDRESS)-1)
#define MY_IID_PPV_ARGS                     IID_PPV_ARGS

typedef uint64_t UUID64;
constexpr UUID64 NULL_ID = UINT64_MAX;

#include "Logger.h"
#include "Utils.h"

// Ineficient but has to do for now.
static inline void ThrowIfFailed(HRESULT hr, std::wstring message = L"")
{
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed HR check: {} | Error message: {}", hr, message);
		throw std::runtime_error("HR RUNTIME ERROR");
	}
}

