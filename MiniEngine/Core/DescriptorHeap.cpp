#include "LibraryHeader.h"
#include "Graphics.h"
#include "Utility.h"
#include "DescriptorHeap.h"

//using namespace Microsoft::WRL;

DescriptorHandle::DescriptorHandle()
{
	mCPUHandle.ptr = ~0ull;
	mGPUHandle.ptr = ~0ull;
}

DescriptorHandle::DescriptorHandle( CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle, CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle )
	:mCPUHandle( cpuHandle ), mGPUHandle( gpuHandle )
{
}

DescriptorHandle::DescriptorHandle( D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle )
	: mCPUHandle( cpuHandle ), mGPUHandle( gpuHandle )
{
}

DescriptorHandle DescriptorHandle::operator+ ( INT OffsetScaledByDescriptorSize ) const
{
	DescriptorHandle ret = *this;
	ret += OffsetScaledByDescriptorSize;
	return ret;
}

void DescriptorHandle::operator += ( INT OffsetScaledByDescriptorSize )
{
	if (mCPUHandle.ptr != ~0ull)
		mCPUHandle.ptr += OffsetScaledByDescriptorSize;
	if (mGPUHandle.ptr != ~0ull)
		mGPUHandle.ptr += OffsetScaledByDescriptorSize;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE DescriptorHandle::GetCPUHandle()
{
	return mCPUHandle;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE DescriptorHandle::GetGPUHandle()
{
	return mGPUHandle;
}

DescriptorHeap::DescriptorHeap( ID3D12Device* device, UINT maxDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible )
	: mDevice( device ), mMaxSize( maxDescriptors )
{
	HRESULT hr;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NumDescriptors = mMaxSize;

	V( mDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &mHeap ) ) );

	mShaderVisible = shaderVisible;

	mCPUBegin = mHeap->GetCPUDescriptorHandleForHeapStart();
	mHandleIncrementSize = mDevice->GetDescriptorHandleIncrementSize( desc.Type );

	if (shaderVisible) {
		mGPUBegin = mHeap->GetGPUDescriptorHandleForHeapStart();
	}
}

DescriptorHandle DescriptorHeap::Append()
{
	ASSERT( mCurrentSize < mMaxSize );
	DescriptorHandle ret( CPU( mCurrentSize ), GPU( mCurrentSize ) );
	mCurrentSize++;
	return ret;
}