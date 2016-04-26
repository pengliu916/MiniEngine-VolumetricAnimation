#pragma once

#include "GpuResource.h"
#include <vector>
#include <queue>

// Constant blocks must be multiples of 16 constants @ 16 bytes each
#define DEFAULT_ALIGN 256

struct DynAlloc
{
	DynAlloc( GpuResource& BaseResource, size_t ThisOffset, size_t ThisSize )
		: Buffer( BaseResource ), Offset( ThisOffset ), Size( ThisSize ) {}

	DynAlloc( DynAlloc&& one )
		:Buffer(std::move(one.Buffer)), Offset(one.Offset),Size(one.Size)
		,DataPtr(one.DataPtr),GpuAddress(one.GpuAddress){}

	DynAlloc& operator=( DynAlloc&& one ) = default;

	GpuResource&				Buffer;
	size_t						Offset;
	size_t						Size;
	void*						DataPtr;
	D3D12_GPU_VIRTUAL_ADDRESS	GpuAddress;
};

class LinearAllocationPage : public GpuResource
{
public:
	LinearAllocationPage( ID3D12Resource* pGfxResource, D3D12_RESOURCE_STATES Usage );
	~LinearAllocationPage();

	LinearAllocationPage& operator=( LinearAllocationPage const& ) = delete;
	LinearAllocationPage( LinearAllocationPage const& ) = delete;

	void*									m_CpuVirtualAddr;
	D3D12_GPU_VIRTUAL_ADDRESS				m_GpuVirtualAddr;
};

enum LinearAllocatorType
{
	kInvalidAllocator = -1,
	kGpuExclusive = 0,		// DEFAULT   GPU-writable (via UAV)
	kCpuWritable = 1,		// UPLOAD CPU-writable (but write combined)
	kNumAllocatorTypes
};

enum
{
	kGpuAllocatorPageSize = 0x10000,	// 64K
	kCpuAllocatorPageSize = 0x200000	// 2MB
};

class LinearAllocatorPageMngr
{
	friend class LinearAllocator;

public:
	LinearAllocatorPageMngr( LinearAllocatorType );
	~LinearAllocatorPageMngr();

	LinearAllocationPage* RequestPage();
	void DiscardPages( uint64_t FenceID, const std::vector<LinearAllocationPage*>& Pages );
	void Destory();

	//LinearAllocatorPageMngr( LinearAllocatorPageMngr const& ) = delete;
	LinearAllocatorPageMngr& operator= ( LinearAllocatorPageMngr const& ) = delete;

private:
	LinearAllocationPage* CreateNewPage();

	LinearAllocatorType										m_AllocationType;
	std::vector<std::unique_ptr<LinearAllocationPage>>		m_PagePool;
	std::queue<std::pair<uint64_t, LinearAllocationPage*>>	m_RetiredPages;
	std::queue<LinearAllocationPage*>						m_AvailablePages;
	CRITICAL_SECTION										m_CS;
};

class LinearAllocator
{
public:
	LinearAllocator( LinearAllocatorType Type );
	LinearAllocator( LinearAllocator const& ) = delete;
	LinearAllocator& operator= ( LinearAllocator const& ) = delete;

	DynAlloc Allocate( size_t SizeInByte, size_t Alignment = DEFAULT_ALIGN );
	void CleanupUsedPages( uint64_t FenceID );

	static void DestroyAll();

private:
	static LinearAllocatorPageMngr		sm_PageMngr[2];

	LinearAllocatorType					m_AllocationType;
	size_t								m_PageSize;
	size_t								m_CurOffset;
	LinearAllocationPage*				m_CurPage;
	std::vector<LinearAllocationPage*>	m_RetiredPages;
};

