#include "LibraryHeader.h"
#include "Utility.h"
#include "Graphics.h"
#include "RootSignature.h"

#include <unordered_map>
#include <thread>

using Microsoft::WRL::ComPtr;

static std::unordered_map<size_t, ComPtr<ID3D12RootSignature>> s_RootSignatureHashMap;
static CRITICAL_SECTION s_RootSignatureCS;

//--------------------------------------------------------------------------------------
// RootSignature
//--------------------------------------------------------------------------------------
RootSignature::RootSignature( UINT NumRootParams /* = 0 */, UINT NumStaticSamplers /* = 0 */ )
	:m_Finalized( FALSE ), m_NumParameters( NumRootParams )
{
	Reset( NumRootParams, NumStaticSamplers );
}

void RootSignature::Initialize()
{
	InitializeCriticalSection( &s_RootSignatureCS );
}

void RootSignature::DestroyAll()
{
	s_RootSignatureHashMap.clear();
	DeleteCriticalSection( &s_RootSignatureCS );
}

void RootSignature::Reset( UINT NumRootParams, UINT NumStaticSamplers /* = 0 */ )
{
	if (NumRootParams > 0)
		m_ParamArray.reset( new RootParameter[NumRootParams] );
	else
		m_ParamArray = nullptr;
	m_NumParameters = NumRootParams;

	if (NumStaticSamplers > 0)
		m_SamplerArray.reset( new D3D12_STATIC_SAMPLER_DESC[NumStaticSamplers] );
	else
		m_SamplerArray = nullptr;
	m_NumSamplers = NumStaticSamplers;
	m_NumInitializedStaticSamplers = 0;
}

RootParameter& RootSignature::operator []( size_t EntryIndex )
{
	ASSERT( EntryIndex < m_NumParameters );
	return m_ParamArray.get()[EntryIndex];
}

const RootParameter& RootSignature::operator []( size_t EntryIndex ) const
{
	ASSERT( EntryIndex < m_NumParameters );
	return m_ParamArray.get()[EntryIndex];
}

void RootSignature::InitStaticSampler( UINT Register, const D3D12_SAMPLER_DESC& NonStaticSamplerDesc,
	D3D12_SHADER_VISIBILITY Visibility /* = D3D12_SHADER_VISIBILITY_ALL */ )
{
	ASSERT( m_NumInitializedStaticSamplers < m_NumSamplers );
	D3D12_STATIC_SAMPLER_DESC& StaticSamplerDesc = m_SamplerArray[m_NumInitializedStaticSamplers++];
	StaticSamplerDesc.Filter = NonStaticSamplerDesc.Filter;
	StaticSamplerDesc.AddressU = NonStaticSamplerDesc.AddressU;
	StaticSamplerDesc.AddressV = NonStaticSamplerDesc.AddressV;
	StaticSamplerDesc.AddressW = NonStaticSamplerDesc.AddressW;
	StaticSamplerDesc.MipLODBias = NonStaticSamplerDesc.MipLODBias;
	StaticSamplerDesc.MaxAnisotropy = NonStaticSamplerDesc.MaxAnisotropy;
	StaticSamplerDesc.ComparisonFunc = NonStaticSamplerDesc.ComparisonFunc;
	StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	StaticSamplerDesc.MinLOD = NonStaticSamplerDesc.MinLOD;
	StaticSamplerDesc.MaxLOD = NonStaticSamplerDesc.MaxLOD;
	StaticSamplerDesc.ShaderRegister = Register;
	StaticSamplerDesc.RegisterSpace = 0;
	StaticSamplerDesc.ShaderVisibility = Visibility;

	if (StaticSamplerDesc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		StaticSamplerDesc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
		StaticSamplerDesc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
	{
		if (NonStaticSamplerDesc.BorderColor[3] == 1.0f)
		{
			if (NonStaticSamplerDesc.BorderColor[0] == 1.0f)
				StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
			else
				StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		}
		else
			StaticSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	}
}

void RootSignature::Finalize( D3D12_ROOT_SIGNATURE_FLAGS Flags /* = D3D12_ROOT_SIGNATURE_FLAG_NONE */ )
{
	if (m_Finalized)
		return;

	ASSERT( m_NumInitializedStaticSamplers == m_NumSamplers );

	D3D12_ROOT_SIGNATURE_DESC RootDesc;
	RootDesc.NumParameters = m_NumParameters;
	RootDesc.pParameters = (const D3D12_ROOT_PARAMETER *)m_ParamArray.get();
	RootDesc.NumStaticSamplers = m_NumSamplers;
	RootDesc.pStaticSamplers = (const D3D12_STATIC_SAMPLER_DESC *)m_SamplerArray.get();
	RootDesc.Flags = Flags;

	m_DescriptorTableBitMap = 0;
	m_MaxDescriptorCacheHandleCount = 0;

	size_t HashCode = HashStateArray( RootDesc.pStaticSamplers, m_NumSamplers );

	for (UINT Param = 0; Param < m_NumParameters; ++Param)
	{
		const D3D12_ROOT_PARAMETER& RootParam = RootDesc.pParameters[Param];
		m_DescriptorTableSize[Param] = 0;
		if (RootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			ASSERT( RootParam.DescriptorTable.pDescriptorRanges != nullptr );
			HashCode = HashStateArray( RootParam.DescriptorTable.pDescriptorRanges,
				RootParam.DescriptorTable.NumDescriptorRanges, HashCode );

			if (RootParam.DescriptorTable.pDescriptorRanges->RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
				continue;

			m_DescriptorTableBitMap |= (1 << Param);
			for (UINT TableRange = 0; TableRange < RootParam.DescriptorTable.NumDescriptorRanges; ++TableRange)
				m_DescriptorTableSize[Param] += RootParam.DescriptorTable.pDescriptorRanges[TableRange].NumDescriptors;

			m_MaxDescriptorCacheHandleCount += m_DescriptorTableSize[Param];
		}
		else
			HashCode = HashState( &RootParam, HashCode );
	}

	ID3D12RootSignature** RSRef = nullptr;
	bool firstCompile = false;
	{
		CriticalSectionScope LockGaurd( &s_RootSignatureCS );
		auto iter = s_RootSignatureHashMap.find( HashCode );
		if (iter == s_RootSignatureHashMap.end())
		{
			RSRef = s_RootSignatureHashMap[HashCode].GetAddressOf();
			firstCompile = true;
		}
		else
			RSRef = iter->second.GetAddressOf();
	}

	if (firstCompile)
	{
		ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
		HRESULT hr;
		V( D3D12SerializeRootSignature( &RootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf() ) );
		V( Graphics::g_device->CreateRootSignature( 1, pOutBlob->GetBufferPointer(),
			pOutBlob->GetBufferSize(), IID_PPV_ARGS( &m_Signature ) ) );
	}
	else
	{
		while (*RSRef == nullptr)
			std::this_thread::yield();
		m_Signature = *RSRef;
	}
	m_Finalized = TRUE;
}