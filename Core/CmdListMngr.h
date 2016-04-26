#pragma once

#include <vector>
#include <queue>

//--------------------------------------------------------------------------------------
// CommandAllocatorPool
//--------------------------------------------------------------------------------------
class CommandAllocatorPool
{
public:
	CommandAllocatorPool() = delete;
	CommandAllocatorPool( D3D12_COMMAND_LIST_TYPE Type );
	~CommandAllocatorPool();

	void Create( ID3D12Device* pDevice );
	void Shutdown();

	ID3D12CommandAllocator* RequestAllocator( uint64_t CompletedFenceValue );
	void DiscardAllocator( uint64_t FenceValue, ID3D12CommandAllocator* Allocator );

	inline size_t Size() { return m_AllocatorPool.size(); }

private:
	const D3D12_COMMAND_LIST_TYPE m_cCommandListType;

	ID3D12Device* m_pDevice;
	std::vector<ID3D12CommandAllocator*> m_AllocatorPool;
	std::queue<std::pair<uint64_t, ID3D12CommandAllocator*>> m_ReadyAllocators;
	CRITICAL_SECTION m_AllocatorCS;
};

//--------------------------------------------------------------------------------------
// CommandQueue
//--------------------------------------------------------------------------------------
class CommandQueue
{
	friend class CmdListMngr;
	friend class CommandContext;

public:
	CommandQueue() = delete;
	CommandQueue( D3D12_COMMAND_LIST_TYPE Type );
	~CommandQueue();

	void Create( ID3D12Device* pDevice );
	void Shutdown();

	inline bool IsReady()
	{
		return m_CommandQueue != nullptr;
	}

	uint64_t IncrementFence();
	bool IsFenceCompelete( uint64_t FenceValue );
	void WaitForFence( uint64_t FenceValue );
	void WaitforIdle();

	ID3D12CommandQueue* GetCommandQueue();

private:
	uint64_t ExecuteCommandList( ID3D12CommandList* List );
	ID3D12CommandAllocator* RequestAllocator();
	void DiscardAllocator( uint64_t FenceValueForReset, ID3D12CommandAllocator* Allocator );

	ID3D12CommandQueue* m_CommandQueue;
	const D3D12_COMMAND_LIST_TYPE m_Type;
	CommandAllocatorPool m_AllocatorPool;

	CRITICAL_SECTION m_FenceCS;
	CRITICAL_SECTION m_EventCS;

	ID3D12Fence* m_pFence;
	uint64_t m_NextFenceValue;
	uint64_t m_LastCompletedFenceValue;
	HANDLE m_FenceEventHandle;
};

//--------------------------------------------------------------------------------------
// CmdListMngr
//--------------------------------------------------------------------------------------
class CmdListMngr
{
public:
	CmdListMngr();
	~CmdListMngr();

	void Create( ID3D12Device* pDevice );
	void Shutdown();

	CommandQueue& GetGraphicsQueue();
	CommandQueue& GetComputeQueue();
	CommandQueue& GetCopyQueue();
	CommandQueue& GetQueue( D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT );
	ID3D12CommandQueue* GetCommandQueue();

	void CreateNewCommandList( D3D12_COMMAND_LIST_TYPE Type,
		ID3D12GraphicsCommandList** List,
		ID3D12CommandAllocator** Allocator );
	bool IsFenceComplete( uint64_t FenceValue );
	void WaitForFence( uint64_t FenceValue );
	void IdleGPU();

private:
	ID3D12Device* m_pDevice;

	CommandQueue m_GraphicsQueue;
	CommandQueue m_ComputeQueue;
	CommandQueue m_CopyQueue;
};