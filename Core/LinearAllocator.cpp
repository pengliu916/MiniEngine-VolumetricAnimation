#include "LibraryHeader.h"
#include "Graphics.h"
#include "CmdListMngr.h"
#include "Utility.h"

using namespace std;
using namespace Microsoft::WRL;

#include "LinearAllocator.h"

LinearAllocatorPageMngr LinearAllocator::sm_PageMngr[2] = {LinearAllocatorPageMngr( kGpuExclusive ),LinearAllocatorPageMngr( kCpuWritable )};
//--------------------------------------------------------------------------------------
// LinearAllocationPage
//--------------------------------------------------------------------------------------

LinearAllocationPage::LinearAllocationPage( ID3D12Resource* pGfxResource, D3D12_RESOURCE_STATES Usage )
	: GpuResource()
{
	m_pResource.Attach( pGfxResource );
	m_UsageState = Usage;
	m_GpuVirtualAddr = m_pResource->GetGPUVirtualAddress();
	m_pResource->Map( 0, nullptr, &m_CpuVirtualAddr );
}

LinearAllocationPage::~LinearAllocationPage() { m_pResource->Unmap( 0, nullptr ); }

//--------------------------------------------------------------------------------------
// LinearAllocatorPageMngr
//--------------------------------------------------------------------------------------
LinearAllocatorPageMngr::LinearAllocatorPageMngr( LinearAllocatorType Type )
{
	InitializeCriticalSection( &m_CS );
	m_AllocationType = Type;
}

LinearAllocatorPageMngr::~LinearAllocatorPageMngr() { DeleteCriticalSection( &m_CS ); }

LinearAllocationPage* LinearAllocatorPageMngr::RequestPage()
{
	CriticalSectionScope LockGard( &m_CS );
	while (!m_RetiredPages.empty() && Graphics::g_cmdListMngr.IsFenceComplete( m_RetiredPages.front().first ))
	{
		m_AvailablePages.push( m_RetiredPages.front().second );
		m_RetiredPages.pop();
	}
	LinearAllocationPage* PagePtr = nullptr;
	if (!m_AvailablePages.empty())
	{
		PagePtr = m_AvailablePages.front();
		m_AvailablePages.pop();
	}
	else
	{
		PagePtr = CreateNewPage();
		m_PagePool.emplace_back( PagePtr );
	}
	return PagePtr;
}

void LinearAllocatorPageMngr::DiscardPages( uint64_t FenceValue, const vector<LinearAllocationPage*>& UsedPages )
{
	CriticalSectionScope LockGard( &m_CS );
	for (auto iter = UsedPages.begin(); iter != UsedPages.end(); ++iter)
		m_RetiredPages.push( make_pair( FenceValue, *iter ) );
}

LinearAllocationPage* LinearAllocatorPageMngr::CreateNewPage()
{
	HRESULT hr;
	ID3D12Resource* pBuffer;
	D3D12_RESOURCE_STATES DefaultUsage;
	if (m_AllocationType == kGpuExclusive)
	{
		DefaultUsage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		V( Graphics::g_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ), D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( kGpuAllocatorPageSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ),
			DefaultUsage, nullptr, IID_PPV_ARGS( &pBuffer ) ) );
	}
	else
	{
		DefaultUsage = D3D12_RESOURCE_STATE_GENERIC_READ;
		V( Graphics::g_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ), D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( kCpuAllocatorPageSize ),
			DefaultUsage, nullptr, IID_PPV_ARGS( &pBuffer ) ) );
	}
	pBuffer->SetName( L"LinearAllocator Page" );

	return new LinearAllocationPage( pBuffer, DefaultUsage );
}

void LinearAllocatorPageMngr::Destory() { m_PagePool.clear(); }


//--------------------------------------------------------------------------------------
// LinearAllocator
//--------------------------------------------------------------------------------------
LinearAllocator::LinearAllocator( LinearAllocatorType Type )
	:m_AllocationType( Type ), m_PageSize( 0 ), m_CurOffset( ~0ull ), m_CurPage( nullptr )
{
	ASSERT( Type > kInvalidAllocator && Type < kNumAllocatorTypes );
	m_PageSize = (Type == kGpuExclusive ? kGpuAllocatorPageSize : kCpuAllocatorPageSize);
}

DynAlloc LinearAllocator::Allocate( size_t SizeInByte, size_t Alignment )
{
	ASSERT( SizeInByte <= m_PageSize );
	const size_t AlignmentMask = Alignment - 1;
	// Assert that it's a power of two.
	ASSERT( (AlignmentMask & Alignment) == 0 );
	const size_t AlignedSize = AlignUpWithMask( SizeInByte, AlignmentMask );
	m_CurOffset = AlignUp( m_CurOffset, Alignment );
	if (m_CurOffset + AlignedSize > m_PageSize)
	{
		ASSERT( m_CurPage != nullptr );
		m_RetiredPages.push_back( m_CurPage );
		m_CurPage = nullptr;
	}
	if (m_CurPage == nullptr)
	{
		m_CurPage = sm_PageMngr[m_AllocationType].RequestPage();
		m_CurOffset = 0;
	}

	DynAlloc ret( *m_CurPage, m_CurOffset, AlignedSize );
	ret.GpuAddress = m_CurPage->m_GpuVirtualAddr + m_CurOffset;
	ret.DataPtr = (uint8_t*)m_CurPage->m_CpuVirtualAddr + m_CurOffset;

	m_CurOffset += AlignedSize;

	return ret;
}

void LinearAllocator::CleanupUsedPages( uint64_t FenceID )
{
	if (m_CurPage == nullptr)
		return;

	m_RetiredPages.push_back( m_CurPage );
	m_CurPage = nullptr;
	m_CurOffset = 0;

	sm_PageMngr[m_AllocationType].DiscardPages( FenceID, m_RetiredPages );
	m_RetiredPages.clear();
}

void LinearAllocator::DestroyAll()
{
	sm_PageMngr[0].Destory();
	sm_PageMngr[1].Destory();
}