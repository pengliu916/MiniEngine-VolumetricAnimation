#pragma once
#include <vector>

class DescriptorHandle
{
	friend class DescriptorHeap;
public:
	DescriptorHandle();
	DescriptorHandle( CD3DX12_CPU_DESCRIPTOR_HANDLE, CD3DX12_GPU_DESCRIPTOR_HANDLE );
	DescriptorHandle( D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE );
	DescriptorHandle operator+ ( INT OffsetScaledByDescriptorSize ) const;
	void operator += ( INT OffsetScaledByDescriptorSize );
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUHandle();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUHandle();
private:
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCPUHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGPUHandle;
	bool hasGpuHandle;
};

class DescriptorHeap
{
public:

	// device must live for the lifetime of this object (no explicit refcount increment)
	DescriptorHeap( ID3D12Device* device, UINT maxDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible = false );

	// NOTE: Caller can fill in data at new handle and/or derived classes provide
	// specialized methods to do it in one step.
	DescriptorHandle Append();

	// Invalidates contents of any previous handles
	void Clear() { mCurrentSize = 0; }
	void Resize( UINT newSize ) { mCurrentSize = newSize; }
	UINT Size() const { return mCurrentSize; }

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
	UINT mHandleIncrementSize = 0;

protected:
	// Direct access to handles
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPU( UINT index = 0 ) const { return CD3DX12_CPU_DESCRIPTOR_HANDLE( mCPUBegin, index, mHandleIncrementSize ); }
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPU( UINT index = 0 ) const { return CD3DX12_GPU_DESCRIPTOR_HANDLE( mGPUBegin, index, mHandleIncrementSize ); }
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUEnd() const { return GPU( mCurrentSize ); }
	UINT HandleIncrementSize() const { return mHandleIncrementSize; }

	ID3D12Device* mDevice = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCPUBegin;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGPUBegin;
	UINT mCurrentSize = 0;
	UINT mMaxSize = 0;
	bool mShaderVisible;
};