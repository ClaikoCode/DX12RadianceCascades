#pragma once

#include "RaytracingPSO.h"
#include "ShaderTable.h"

// This struct is adapted from the RaytracingDispatchRayInputs struct in ModelViewer.cpp.
struct RaytracingDispatchRayInputs
{
	RaytracingDispatchRayInputs() : m_hitGroupStride(0) {}

	template<typename T>
	RaytracingDispatchRayInputs(RaytracingPSO& rtPSO, ShaderTable<T>& hitShaderTable, const std::wstring& rayGenExport, const std::wstring& missShaderExport)
	{
		Init(rtPSO, hitShaderTable, rayGenExport, missShaderExport);
	}

	template<typename T>
	void Init(RaytracingPSO& rtPSO, ShaderTable<T>& hitShaderTable, const std::wstring& rayGenExport, const std::wstring& missShaderExport)
	{
		m_stateObject = rtPSO.GetStateObject();
		m_hitGroupStride = GetShaderTableEntrySize(hitShaderTable);

		m_hitGroupShaderTable.Create(
			L"Hit Shader Table",
			1,
			GetShaderTableSize(hitShaderTable),
			hitShaderTable.data()
		);

		ShaderTableEntrySimple rayGenShaderIdentifier = ShaderTableEntrySimple(rtPSO.GetShaderIdentifier(rayGenExport));
		ShaderTableSimple rayGenShaderTable = { {rayGenShaderIdentifier} };
		m_rayGenShaderTable.Create(
			L"Ray Gen Shader Table",
			1,
			GetShaderTableSimpleSize(rayGenShaderTable),
			rayGenShaderTable.data()
		);

		ShaderTableEntrySimple missShaderIdentifier = ShaderTableEntrySimple(rtPSO.GetShaderIdentifier(missShaderExport));
		ShaderTableSimple missShaderTable = { {missShaderIdentifier} };
		m_missShaderTable.Create(
			L"Miss Shader Table",
			1,
			GetShaderTableSimpleSize(missShaderTable),
			missShaderTable.data()
		);

	}

	D3D12_DISPATCH_RAYS_DESC BuildDispatchRaysDesc(UINT width, UINT height)
	{
		D3D12_DISPATCH_RAYS_DESC desc = {};

		desc.HitGroupTable.StartAddress = m_hitGroupShaderTable.GetGpuVirtualAddress();
		desc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable.GetBufferSize();
		desc.HitGroupTable.StrideInBytes = m_hitGroupStride;

		desc.MissShaderTable.StartAddress = m_missShaderTable.GetGpuVirtualAddress();
		desc.MissShaderTable.SizeInBytes = m_missShaderTable.GetBufferSize();
		desc.MissShaderTable.StrideInBytes = desc.MissShaderTable.SizeInBytes; // Assumes only a single entry for miss ST.

		desc.RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable.GetGpuVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable.GetBufferSize();

		desc.Width = width;
		desc.Height = height;
		desc.Depth = 1;

		return desc;
	}

	Microsoft::WRL::ComPtr<ID3D12StateObject> m_stateObject;
	uint32_t m_hitGroupStride;
	ByteAddressBuffer m_rayGenShaderTable;
	ByteAddressBuffer m_missShaderTable;
	ByteAddressBuffer m_hitGroupShaderTable;
};

