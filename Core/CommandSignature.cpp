#include "LibraryHeader.h"
#include "Utility.h"
#include "Graphics.h"
#include "RootSignature.h"
#include "CommandSignature.h"

//--------------------------------------------------------------------------------------
// IndirectParameter
//--------------------------------------------------------------------------------------
IndirectParameter::IndirectParameter()
{
	m_IndirectParam.Type = (D3D12_INDIRECT_ARGUMENT_TYPE)0xFFFFFFFF;
}

void IndirectParameter::Draw()
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
}

void IndirectParameter::DrawIndexed()
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
}

void IndirectParameter::Dispatch()
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
}

void IndirectParameter::VertexBufferView( UINT Slot )
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
	m_IndirectParam.VertexBuffer.Slot = Slot;
}

void IndirectParameter::IndexBufferView()
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
}

void IndirectParameter::Constant( UINT RootParameterIndex, UINT DestOffsetIn32BitValues, UINT Num32BitValuesToSet )
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
	m_IndirectParam.Constant.RootParameterIndex = RootParameterIndex;
	m_IndirectParam.Constant.DestOffsetIn32BitValues = DestOffsetIn32BitValues;
	m_IndirectParam.Constant.Num32BitValuesToSet = Num32BitValuesToSet;
}

void IndirectParameter::ConstantBufferView( UINT RootParameterIndex )
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
	m_IndirectParam.ConstantBufferView.RootParameterIndex = RootParameterIndex;
}

void IndirectParameter::ShaderResourceView( UINT RootParameterIndex )
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
	m_IndirectParam.ShaderResourceView.RootParameterIndex = RootParameterIndex;
}

void IndirectParameter::UnorderedAccessView( UINT RootParameterIndex )
{
	m_IndirectParam.Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
	m_IndirectParam.UnorderedAccessView.RootParameterIndex = RootParameterIndex;
}

D3D12_INDIRECT_ARGUMENT_TYPE IndirectParameter::GetType() const
{
	return m_IndirectParam.Type;
}

//--------------------------------------------------------------------------------------
// CommandSignature
//--------------------------------------------------------------------------------------
CommandSignature::CommandSignature( UINT NumParams )
	:m_Finalized( FALSE ), m_NumParameters( NumParams )
{
	Reset( NumParams );
}

void CommandSignature::Destroy()
{
	m_Signature = nullptr;
	m_ParamArray = nullptr;
}

void CommandSignature::Reset( UINT NumParams )
{
	if (NumParams > 0) m_ParamArray.reset( new IndirectParameter[NumParams] );
	else m_ParamArray = nullptr;
	m_NumParameters = NumParams;
}

IndirectParameter& CommandSignature::operator[] ( size_t EntryIndex )
{
	ASSERT( EntryIndex < m_NumParameters );
	return m_ParamArray.get()[EntryIndex];
}

const IndirectParameter& CommandSignature::operator[]( size_t EntryIndex ) const
{
	ASSERT( EntryIndex < m_NumParameters );
	return m_ParamArray.get()[EntryIndex];
}

void CommandSignature::Finalize( const RootSignature* RootSignature /* = nullptr */ )
{
	if (m_Finalized) return;

	UINT ByteStride = 0;
	bool RequiresRootSignature = false;

	for (UINT i = 0; i < m_NumParameters; ++i)
	{
		switch (m_ParamArray[i].GetType())
		{
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
			ByteStride += sizeof( D3D12_DRAW_ARGUMENTS );
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
			ByteStride += sizeof( D3D12_DRAW_INDEXED_ARGUMENTS );
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
			ByteStride += sizeof( D3D12_DISPATCH_ARGUMENTS );
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
			RequiresRootSignature = true;
			ByteStride += 4;
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
			ByteStride += sizeof( D3D12_VERTEX_BUFFER_VIEW );
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
			ByteStride += sizeof( D3D12_INDEX_BUFFER_VIEW );
			break;
		case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
		case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
			ByteStride += 8;
			RequiresRootSignature = true;
			break;
		}
	}

	D3D12_COMMAND_SIGNATURE_DESC CommandSignatureDesc;
	CommandSignatureDesc.ByteStride = ByteStride;
	CommandSignatureDesc.NumArgumentDescs = m_NumParameters;
	CommandSignatureDesc.pArgumentDescs = (const D3D12_INDIRECT_ARGUMENT_DESC *)m_ParamArray.get();
	CommandSignatureDesc.NodeMask = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
	ID3D12RootSignature* pRootSig = RootSignature ? RootSignature->GetSignature() : nullptr;
	if (RequiresRootSignature)
	{
		ASSERT( pRootSig != nullptr );
	}
	else
	{
		pRootSig = nullptr;
	}
	HRESULT hr;
	V( Graphics::g_device->CreateCommandSignature( &CommandSignatureDesc, pRootSig, IID_PPV_ARGS( &m_Signature ) ) );
	m_Signature->SetName( L"CommandSignature" );
	m_Finalized = TRUE;
}

ID3D12CommandSignature* CommandSignature::GetSignature() const
{
	return m_Signature.Get();
}