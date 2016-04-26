
#include "LibraryHeader.h"
#include "Graphics.h"
#include "DescriptorHeap.h"
#include "Utility.h"
#include "SamplerMngr.h"

#include <unordered_map>

namespace
{
	std::unordered_map<size_t, D3D12_CPU_DESCRIPTOR_HANDLE> s_SamplerCache;
}

//--------------------------------------------------------------------------------------
// SamplerDesc
//--------------------------------------------------------------------------------------
SamplerDesc::SamplerDesc()
{
	Filter = D3D12_FILTER_ANISOTROPIC;
	AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	MipLODBias = 0.0f;
	MaxAnisotropy = 16;
	ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	BorderColor[0] = 1.0f;
	BorderColor[1] = 1.0f;
	BorderColor[2] = 1.0f;
	BorderColor[3] = 1.0f;
	MinLOD = 0.0f;
	MaxLOD = D3D12_FLOAT32_MAX;
}

void SamplerDesc::SetTextureAddressMode( D3D12_TEXTURE_ADDRESS_MODE AddressMode )
{
	AddressU = AddressMode;
	AddressV = AddressMode;
	AddressW = AddressMode;
}

void SamplerDesc::SetBorderColor( DirectX::XMVECTOR BorderCol )
{
	BorderColor[0] = DirectX::XMVectorGetX( BorderCol );
	BorderColor[1] = DirectX::XMVectorGetY( BorderCol );
	BorderColor[2] = DirectX::XMVectorGetZ( BorderCol );
	BorderColor[3] = DirectX::XMVectorGetW( BorderCol );
}

//--------------------------------------------------------------------------------------
// SamplerMngr
//--------------------------------------------------------------------------------------
SamplerDescriptor::SamplerDescriptor() {}

SamplerDescriptor::SamplerDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor )
	:m_hCpuDescriptorHandle( hCpuDescriptor )
{
}

void SamplerDescriptor::Create( const D3D12_SAMPLER_DESC& Desc )
{
	size_t hashValue = HashState( &Desc );
	auto iter = s_SamplerCache.find( hashValue );
	if (iter != s_SamplerCache.end())
	{
		*this = SamplerDescriptor( iter->second );
		return;
	}
	m_hCpuDescriptorHandle = Graphics::g_pSMPDescriptorHeap->Append().GetCPUHandle();
	Graphics::g_device->CreateSampler( &Desc, m_hCpuDescriptorHandle );
}

D3D12_CPU_DESCRIPTOR_HANDLE SamplerDescriptor::GetCpuDescriptorHandle() const
{
	return m_hCpuDescriptorHandle;
}