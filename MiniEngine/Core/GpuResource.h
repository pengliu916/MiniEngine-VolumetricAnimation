#pragma once

#define D3D12_GPU_VIRTUAL_ADDRESS_NULL 0ull
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ~0ull

//--------------------------------------------------------------------------------------
// GpuResource
//--------------------------------------------------------------------------------------
class GpuResource
{
	friend class CommandContext;
	friend class GraphicsContext;
	friend class ComputeContext;

public:
	GpuResource() :
		m_GpuVirtualAddress( D3D12_GPU_VIRTUAL_ADDRESS_NULL ),
		m_UsageState( D3D12_RESOURCE_STATE_COMMON ),
		m_TransitioningState( (D3D12_RESOURCE_STATES)-1 ) {}

	GpuResource( ID3D12Resource* pResource, D3D12_RESOURCE_STATES CurrentState ) :
		m_pResource( pResource ),
		m_UsageState( CurrentState ),
		m_TransitioningState( (D3D12_RESOURCE_STATES)-1 ) 
	{
		m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
	}

	GpuResource( GpuResource&& one )
	{
		m_pResource = one.m_pResource.Detach();
		m_UsageState = one.m_UsageState;
		m_TransitioningState = one.m_TransitioningState;
		m_GpuVirtualAddress = one.m_GpuVirtualAddress;
	}

	void Destroy() { m_pResource = nullptr; }

	ID3D12Resource* operator->() { return m_pResource.Get(); }
	const ID3D12Resource* operator->() const { return m_pResource.Get(); }

	ID3D12Resource* GetResource() { return m_pResource.Get(); }
	const ID3D12Resource* GetResource() const { return m_pResource.Get(); }

	D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const { return m_GpuVirtualAddress; }

protected:

	Microsoft::WRL::ComPtr<ID3D12Resource> m_pResource;
	D3D12_RESOURCE_STATES m_UsageState;
	D3D12_RESOURCE_STATES m_TransitioningState;
	D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;
};
//--------------------------------------------------------------------------------------
// PixelBuffer
//--------------------------------------------------------------------------------------
class PixelBuffer : public GpuResource
{
public:
	PixelBuffer() :m_Width( 0 ), m_Height( 0 ) {}
	uint32_t GetWidth() const { return m_Width; }
	uint32_t GetHeight() const { return m_Height; }
	uint32_t GetDepth() const { return m_ArraySize; }
	const DXGI_FORMAT& GetFormat() const { return m_Format; }

protected:
	D3D12_RESOURCE_DESC DescribeTex2D( uint32_t Width, uint32_t Height, uint32_t DepthOrArraySize,
		uint32_t NumMips, DXGI_FORMAT Format, UINT Flags );

	void AssociateWithResource( ID3D12Device* Device, const std::wstring& Name,
		ID3D12Resource* Resource, D3D12_RESOURCE_STATES CurrentState );

	void CreateTextureResource( ID3D12Device* Device, const std::wstring& Name,
		const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_CLEAR_VALUE ClearValue,
		D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );

	static DXGI_FORMAT GetBaseFormat( DXGI_FORMAT Format );
	static DXGI_FORMAT GetUAVFormat( DXGI_FORMAT Format );
	static DXGI_FORMAT GetDSVFormat( DXGI_FORMAT Format );
	static DXGI_FORMAT GetDepthFormat( DXGI_FORMAT Format );
	static DXGI_FORMAT GetStencilFormat( DXGI_FORMAT Format );

	uint32_t m_Width;
	uint32_t m_Height;
	uint32_t m_ArraySize;
	DXGI_FORMAT m_Format;
	std::wstring m_Name;
};

//--------------------------------------------------------------------------------------
// ColorBuffer
//--------------------------------------------------------------------------------------
class ColorBuffer : public PixelBuffer
{
public:
	ColorBuffer( DirectX::XMVECTOR ClearColor = DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f ) );
	void CreateFromSwapChain( const std::wstring& Name, ID3D12Resource* BaseResource );
	void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumMips,
		DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );
	void GuiShow();
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTV() const { return m_RTVHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVHandle[0]; }
	DirectX::XMVECTOR GetClearColor() const { return m_ClearColor; }

protected:
	static inline uint32_t ComputeNumMips( uint32_t Width, uint32_t Height )
	{
		uint32_t HighBit;
		_BitScanReverse( (unsigned long*)&HighBit, Width | Height );
		return HighBit + 1;
	}
	void CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips = 1 );

	DirectX::XMVECTOR m_ClearColor;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle[12];
	uint32_t m_NumMipMaps;
	bool m_GuiOpen = true;
	bool m_GuiNativeReso = false;
};

//--------------------------------------------------------------------------------------
// DepthBuffer
//--------------------------------------------------------------------------------------
class DepthBuffer : public PixelBuffer
{
public:
	DepthBuffer( FLOAT ClearDepth = .0f, UINT8 ClearStencil = 0 );
	void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT format,
		D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV() const { return m_DSVHandle[0]; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_DepthReadOnly() const { return m_DSVHandle[1]; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_StencilReadOnly() const { return m_DSVHandle[2]; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_ReadOnly() const { return m_DSVHandle[3]; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetDepthSRV() const { return m_DepthSRVHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetStencilSRV() const { return m_StencilSRVHandle; }
	float GetClearDepth() const { return m_ClearDepth; }
	uint32_t GetClearStencil() const { return m_ClearStencil; }

protected:
	void CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format );
	FLOAT m_ClearDepth;
	UINT8 m_ClearStencil;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DSVHandle[4];
	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthSRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_StencilSRVHandle;
};

//--------------------------------------------------------------------------------------
// VolumeTexture
//--------------------------------------------------------------------------------------
class VolumeTexture : public GpuResource
{
public:
	VolumeTexture();
	void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Depth, DXGI_FORMAT Format);
	uint32_t GetWidth() const { return m_Width; }
	uint32_t GetHeight() const { return m_Height; }
	uint32_t GetDepth() const { return m_Depth; }
	const DXGI_FORMAT& GetFormat() const { return m_Format; }

	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRVHandle; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAVHandle; }

protected:
	D3D12_RESOURCE_DESC DescribeTex3D( uint32_t Width, uint32_t Height, uint32_t Depth,
		DXGI_FORMAT Format);
	void CreateDerivedViews();

	DXGI_FORMAT GetBaseFormat();
	DXGI_FORMAT GetUAVFormat();

	uint32_t m_Width;
	uint32_t m_Height;
	uint32_t m_Depth;
	DXGI_FORMAT m_Format;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SRVHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE m_UAVHandle;
};

//--------------------------------------------------------------------------------------
// GpuBuffer
//--------------------------------------------------------------------------------------
class GpuBuffer :public GpuResource
{
public:
	virtual ~GpuBuffer() { Destroy(); }
	virtual void Destroy();
	void Create( const std::wstring& Name, uint32_t NumElements, uint32_t ElementSize, const void* InitData = nullptr );
	void CreatePlaced( const std::wstring& Name, ID3D12Heap* BackingHeap, uint32_t HeapOffset, uint32_t NumElements,
		uint32_t ElementSize, const void* InitData = nullptr );
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetUAV() const { return m_UAV; }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const { return m_SRV; }
	D3D12_GPU_VIRTUAL_ADDRESS RootConstantBufferView() const { return m_GpuVirtualAddress; }
	D3D12_CPU_DESCRIPTOR_HANDLE CreateConstantBufferView( uint32_t Offset, uint32_t Size ) const;
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView( size_t Offset, uint32_t Size, uint32_t Stride ) const;
	D3D12_VERTEX_BUFFER_VIEW VertexBufferView( size_t BaseVertexIndex = 0 ) const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView( size_t Offset, uint32_t Size, bool b32Bit = false ) const;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView( size_t StartIndex = 0 ) const;

protected:
	GpuBuffer();
	D3D12_RESOURCE_DESC DescribeBuffer();
	virtual void CreateDerivedViews() = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE m_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SRV;

	size_t m_BufferSize;
	uint32_t m_ElementCount;
	uint32_t m_ElementSize;
	D3D12_RESOURCE_FLAGS m_ResourceFlags;
};

inline D3D12_VERTEX_BUFFER_VIEW GpuBuffer::VertexBufferView( size_t Offset, uint32_t Size, uint32_t Stride ) const
{
	D3D12_VERTEX_BUFFER_VIEW VBView;
	VBView.BufferLocation = m_GpuVirtualAddress + Offset;
	VBView.SizeInBytes = Size;
	VBView.StrideInBytes = Stride;
	return VBView;
}

inline D3D12_VERTEX_BUFFER_VIEW GpuBuffer::VertexBufferView( size_t BaseVertexIndex /* = 0 */ ) const
{
	size_t Offset = BaseVertexIndex * m_ElementSize;
	return VertexBufferView( Offset, (uint32_t)(m_BufferSize - Offset), m_ElementSize );
}

inline D3D12_INDEX_BUFFER_VIEW GpuBuffer::IndexBufferView( size_t Offset, uint32_t Size, bool b32Bit ) const
{
	D3D12_INDEX_BUFFER_VIEW IBView;
	IBView.BufferLocation = m_GpuVirtualAddress + Offset;
	IBView.Format = b32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	IBView.SizeInBytes = Size;
	return IBView;
}

inline D3D12_INDEX_BUFFER_VIEW GpuBuffer::IndexBufferView( size_t StartIndex /*= 0*/ ) const
{
	size_t Offset = StartIndex * m_ElementSize;
	return IndexBufferView( Offset, (uint32_t)(m_BufferSize - Offset), m_ElementSize == 4 );
}

//--------------------------------------------------------------------------------------
// ByteAddressBuffer
//--------------------------------------------------------------------------------------
class ByteAddressBuffer : public GpuBuffer
{
public:
	virtual void CreateDerivedViews() override;
};

//--------------------------------------------------------------------------------------
// StructureBuffer
//--------------------------------------------------------------------------------------
class StructuredBuffer :public GpuBuffer
{
public:
	virtual void Destroy() override;
	virtual void CreateDerivedViews() override;

	ByteAddressBuffer& GetCounterBuffer();

	const D3D12_CPU_DESCRIPTOR_HANDLE& GetCounterSRV( CommandContext& Context );
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetCounterUAV( CommandContext& Context );

private:
	ByteAddressBuffer m_CounterBuffer;
};

//--------------------------------------------------------------------------------------
// TypedBuffer
//--------------------------------------------------------------------------------------
class TypedBuffer : public GpuBuffer
{
public:
	TypedBuffer( DXGI_FORMAT Format ) : m_DataFormat( Format ) {}
	virtual void CreateDerivedViews( void ) override;

protected:
	DXGI_FORMAT m_DataFormat;
};

//--------------------------------------------------------------------------------------
// IndirectArgsBuffer
//--------------------------------------------------------------------------------------
class IndirectArgsBuffer : public ByteAddressBuffer
{
public:
	IndirectArgsBuffer() {}
};

//--------------------------------------------------------------------------------------
// Texture
//--------------------------------------------------------------------------------------
class Texture : public GpuResource
{
	friend class CommandContext;
public:
	Texture();
	Texture( D3D12_CPU_DESCRIPTOR_HANDLE Handle );

	// Create a 2D texture
	void Create( size_t Width, size_t Height, DXGI_FORMAT Format, const void* InitData );
	/*void CreateTGAFromMemory(const void* memBuffer, size_t fileSize, bool sRGB);
	bool CreateDDSFromMemory(const void* memBuffer, size_t fileSize, bool sRGB);*/
	bool CreateFromFIle( const wchar_t* FileName, bool sRGB );
	void Destroy();
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetSRV() const;
	bool operator!();
protected:
	D3D12_CPU_DESCRIPTOR_HANDLE m_hCpuDescriptorHandle;
};