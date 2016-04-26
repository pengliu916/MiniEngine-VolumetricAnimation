#include "LibraryHeader.h"
#include "Utility.h"
#include "DX12Framework.h"
#include "Graphics.h"
#include "CmdListMngr.h"

//--------------------------------------------------------------------------------------
// CommandAllocatorPool
//--------------------------------------------------------------------------------------
CommandAllocatorPool::CommandAllocatorPool( D3D12_COMMAND_LIST_TYPE Type ) :
	m_cCommandListType( Type ),
	m_pDevice( nullptr )
{
	InitializeCriticalSection( &m_AllocatorCS );
}

CommandAllocatorPool::~CommandAllocatorPool()
{
	Shutdown();
	DeleteCriticalSection( &m_AllocatorCS );
}

void CommandAllocatorPool::Create( ID3D12Device* pDevice )
{
	m_pDevice = pDevice;
}

void CommandAllocatorPool::Shutdown()
{
	for (size_t i = 0; i < m_AllocatorPool.size(); ++i)
		m_AllocatorPool[i]->Release();
	m_AllocatorPool.clear();
}

ID3D12CommandAllocator* CommandAllocatorPool::RequestAllocator( uint64_t CompletedFenceValue )
{
	CriticalSectionScope LockGuard( &m_AllocatorCS );

	HRESULT hr;
	ID3D12CommandAllocator* pAllocator = nullptr;

	if (!m_ReadyAllocators.empty())
	{
		std::pair<uint64_t, ID3D12CommandAllocator*>& AllocatorPair = m_ReadyAllocators.front();
		if (AllocatorPair.first <= CompletedFenceValue)
		{
			pAllocator = AllocatorPair.second;
			V( pAllocator->Reset() );
			m_ReadyAllocators.pop();
		}
		Graphics::g_stats.allocatorReady[m_cCommandListType] = (uint16_t)m_ReadyAllocators.size();
	}
	if (pAllocator == nullptr)
	{
		V( m_pDevice->CreateCommandAllocator( m_cCommandListType, IID_PPV_ARGS( &pAllocator ) ) );
		wchar_t AllocatorName[32];
		swprintf( AllocatorName, 32, L"CommandAllocator %zu", m_AllocatorPool.size() );
		pAllocator->SetName( AllocatorName );
		m_AllocatorPool.push_back( pAllocator );
		Graphics::g_stats.allocatorCreated[m_cCommandListType] = (uint16_t)m_AllocatorPool.size();
	}

	return pAllocator;
}

void CommandAllocatorPool::DiscardAllocator( uint64_t FenceValue, ID3D12CommandAllocator* Allocator )
{
	CriticalSectionScope LockGuard( &m_AllocatorCS );
	m_ReadyAllocators.push( std::make_pair( FenceValue, Allocator ) );
}

//--------------------------------------------------------------------------------------
// CommandQueue
//--------------------------------------------------------------------------------------
CommandQueue::CommandQueue( D3D12_COMMAND_LIST_TYPE Type ) :
	m_Type( Type ),
	m_CommandQueue( nullptr ),
	m_pFence( nullptr ),
	m_NextFenceValue( (uint64_t)Type << 56 | 1 ),
	m_LastCompletedFenceValue( (uint64_t)Type << 56 ),
	m_AllocatorPool( Type )
{
	InitializeCriticalSection( &m_FenceCS );
	InitializeCriticalSection( &m_EventCS );
}

CommandQueue::~CommandQueue()
{
	Shutdown();
	DeleteCriticalSection( &m_FenceCS );
	DeleteCriticalSection( &m_EventCS );
}

void CommandQueue::Create( ID3D12Device* pDevice )
{
	ASSERT( pDevice != nullptr );
	ASSERT( !IsReady() );
	ASSERT( m_AllocatorPool.Size() == 0 );

	HRESULT hr;

	D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
	QueueDesc.Type = m_Type;
	QueueDesc.NodeMask = 1;
	V( pDevice->CreateCommandQueue( &QueueDesc, IID_PPV_ARGS( &m_CommandQueue ) ) );
	m_CommandQueue->SetName( L"m_CommandQueue" );

	V( pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_pFence ) ) );
	m_pFence->SetName( L"m_pFence" );
	m_pFence->Signal( (uint64_t)m_Type << 56 );

	m_FenceEventHandle = CreateEvent( nullptr, false, false, nullptr );
	ASSERT( m_FenceEventHandle != INVALID_HANDLE_VALUE );

	m_AllocatorPool.Create( pDevice );
	ASSERT( IsReady() );
}

void CommandQueue::Shutdown()
{
	if (m_CommandQueue == nullptr)
		return;
	m_AllocatorPool.Shutdown();
	CloseHandle( m_FenceEventHandle );

	m_pFence->Release();
	m_CommandQueue->Release();
	m_CommandQueue = nullptr;
}

uint64_t CommandQueue::IncrementFence()
{
	CriticalSectionScope LockGuard( &m_FenceCS );
	m_CommandQueue->Signal( m_pFence, m_NextFenceValue );
	return m_NextFenceValue++;
}

bool CommandQueue::IsFenceCompelete( uint64_t FenceValue )
{
	if (FenceValue > m_LastCompletedFenceValue)
		m_LastCompletedFenceValue = max( m_LastCompletedFenceValue, m_pFence->GetCompletedValue() );

	return FenceValue <= m_LastCompletedFenceValue;
}

void CommandQueue::WaitForFence( uint64_t FenceValue )
{
	if (IsFenceCompelete( FenceValue ))
		return;
	{
		CriticalSectionScope LockGuard( &m_EventCS );
		m_pFence->SetEventOnCompletion( FenceValue, m_FenceEventHandle );
		int64_t startTick, endTick;
		LARGE_INTEGER currentTick;
		QueryPerformanceCounter( &currentTick );
		startTick = static_cast<int64_t>(currentTick.QuadPart);
		WaitForSingleObject( m_FenceEventHandle, INFINITE );
		QueryPerformanceCounter( &currentTick );
		endTick = static_cast<int64_t>(currentTick.QuadPart);

		Graphics::g_stats.cpuStallCountPerFrame++;
		Graphics::g_stats.cpuStallTimePerFrame += (double)(endTick - startTick) / Core::g_tickesPerSecond * 1000.f;
		m_LastCompletedFenceValue = FenceValue;
	}
}

void CommandQueue::WaitforIdle()
{
	WaitForFence( m_NextFenceValue - 1 );
}

ID3D12CommandQueue* CommandQueue::GetCommandQueue()
{
	return m_CommandQueue;
}

uint64_t CommandQueue::ExecuteCommandList( ID3D12CommandList* List )
{
	CriticalSectionScope LockGuard( &m_FenceCS );
	HRESULT hr;
	V( ((ID3D12GraphicsCommandList*)List)->Close() );

	m_CommandQueue->ExecuteCommandLists( 1, &List );
	m_CommandQueue->Signal( m_pFence, m_NextFenceValue );
	return m_NextFenceValue++;
}

ID3D12CommandAllocator* CommandQueue::RequestAllocator()
{
	uint64_t CompletedFence = m_pFence->GetCompletedValue();
	return m_AllocatorPool.RequestAllocator( CompletedFence );
}

void CommandQueue::DiscardAllocator( uint64_t FenceValueForReset, ID3D12CommandAllocator* Allocator )
{
	m_AllocatorPool.DiscardAllocator( FenceValueForReset, Allocator );
}
//--------------------------------------------------------------------------------------
// CmdListMngr
//--------------------------------------------------------------------------------------
CmdListMngr::CmdListMngr() :
	m_pDevice( nullptr ),
	m_GraphicsQueue( D3D12_COMMAND_LIST_TYPE_DIRECT ),
	m_ComputeQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE ),
	m_CopyQueue( D3D12_COMMAND_LIST_TYPE_COPY )
{
}

CmdListMngr::~CmdListMngr()
{
	Shutdown();
}

void CmdListMngr::Create( ID3D12Device* pDevice )
{
	ASSERT( pDevice != nullptr );
	m_pDevice = pDevice;
#ifndef RELEASE
	pDevice->SetStablePowerState( TRUE );
#endif
	m_GraphicsQueue.Create( pDevice );
	m_ComputeQueue.Create( pDevice );
	m_CopyQueue.Create( pDevice );
}

void CmdListMngr::Shutdown()
{
}

CommandQueue& CmdListMngr::GetGraphicsQueue()
{
	return m_GraphicsQueue;
}

CommandQueue& CmdListMngr::GetComputeQueue()
{
	return m_ComputeQueue;
}

CommandQueue& CmdListMngr::GetCopyQueue()
{
	return m_CopyQueue;
}

CommandQueue& CmdListMngr::GetQueue( D3D12_COMMAND_LIST_TYPE Type /* = D3D12_COMMAND_LIST_TYPE_DIRECT */ )
{
	switch (Type)
	{
	case D3D12_COMMAND_LIST_TYPE_COMPUTE: return m_ComputeQueue;
	case D3D12_COMMAND_LIST_TYPE_COPY: return m_CopyQueue;
	default: return m_GraphicsQueue;
	}
}

ID3D12CommandQueue* CmdListMngr::GetCommandQueue()
{
	return m_GraphicsQueue.GetCommandQueue();
}

void CmdListMngr::CreateNewCommandList( D3D12_COMMAND_LIST_TYPE Type, ID3D12GraphicsCommandList** List, ID3D12CommandAllocator** Allocator )
{
	ASSERT( Type != D3D12_COMMAND_LIST_TYPE_BUNDLE ); // Bundles are not yet supported
	switch (Type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT: *Allocator = m_GraphicsQueue.RequestAllocator(); break;
	case D3D12_COMMAND_LIST_TYPE_BUNDLE: break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE: *Allocator = m_ComputeQueue.RequestAllocator(); break;
	case D3D12_COMMAND_LIST_TYPE_COPY: *Allocator = m_CopyQueue.RequestAllocator(); break;
	}
	HRESULT hr;
	V( m_pDevice->CreateCommandList( 1, Type, *Allocator, nullptr, IID_PPV_ARGS( List ) ) );
	(*List)->SetName( L"CommandList" );
}

bool CmdListMngr::IsFenceComplete( uint64_t FenceValue )
{
	return GetQueue( D3D12_COMMAND_LIST_TYPE( FenceValue >> 56 ) ).IsFenceCompelete( FenceValue );
}

void CmdListMngr::WaitForFence( uint64_t FenceValue )
{
	GetQueue( (D3D12_COMMAND_LIST_TYPE)(FenceValue >> 56) ).WaitForFence( FenceValue );
}

void CmdListMngr::IdleGPU()
{
	m_GraphicsQueue.WaitforIdle();
	m_ComputeQueue.WaitforIdle();
	m_CopyQueue.WaitforIdle();
}