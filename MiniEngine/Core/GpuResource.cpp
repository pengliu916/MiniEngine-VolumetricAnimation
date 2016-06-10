#include "LibraryHeader.h"
#include "Graphics.h"
#include "DescriptorHeap.h"
#include "CommandContext.h"
#include "Utility.h"
#include "DDSTextureLoader.h"
#include "GpuResource.h"
#include "imgui.h"
#include <atlbase.h>

//--------------------------------------------------------------------------------------
// PixelBuffer
//--------------------------------------------------------------------------------------
D3D12_RESOURCE_DESC PixelBuffer::DescribeTex2D( uint32_t Width, uint32_t Height, uint32_t DepthOrArraySize,
	uint32_t NumMips, DXGI_FORMAT Format, UINT Flags )
{
	m_Width = Width;
	m_Height = Height;
	m_ArraySize = DepthOrArraySize;
	m_Format = Format;

	D3D12_RESOURCE_DESC Desc = {};
	Desc.Alignment = 0;
	Desc.DepthOrArraySize = (UINT16)DepthOrArraySize;
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Flags = (D3D12_RESOURCE_FLAGS)Flags;
	Desc.Format = GetBaseFormat( Format );
	Desc.Height = (UINT)Height;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	Desc.MipLevels = NumMips;
	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Width = (UINT64)Width;
	return Desc;
}

void PixelBuffer::AssociateWithResource( ID3D12Device* Device, const std::wstring& Name,
	ID3D12Resource* Resource, D3D12_RESOURCE_STATES CurrentState )
{
	ASSERT( Resource != nullptr );
	D3D12_RESOURCE_DESC ResourceDesc = Resource->GetDesc();

	m_pResource.Attach( Resource );
	m_UsageState = CurrentState;

	m_Width = (uint32_t)ResourceDesc.Width;
	m_Height = (uint32_t)ResourceDesc.Height;
	m_ArraySize = (uint32_t)ResourceDesc.DepthOrArraySize;
	m_Format = ResourceDesc.Format;

#ifndef RELEASE
	m_pResource->SetName( Name.c_str() );
#else
	(Name);
#endif
}

void PixelBuffer::CreateTextureResource( ID3D12Device* Device, const std::wstring& Name,
	const D3D12_RESOURCE_DESC& ResourceDesc, D3D12_CLEAR_VALUE ClearValue,
	D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr /* = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN */ )
{
	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	HRESULT hr;
	V( Device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &ResourceDesc,
		D3D12_RESOURCE_STATE_COMMON, &ClearValue, IID_PPV_ARGS( &m_pResource ) ) );
#ifdef RELEASE
	( Name );
#else
	m_pResource->SetName( Name.c_str() );
#endif
	m_UsageState = D3D12_RESOURCE_STATE_COMMON;
	m_GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS_NULL;
	m_Name = Name;
}

DXGI_FORMAT PixelBuffer::GetBaseFormat( DXGI_FORMAT defaultFormat )
{
	switch (defaultFormat)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_TYPELESS;

		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32G8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_TYPELESS;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_TYPELESS;

	default:
		return defaultFormat;
	}
}

DXGI_FORMAT PixelBuffer::GetUAVFormat( DXGI_FORMAT defaultFormat )
{
	switch (defaultFormat)
	{
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_UNORM;

	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM;

	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_UNORM;

	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

#ifdef _DEBUG
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_D16_UNORM:

		PRINTWARN( "Requested a UAV format for a depth stencil format." );
#endif

	default:
		return defaultFormat;
	}
}

DXGI_FORMAT PixelBuffer::GetDSVFormat( DXGI_FORMAT defaultFormat )
{
	switch (defaultFormat)
	{
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_D32_FLOAT;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_D16_UNORM;

	default:
		return defaultFormat;
	}
}

DXGI_FORMAT PixelBuffer::GetDepthFormat( DXGI_FORMAT defaultFormat )
{
	switch (defaultFormat)
	{
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_UNORM;

	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

DXGI_FORMAT PixelBuffer::GetStencilFormat( DXGI_FORMAT defaultFormat )
{
	switch (defaultFormat)
	{
		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_X24_TYPELESS_G8_UINT;

	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

//--------------------------------------------------------------------------------------
// ColorBuffer
//--------------------------------------------------------------------------------------
ColorBuffer::ColorBuffer( DirectX::XMVECTOR ClearColor )
	: m_ClearColor( ClearColor ), m_NumMipMaps( 0 )
{
	m_SRVHandle.ptr = ~0ull;
	m_RTVHandle.ptr = ~0ull;
	std::memset( m_UAVHandle, 0xFF, sizeof( m_UAVHandle ) );
}

void ColorBuffer::CreateFromSwapChain( const std::wstring& Name, ID3D12Resource* BaseResource )
{
	AssociateWithResource( Graphics::g_device.Get(), Name, BaseResource, D3D12_RESOURCE_STATE_PRESENT );
	if (m_RTVHandle.ptr == ~0ull)
		m_RTVHandle = Graphics::g_pRTVDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateRenderTargetView( m_pResource.Get(), nullptr, m_RTVHandle );
}

void ColorBuffer::Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumMips,
	DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr /* = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN */ )
{
	NumMips = (NumMips == 0 ? ComputeNumMips( Width, Height ) : NumMips);
	D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D( Width, Height, 1, NumMips, Format,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );

	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Format;
	ClearValue.Color[0] = DirectX::XMVectorGetX( m_ClearColor );
	ClearValue.Color[1] = DirectX::XMVectorGetY( m_ClearColor );
	ClearValue.Color[2] = DirectX::XMVectorGetZ( m_ClearColor );
	ClearValue.Color[3] = DirectX::XMVectorGetW( m_ClearColor );

	CreateTextureResource( Graphics::g_device.Get(), Name, ResourceDesc, ClearValue, VidMemPtr );
	CreateDerivedViews( Graphics::g_device.Get(), Format, 1, NumMips );
}

void ColorBuffer::GuiShow()
{
	USES_CONVERSION;
	if (ImGui::Begin( W2A( m_Name.c_str() ), &m_GuiOpen ))
	{
		ImTextureID tex_id = (void*)&this->GetSRV();
		uint32_t OrigTexWidth = this->GetWidth();
		uint32_t OrigTexHeight = this->GetHeight();

		ImGuiStyle& style = ImGui::GetStyle();
		char temp[64];
		sprintf( temp, "Native Reso:%dx%d", OrigTexWidth, OrigTexHeight );
		ImGui::Checkbox( temp, &m_GuiNativeReso );
		float height = ImGui::GetWindowHeight();
		float AdaptedTexHeight = ImGui::GetContentRegionAvail().y;
		float AdaptedTexWidth = AdaptedTexHeight *OrigTexWidth / OrigTexHeight;

		if (m_GuiNativeReso)
		{
			ImGui::Image( tex_id, ImVec2( (float)OrigTexWidth, (float)OrigTexHeight ) );
			ImGui::SetWindowSize( ImVec2( 0, 0 ) );
		}
		else
		{
			ImGui::Image( tex_id, ImVec2( AdaptedTexWidth, AdaptedTexHeight ) );
			ImGui::SetWindowSize( ImVec2( AdaptedTexWidth + 2.f * style.WindowPadding.x, height > 240.f ? height : 240.f ) );
		}
	}
	ImGui::End();
}

void ColorBuffer::CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize, uint32_t NumMips /* = 1 */ )
{
	ASSERT( ArraySize == 1 || NumMips == 1 );
	m_NumMipMaps = NumMips - 1;
	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};

	RTVDesc.Format = Format;
	UAVDesc.Format = GetUAVFormat( Format );
	SRVDesc.Format = Format;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (ArraySize > 1)
	{
		RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		RTVDesc.Texture2DArray.MipSlice = 0;
		RTVDesc.Texture2DArray.FirstArraySlice = 0;
		RTVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		UAVDesc.Texture2DArray.MipSlice = 0;
		UAVDesc.Texture2DArray.FirstArraySlice = 0;
		UAVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.MipLevels = NumMips;
		SRVDesc.Texture2DArray.MostDetailedMip = 0;
		SRVDesc.Texture2DArray.FirstArraySlice = 0;
		SRVDesc.Texture2DArray.ArraySize = (UINT)ArraySize;
	}
	else
	{
		RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		RTVDesc.Texture2D.MipSlice = 0;
		UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		UAVDesc.Texture2D.MipSlice = 0;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels = NumMips;
		SRVDesc.Texture2D.MostDetailedMip = 0;
	}

	if (m_SRVHandle.ptr == ~0ull)
	{
		m_RTVHandle = Graphics::g_pRTVDescriptorHeap->Append().GetCPUHandle();
		m_SRVHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	}

	ID3D12Resource* Resource = m_pResource.Get();
	Device->CreateRenderTargetView( Resource, &RTVDesc, m_RTVHandle );
	Device->CreateShaderResourceView( Resource, &SRVDesc, m_SRVHandle );
	for (uint32_t i = 0; i < NumMips; ++i)
	{
		if (m_UAVHandle[i].ptr == ~0ull)
			m_UAVHandle[i] = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
		Device->CreateUnorderedAccessView( Resource, nullptr, &UAVDesc, m_UAVHandle[i] );
		UAVDesc.Texture2D.MipSlice++;
	}
}

//--------------------------------------------------------------------------------------
// DepthBuffer
//--------------------------------------------------------------------------------------
DepthBuffer::DepthBuffer( FLOAT ClearDepth /* = .0f */, UINT8 ClearStencil /* = 0 */ )
	:m_ClearDepth( ClearDepth ), m_ClearStencil( ClearStencil )
{
	m_DSVHandle[0].ptr = ~0ull;
	m_DSVHandle[1].ptr = ~0ull;
	m_DSVHandle[2].ptr = ~0ull;
	m_DSVHandle[3].ptr = ~0ull;
	m_DepthSRVHandle.ptr = ~0ull;
	m_StencilSRVHandle.ptr = ~0ull;
}

void DepthBuffer::Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format,
	D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr /* = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN */ )
{
	D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D( Width, Height, 1, 1, Format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL );
	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Format;
	ClearValue.DepthStencil.Depth = m_ClearDepth;
	ClearValue.DepthStencil.Stencil = m_ClearStencil;
	CreateTextureResource( Graphics::g_device.Get(), Name, ResourceDesc, ClearValue, VidMemPtr );
	CreateDerivedViews( Graphics::g_device.Get(), Format );
}

void DepthBuffer::CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format )
{
	ID3D12Resource* Resource = m_pResource.Get();
	D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc;
	DSVDesc.Format = GetDSVFormat( Format );
	DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;
	if (m_DSVHandle[0].ptr == ~0ull)
	{
		m_DSVHandle[0] = Graphics::g_pDSVDescriptorHeap->Append().GetCPUHandle();
		m_DSVHandle[1] = Graphics::g_pDSVDescriptorHeap->Append().GetCPUHandle();
	}
	DSVDesc.Flags = D3D12_DSV_FLAG_NONE;
	Device->CreateDepthStencilView( Resource, &DSVDesc, m_DSVHandle[0] );
	DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
	Device->CreateDepthStencilView( Resource, &DSVDesc, m_DSVHandle[1] );

	DXGI_FORMAT stencilReadFormat = GetStencilFormat( Format );
	if (stencilReadFormat != DXGI_FORMAT_UNKNOWN)
	{
		if (m_DSVHandle[2].ptr == ~0ull)
		{
			m_DSVHandle[2] = Graphics::g_pDSVDescriptorHeap->Append().GetCPUHandle();
			m_DSVHandle[3] = Graphics::g_pDSVDescriptorHeap->Append().GetCPUHandle();
		}
		DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		Device->CreateDepthStencilView( Resource, &DSVDesc, m_DSVHandle[2] );
		DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		Device->CreateDepthStencilView( Resource, &DSVDesc, m_DSVHandle[3] );
	}
	else
	{
		m_DSVHandle[2] = m_DSVHandle[0];
		m_DSVHandle[3] = m_DSVHandle[1];
	}

	if (m_DepthSRVHandle.ptr == ~0ull)
		m_DepthSRVHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = GetDepthFormat( Format );
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Resource, &SRVDesc, m_DepthSRVHandle );

	if (stencilReadFormat != DXGI_FORMAT_UNKNOWN)
	{
		if (m_StencilSRVHandle.ptr == ~0ull)
		{
			m_StencilSRVHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
		}
		SRVDesc.Format = stencilReadFormat;
		Device->CreateShaderResourceView( Resource, &SRVDesc, m_StencilSRVHandle );
	}
}

//--------------------------------------------------------------------------------------
// VolumeTexture
//--------------------------------------------------------------------------------------
VolumeTexture::VolumeTexture()
	: m_Width( 0 ), m_Height( 0 ), m_Depth( 0 )
{
	m_SRVHandle.ptr = ~0ull;
	m_UAVHandle.ptr = ~0ull;
}

void VolumeTexture::Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Depth, DXGI_FORMAT Format )
{
	D3D12_RESOURCE_DESC Desc = DescribeTex3D( Width, Height, Depth, Format );

	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	HRESULT hr;
	V( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &Desc,
		D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS( &m_pResource ) ) );
	CreateDerivedViews();
}

D3D12_RESOURCE_DESC VolumeTexture::DescribeTex3D( uint32_t Width, uint32_t Height, uint32_t Depth, DXGI_FORMAT Format )
{
	m_Width = Width;
	m_Height = Height;
	m_Depth = Depth;
	m_Format = Format;

	D3D12_RESOURCE_DESC Desc = {};
	Desc.Alignment = 0;
	Desc.DepthOrArraySize = (UINT16)Depth;
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	Desc.Flags = (D3D12_RESOURCE_FLAGS)(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Desc.Format = GetBaseFormat();
	Desc.Height = (UINT)Height;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	Desc.MipLevels = 1;
	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Width = (UINT64)Width;
	return Desc;
}

void VolumeTexture::CreateDerivedViews()
{
	ID3D12Resource* Resource = m_pResource.Get();
	if (m_SRVHandle.ptr == ~0ull)
		m_SRVHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateShaderResourceView( Resource, nullptr, m_SRVHandle );
	if (m_UAVHandle.ptr == ~0ull)
		m_UAVHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateUnorderedAccessView( Resource, nullptr, nullptr, m_UAVHandle );
}

DXGI_FORMAT VolumeTexture::GetBaseFormat()
{
	switch (m_Format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_TYPELESS;

	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_TYPELESS;

		// 32-bit Z w/ Stencil
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		return DXGI_FORMAT_R32G8X24_TYPELESS;

		// No Stencil
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_TYPELESS;

		// 24-bit Z
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;

		// 16-bit Z w/o Stencil
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
		return DXGI_FORMAT_R16_TYPELESS;

	default:
		return m_Format;
	}
}

DXGI_FORMAT VolumeTexture::GetUAVFormat()
{
	switch (m_Format)
	{
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return DXGI_FORMAT_R8G8B8A8_UNORM;

	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8A8_UNORM;

	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return DXGI_FORMAT_B8G8R8X8_UNORM;

	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_R32_FLOAT:
		return DXGI_FORMAT_R32_FLOAT;

#ifdef _DEBUG
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_D16_UNORM:

		PRINTWARN( "Requested a UAV format for a depth stencil format." );
#endif

	default:
		return m_Format;
	}
}

//--------------------------------------------------------------------------------------
// GpuBuffer
//--------------------------------------------------------------------------------------
void GpuBuffer::Destroy()
{
	GpuResource::Destroy();
}

void GpuBuffer::Create( const std::wstring& Name, uint32_t NumElements, uint32_t ElementSize, const void* InitData /* = nullptr */ )
{
	m_ElementCount = NumElements;
	m_ElementSize = ElementSize;
	m_BufferSize = NumElements*ElementSize;

	D3D12_RESOURCE_DESC ResourceDesc = DescribeBuffer();

	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	HRESULT hr;
	V( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE,
		&ResourceDesc, m_UsageState, nullptr, IID_PPV_ARGS( &m_pResource ) ) );

	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

	if (InitData)
		CommandContext::InitializeBuffer( *this, InitData, m_BufferSize );

#ifdef RELEASE
	( Name );
#else
	m_pResource->SetName( Name.c_str() );
#endif

	CreateDerivedViews();
}

void GpuBuffer::CreatePlaced( const std::wstring& Name, ID3D12Heap* BackingHeap, uint32_t HeapOffset, uint32_t NumElements,
	uint32_t ElementSize, const void* InitData /* = nullptr */ )
{
	m_ElementCount = NumElements;
	m_ElementSize = ElementSize;
	m_BufferSize = NumElements * ElementSize;

	HRESULT hr;
	D3D12_RESOURCE_DESC ResourceDesc = DescribeBuffer();
	m_UsageState = D3D12_RESOURCE_STATE_COMMON;
	V( Graphics::g_device->CreatePlacedResource( BackingHeap, HeapOffset, &ResourceDesc, m_UsageState,
		nullptr, IID_PPV_ARGS( &m_pResource ) ) );
	m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();
	if (InitData)
		CommandContext::InitializeBuffer( *this, InitData, m_BufferSize );
#ifdef RELEASE
	( Name );
#else
	m_pResource->SetName( Name.c_str() );
#endif
	CreateDerivedViews();
}

D3D12_CPU_DESCRIPTOR_HANDLE GpuBuffer::CreateConstantBufferView( uint32_t Offset, uint32_t Size ) const
{
	ASSERT( Offset + Size <= m_BufferSize );
	Size = AlignUp( Size, 16 );
	D3D12_CONSTANT_BUFFER_VIEW_DESC CBVDesc;
	CBVDesc.BufferLocation = m_GpuVirtualAddress + (size_t)Offset;
	CBVDesc.SizeInBytes = Size;

	D3D12_CPU_DESCRIPTOR_HANDLE hCBV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateConstantBufferView( &CBVDesc, hCBV );
	return hCBV;
}

GpuBuffer::GpuBuffer()
	:m_BufferSize( 0 ), m_ElementCount( 0 ), m_ElementSize( 0 )
{
	m_ResourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	m_UAV.ptr = ~0ull;
	m_SRV.ptr = ~0ull;
}

D3D12_RESOURCE_DESC GpuBuffer::DescribeBuffer()
{
	ASSERT( m_BufferSize != 0 );
	D3D12_RESOURCE_DESC Desc = {};
	Desc.Alignment = 0;
	Desc.DepthOrArraySize = 1;
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Desc.Flags = m_ResourceFlags;
	Desc.Format = DXGI_FORMAT_UNKNOWN;
	Desc.Height = 1;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Desc.MipLevels = 1;
	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Width = (UINT64)m_BufferSize;
	return Desc;
}

//--------------------------------------------------------------------------------------
// ByteAddressBuffer
//--------------------------------------------------------------------------------------
void ByteAddressBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = (UINT)m_BufferSize / 4;
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	if (m_SRV.ptr == ~0ull)
		m_SRV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateShaderResourceView( m_pResource.Get(), &SRVDesc, m_SRV );

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	UAVDesc.Buffer.NumElements = (UINT)m_BufferSize / 4;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

	if (m_UAV.ptr == ~0ull)
		m_UAV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateUnorderedAccessView( m_pResource.Get(), nullptr, &UAVDesc, m_UAV );
}

//--------------------------------------------------------------------------------------
// StructureBuffer
//--------------------------------------------------------------------------------------
void StructuredBuffer::Destroy()
{
	m_CounterBuffer.Destroy();
	GpuBuffer::Destroy();
}

void StructuredBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = m_ElementCount;
	SRVDesc.Buffer.StructureByteStride = m_ElementSize;
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (m_SRV.ptr == ~0ull)
		m_SRV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateShaderResourceView( m_pResource.Get(), &SRVDesc, m_SRV );

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.Buffer.CounterOffsetInBytes = 0;
	UAVDesc.Buffer.NumElements = m_ElementCount;
	UAVDesc.Buffer.StructureByteStride = m_ElementSize;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	m_CounterBuffer.Create( L"StructuredBuffer::Counter", 1, 4 );

	if (m_UAV.ptr == ~0ull)
		m_UAV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateUnorderedAccessView( m_pResource.Get(), m_CounterBuffer.GetResource(), &UAVDesc, m_UAV );
}

ByteAddressBuffer& StructuredBuffer::GetCounterBuffer()
{
	return m_CounterBuffer;
}

const D3D12_CPU_DESCRIPTOR_HANDLE& StructuredBuffer::GetCounterSRV( CommandContext& Context )
{
	Context.TransitionResource( m_CounterBuffer, D3D12_RESOURCE_STATE_GENERIC_READ );
	return m_CounterBuffer.GetSRV();
}

const D3D12_CPU_DESCRIPTOR_HANDLE& StructuredBuffer::GetCounterUAV( CommandContext& Context )
{
	Context.TransitionResource( m_CounterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
	return m_CounterBuffer.GetUAV();
}

//--------------------------------------------------------------------------------------
// TypedBuffer
//--------------------------------------------------------------------------------------
void TypedBuffer::CreateDerivedViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = m_DataFormat;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = m_ElementCount;
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	if (m_SRV.ptr == ~0ull)
		m_SRV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateShaderResourceView( m_pResource.Get(), &SRVDesc, m_SRV );

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = m_DataFormat;
	UAVDesc.Buffer.NumElements = m_ElementCount;
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	if (m_UAV.ptr == ~0ull)
		m_UAV = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateUnorderedAccessView( m_pResource.Get(), nullptr, &UAVDesc, m_UAV );
}

//--------------------------------------------------------------------------------------
// Texture
//--------------------------------------------------------------------------------------
Texture::Texture()
{
	m_hCpuDescriptorHandle.ptr = ~0ull;
}

Texture::Texture( D3D12_CPU_DESCRIPTOR_HANDLE Handle )
	:m_hCpuDescriptorHandle( Handle )
{
}

void Texture::Create( size_t Width, size_t Height, DXGI_FORMAT Format, const void* InitData )
{
	m_UsageState = D3D12_RESOURCE_STATE_COMMON;

	D3D12_RESOURCE_DESC textDesc = {};
	textDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textDesc.Width = Width;
	textDesc.Height = (UINT)Height;
	textDesc.DepthOrArraySize = 1;
	textDesc.MipLevels = 1;
	textDesc.Format = Format;
	textDesc.SampleDesc.Count = 1;
	textDesc.SampleDesc.Quality = 0;
	textDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	textDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	HRESULT hr;
	V( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &textDesc,
		m_UsageState, nullptr, IID_PPV_ARGS( m_pResource.ReleaseAndGetAddressOf() ) ) );
	m_pResource->SetName( L"Texture" );

	D3D12_SUBRESOURCE_DATA texResource;
	texResource.pData = InitData;
	texResource.RowPitch = Width * BitsPerPixel( Format ) / 8;
	texResource.SlicePitch = texResource.RowPitch * Height;

	CommandContext::InitializeTexture( *this, 1, &texResource );

	if (m_hCpuDescriptorHandle.ptr == ~0ull)
		m_hCpuDescriptorHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateShaderResourceView( m_pResource.Get(), nullptr, m_hCpuDescriptorHandle );
}

bool Texture::CreateFromFIle( const wchar_t* FileName, bool sRGB )
{
	if (m_hCpuDescriptorHandle.ptr == ~0ull)
		m_hCpuDescriptorHandle = Graphics::g_pCSUDescriptorHeap->Append().GetCPUHandle();
	HRESULT hr = CreateDDSTextureFromFile( Graphics::g_device.Get(), FileName, 0, sRGB, &m_pResource, m_hCpuDescriptorHandle );
	return SUCCEEDED( hr );
}

void Texture::Destroy()
{
	GpuResource::Destroy();
}

const D3D12_CPU_DESCRIPTOR_HANDLE& Texture::GetSRV() const
{
	return m_hCpuDescriptorHandle;
}

bool Texture::operator!()
{
	return m_hCpuDescriptorHandle.ptr == 0;
}