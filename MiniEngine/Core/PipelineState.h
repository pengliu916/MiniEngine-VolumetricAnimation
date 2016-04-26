#pragma once

class RootSignature;

struct CD3D12_SHADER_BYTECODE :public D3D12_SHADER_BYTECODE
{
	CD3D12_SHADER_BYTECODE() {}
	~CD3D12_SHADER_BYTECODE() {}
	explicit CD3D12_SHADER_BYTECODE( const D3D12_SHADER_BYTECODE& o ) :D3D12_SHADER_BYTECODE( o ) {}
	explicit CD3D12_SHADER_BYTECODE( const void* _pShaderBytecode, SIZE_T _BytecodeLength )
	{
		pShaderBytecode = _pShaderBytecode;
		BytecodeLength = _BytecodeLength;
	}
	operator const D3D12_SHADER_BYTECODE&() const { return *this; }
};

//--------------------------------------------------------------------------------------
// PSO
//--------------------------------------------------------------------------------------
class PSO
{
public:
	PSO() :m_RootSignature( nullptr ) {}

	static void Initialize();
	static void DestroyAll();

	void SetRootSignature( const RootSignature& BindMappings );
	const RootSignature& GetRootSignature() const;
	ID3D12PipelineState* GetPipelineStateObject() const;
protected:
	const RootSignature* m_RootSignature;
	ID3D12PipelineState* m_PSO;
};

//--------------------------------------------------------------------------------------
// GraphicsPSO
//--------------------------------------------------------------------------------------
class GraphicsPSO :public PSO
{
	friend class CommandContext;
public:
	GraphicsPSO();
	void SetBlendState( const D3D12_BLEND_DESC& BlendDesc );
	void SetRasterizerState( const D3D12_RASTERIZER_DESC& RasterizerDesc );
	void SetDepthStencilState( const D3D12_DEPTH_STENCIL_DESC& DepthStencilDesc );
	void SetSampleMask( UINT SampleMask );
	void SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType );
	void SetRenderTargetFormats( UINT NumRTVs, const DXGI_FORMAT* RTVFormats, DXGI_FORMAT DSVFormat, UINT MsaaCount = 1, UINT MsaaQuality = 0 );
	void SetInputLayout( UINT NumElements, const D3D12_INPUT_ELEMENT_DESC* pInputElementDescs );
	void SetPrimitiveRestart( D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBProps );

	void SetVertexShader( const void* Binary, size_t Size ) { m_PSODesc.VS = CD3D12_SHADER_BYTECODE( Binary, Size ); }
	void SetPixelShader( const void* Binary, size_t Size ) { m_PSODesc.PS = CD3D12_SHADER_BYTECODE( Binary, Size ); }
	void SetGeometryShader( const void* Binary, size_t Size ) { m_PSODesc.GS = CD3D12_SHADER_BYTECODE( Binary, Size ); }
	void SetHullShader( const void* Binary, size_t Size ) { m_PSODesc.HS = CD3D12_SHADER_BYTECODE( Binary, Size ); }
	void SetDomainShader( const void* Binary, size_t Size ) { m_PSODesc.DS = CD3D12_SHADER_BYTECODE( Binary, Size ); }

	void SetVertexShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.VS = Binary; }
	void SetPixelShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.PS = Binary; }
	void SetGeometryShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.GS = Binary; }
	void SetHullShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.HS = Binary; }
	void SetDomainShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.DS = Binary; }

	// Perform validation and compute a hash value for fast state block comparisons
	void Finalize();

private:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC m_PSODesc;
	std::shared_ptr<const D3D12_INPUT_ELEMENT_DESC> m_InputLayouts;
};

//--------------------------------------------------------------------------------------
// ComputePSO
//--------------------------------------------------------------------------------------
class ComputePSO :public PSO
{
	friend class CommandContext;
public:
	ComputePSO();
	void SetComputeShader( const void* Binary, size_t Size ) { m_PSODesc.CS = CD3D12_SHADER_BYTECODE( Binary, Size ); }
	void SetComputeShader( const D3D12_SHADER_BYTECODE& Binary ) { m_PSODesc.CS = Binary; }
	void Finalize();

private:
	D3D12_COMPUTE_PIPELINE_STATE_DESC m_PSODesc;
};