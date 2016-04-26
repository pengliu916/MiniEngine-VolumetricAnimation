#pragma once

class SamplerDesc :public D3D12_SAMPLER_DESC
{
public:
	SamplerDesc();
	void SetTextureAddressMode( D3D12_TEXTURE_ADDRESS_MODE AddressMode );
	void SetBorderColor( DirectX::XMVECTOR BorderCol );
};

class SamplerDescriptor
{
	friend class CommandContext;
public:
	SamplerDescriptor();
	SamplerDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor );
	void Create( const D3D12_SAMPLER_DESC& Desc );
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle() const;
protected:
	D3D12_CPU_DESCRIPTOR_HANDLE m_hCpuDescriptorHandle;
};