#pragma once

class RootSignature;

//--------------------------------------------------------------------------------------
// IndirectParameter
//--------------------------------------------------------------------------------------
class IndirectParameter
{
	friend class CommandSignature;
public:
	IndirectParameter();
	void Draw();
	void DrawIndexed();
	void Dispatch();
	void VertexBufferView( UINT Slot );
	void IndexBufferView();
	void Constant( UINT RootParameterIndex, UINT DestOffsetIn32BitValues, UINT Num32BitValuesToSet );
	void ConstantBufferView( UINT RootParameterIndex );
	void ShaderResourceView( UINT RootParameterIndex );
	void UnorderedAccessView( UINT RootParameterIndex );
	D3D12_INDIRECT_ARGUMENT_TYPE GetType() const;

protected:
	D3D12_INDIRECT_ARGUMENT_DESC m_IndirectParam;
};

//--------------------------------------------------------------------------------------
// CommandSignature
//--------------------------------------------------------------------------------------
class CommandSignature
{
public:
	CommandSignature( UINT NumParams = 0 );
	void Destroy();
	void Reset( UINT NumParams );
	IndirectParameter& operator[] ( size_t EntryIndex );
	const IndirectParameter& operator[] ( size_t EntryIndex ) const;
	void Finalize( const RootSignature* RootSignature = nullptr );
	ID3D12CommandSignature* GetSignature() const;

protected:
	BOOL m_Finalized;
	UINT m_NumParameters;
	std::unique_ptr<IndirectParameter[]> m_ParamArray;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_Signature;
};

