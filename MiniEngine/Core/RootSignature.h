#pragma once

//--------------------------------------------------------------------------------------
// RootParameter
//--------------------------------------------------------------------------------------
class RootParameter
{
	friend class RootSignature;
public:

	RootParameter() { m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF; }
	~RootParameter() { Clear(); }
	void Clear()
	{
		if (m_RootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
			delete[] m_RootParam.DescriptorTable.pDescriptorRanges;

		m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
	}

	void InitAsConstants( UINT Register, UINT NumDwords, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Constants.Num32BitValues = NumDwords;
		m_RootParam.Constants.ShaderRegister = Register;
		m_RootParam.Constants.RegisterSpace = 0;
	}

	void InitAsConstantBuffer( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsBufferSRV( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsBufferUAV( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		InitAsDescriptorTable( 1, Visibility );
		SetTableRange( 0, Type, Register, Count );
	}

	void InitAsDescriptorTable( UINT RangeCount, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.DescriptorTable.NumDescriptorRanges = RangeCount;
		m_RootParam.DescriptorTable.pDescriptorRanges = new D3D12_DESCRIPTOR_RANGE[RangeCount];
	}

	void SetTableRange( UINT RangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, UINT Space = 0 )
	{
		D3D12_DESCRIPTOR_RANGE* range = const_cast<D3D12_DESCRIPTOR_RANGE*>(m_RootParam.DescriptorTable.pDescriptorRanges + RangeIndex);
		range->RangeType = Type;
		range->NumDescriptors = Count;
		range->BaseShaderRegister = Register;
		range->RegisterSpace = Space;
		range->OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	const D3D12_ROOT_PARAMETER& operator() ( void ) const { return m_RootParam; }

protected:
	D3D12_ROOT_PARAMETER m_RootParam;
};

//--------------------------------------------------------------------------------------
// RootSignature
//--------------------------------------------------------------------------------------
class RootSignature
{
	friend class DynamicDescriptorHeap;
public:
	RootSignature( UINT NumRootParams = 0, UINT NumStaticSamplers = 0 );
	~RootSignature() {};

	static void Initialize();
	static void DestroyAll();

	void Reset( UINT NumRootParams, UINT NumStaticSamplers = 0 );
	RootParameter& operator[] ( size_t EntryIndex );
	const RootParameter& operator[] ( size_t EntryIndex ) const;
	void InitStaticSampler( UINT Register, const D3D12_SAMPLER_DESC& NonStaticSamplerDesc,
		D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL );
	void Finalize( D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE );
	ID3D12RootSignature* GetSignature() const { return m_Signature; }

protected:
	BOOL m_Finalized;
	UINT m_NumParameters;
	UINT m_NumSamplers;
	UINT m_NumInitializedStaticSamplers;
	uint32_t m_DescriptorTableBitMap;
	uint32_t m_DescriptorTableSize[16];
	UINT m_MaxDescriptorCacheHandleCount;
	std::unique_ptr<RootParameter[]> m_ParamArray;
	std::unique_ptr<D3D12_STATIC_SAMPLER_DESC[]> m_SamplerArray;
	ID3D12RootSignature* m_Signature;
};