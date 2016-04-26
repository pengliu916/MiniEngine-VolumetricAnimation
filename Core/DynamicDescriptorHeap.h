#pragma once
#include <vector>
#include <queue>
#include "DescriptorHeap.h"


class DynamicDescriptorHeap
{
public:
	DynamicDescriptorHeap( CommandContext& OwningContext );
	~DynamicDescriptorHeap();

	static void Initialize();
	static void Shutdown();
	static void DestroyAll();
	static uint32_t GetDescriptorSize();

	void CleanupUsedHeaps( uint64_t fenceValue );
	void SetGraphicsDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] );
	void SetComputeDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] );
	D3D12_GPU_DESCRIPTOR_HANDLE UploadDirect( D3D12_CPU_DESCRIPTOR_HANDLE Handles );
	void ParseGraphicsRootSignature( const RootSignature& RootSig );
	void ParseComputeRootSignature( const RootSignature& RootSig );
	void CommitGraphicsRootDescriptorTables( ID3D12GraphicsCommandList* CmdList );
	void CommitComputeRootDescriptorTables( ID3D12GraphicsCommandList* CmdList );

private:
	struct DescriptorTableCache
	{
		DescriptorTableCache();
		uint32_t AssignedHandlesBitMap;
		D3D12_CPU_DESCRIPTOR_HANDLE* TableStart;
		uint32_t TableSize;
	};

	struct DescriptorHandleCache
	{
		DescriptorHandleCache();
		void ClearCache();
		uint32_t ComputeStagedSize();
		void CopyAndBindStaleTables( DescriptorHandle DestHandleStart, ID3D12GraphicsCommandList* CmdList,
			void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::*SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) );
		void UnbindAllValid();
		void StageDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] );
		void ParseRootSignature( const RootSignature& RootSig );

		static const uint32_t kMaxNumDescriptors = 256;
		static const uint32_t kMaxNumDescriptorTables = 16;

		uint32_t m_RootDescriptorTablesBitMap;
		uint32_t m_StaleRootParamsBitMap;
		uint32_t m_MaxCachedDescriptors;

		DescriptorTableCache m_RootDescriptorTable[kMaxNumDescriptorTables];
		D3D12_CPU_DESCRIPTOR_HANDLE m_HandleCache[kMaxNumDescriptors];
	};

	static ID3D12DescriptorHeap* RequestDescriptorHeap();
	static void DiscardDescriptorHeaps( uint64_t FenceValueForReset, const std::vector<ID3D12DescriptorHeap*>& UsedHeaps );

	bool HasSpace( uint32_t Count );
	void RetireCurrentHeap();
	void RetireUsedHeaps( uint64_t FenceValue );
	ID3D12DescriptorHeap* GetHeapPointer();
	DescriptorHandle Allocate( UINT Count );
	void CopyAndBindStagedTables( DescriptorHandleCache& HandleCache, ID3D12GraphicsCommandList* CmdList,
		void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::*SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) );
	void UnbindAllValid();

	static const uint32_t kNumDescriptorsPerHeap = 1024;
	static CRITICAL_SECTION sm_CS;
	static std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> sm_DescriptorHeapPool;
	static std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> sm_RetiredDescriptorHeaps;
	static std::queue<ID3D12DescriptorHeap*> sm_AvailableDescriptorHeaps;
	static uint32_t sm_DescriptorSize;

	DescriptorHandleCache m_GraphicsHandleCache;
	DescriptorHandleCache m_ComputeHandleCache;
	CommandContext& m_OwningContext;
	ID3D12DescriptorHeap* m_CurrentHeapPtr;
	uint32_t m_CurrentOffset;
	DescriptorHandle m_FirstDescriptor;
	std::vector<ID3D12DescriptorHeap*> m_RetiredHeaps;
};

inline void DynamicDescriptorHeap::CommitGraphicsRootDescriptorTables( ID3D12GraphicsCommandList* CmdList )
{
	if (m_GraphicsHandleCache.m_StaleRootParamsBitMap != 0)
		CopyAndBindStagedTables( m_GraphicsHandleCache, CmdList, &ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable );
}

inline void DynamicDescriptorHeap::CommitComputeRootDescriptorTables( ID3D12GraphicsCommandList* CmdList )
{
	if (m_ComputeHandleCache.m_StaleRootParamsBitMap != 0)
		CopyAndBindStagedTables( m_ComputeHandleCache, CmdList, &ID3D12GraphicsCommandList::SetComputeRootDescriptorTable );
}

inline ID3D12DescriptorHeap* DynamicDescriptorHeap::GetHeapPointer()
{
	if (m_CurrentHeapPtr == nullptr)
	{
		ASSERT( m_CurrentOffset == 0 );
		m_CurrentHeapPtr = RequestDescriptorHeap();
		m_FirstDescriptor = DescriptorHandle( m_CurrentHeapPtr->GetCPUDescriptorHandleForHeapStart(), m_CurrentHeapPtr->GetGPUDescriptorHandleForHeapStart() );
	}
	return m_CurrentHeapPtr;
}