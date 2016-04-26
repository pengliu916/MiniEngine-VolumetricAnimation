#include "LibraryHeader.h"
#include "CommandContext.h"
#include "Graphics.h"
#include "CmdListMngr.h"
#include "RootSignature.h"
#include "Utility.h"
#include "DynamicDescriptorHeap.h"
#include <intrin.h>

#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanForward64)

CRITICAL_SECTION DynamicDescriptorHeap::sm_CS;
std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> DynamicDescriptorHeap::sm_DescriptorHeapPool;
std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> DynamicDescriptorHeap::sm_RetiredDescriptorHeaps;
std::queue<ID3D12DescriptorHeap*> DynamicDescriptorHeap::sm_AvailableDescriptorHeaps;
uint32_t DynamicDescriptorHeap::sm_DescriptorSize = 0;

DynamicDescriptorHeap::DynamicDescriptorHeap( CommandContext& OwningContext )
	:m_OwningContext( OwningContext )
{
	m_CurrentHeapPtr = nullptr;
	m_CurrentOffset = 0;
}

DynamicDescriptorHeap::~DynamicDescriptorHeap()
{
}

void DynamicDescriptorHeap::Initialize()
{
	InitializeCriticalSection( &sm_CS );
}

void DynamicDescriptorHeap::Shutdown()
{
	DestroyAll();
	DeleteCriticalSection( &sm_CS );
}

void DynamicDescriptorHeap::DestroyAll()
{
	sm_DescriptorHeapPool.clear();
}

uint32_t DynamicDescriptorHeap::GetDescriptorSize()
{
	if (sm_DescriptorSize == 0)
		sm_DescriptorSize = Graphics::g_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	return sm_DescriptorSize;
}

void DynamicDescriptorHeap::CleanupUsedHeaps( uint64_t FenceValue )
{
	RetireCurrentHeap();
	RetireUsedHeaps( FenceValue );
	m_GraphicsHandleCache.ClearCache();
	m_ComputeHandleCache.ClearCache();
}

void DynamicDescriptorHeap::SetGraphicsDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] )
{
	m_GraphicsHandleCache.StageDescriptorHandles( RootIndex, Offset, NumHandles, Handles );
}

void DynamicDescriptorHeap::SetComputeDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] )
{
	m_ComputeHandleCache.StageDescriptorHandles( RootIndex, Offset, NumHandles, Handles );
}

D3D12_GPU_DESCRIPTOR_HANDLE DynamicDescriptorHeap::UploadDirect( D3D12_CPU_DESCRIPTOR_HANDLE Handles )
{
	if (!HasSpace( 1 ))
	{
		RetireCurrentHeap();
		UnbindAllValid();
	}
	m_OwningContext.SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GetHeapPointer() );
	DescriptorHandle DestHandle = m_FirstDescriptor + m_CurrentOffset*GetDescriptorSize();
	m_CurrentOffset += 1;
	Graphics::g_device->CopyDescriptorsSimple( 1, DestHandle.GetCPUHandle(), Handles, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	return DestHandle.GetGPUHandle();
}

void DynamicDescriptorHeap::ParseGraphicsRootSignature( const RootSignature& RootSig )
{
	m_GraphicsHandleCache.ParseRootSignature( RootSig );
}

void DynamicDescriptorHeap::ParseComputeRootSignature( const RootSignature& RootSig )
{
	m_ComputeHandleCache.ParseRootSignature( RootSig );
}

DynamicDescriptorHeap::DescriptorTableCache::DescriptorTableCache()
	:AssignedHandlesBitMap( 0 )
{
}

DynamicDescriptorHeap::DescriptorHandleCache::DescriptorHandleCache()
{
	ClearCache();
}

void DynamicDescriptorHeap::DescriptorHandleCache::ClearCache()
{
	m_RootDescriptorTablesBitMap = 0;
	m_MaxCachedDescriptors = 0;
}

uint32_t DynamicDescriptorHeap::DescriptorHandleCache::ComputeStagedSize()
{
	uint32_t NeededSpace = 0;
	uint32_t RootIndex;
	uint32_t StaleParams = m_StaleRootParamsBitMap;
	while (_BitScanForward( (unsigned long*)&RootIndex, StaleParams ))
	{
		StaleParams ^= (1 << RootIndex);
		uint32_t MaxSetHandle;
		ASSERT( TRUE == _BitScanReverse( (unsigned long*)&MaxSetHandle, m_RootDescriptorTable[RootIndex].AssignedHandlesBitMap ) );
		NeededSpace += MaxSetHandle + 1;
	}
	return NeededSpace;
}

void DynamicDescriptorHeap::DescriptorHandleCache::CopyAndBindStaleTables( DescriptorHandle DestHandleStart, ID3D12GraphicsCommandList* CmdList,
	void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::*SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) )
{
	uint32_t StaleParamCount = 0;
	uint32_t TableSize[DescriptorHandleCache::kMaxNumDescriptorTables];
	uint32_t RootIndices[DescriptorHandleCache::kMaxNumDescriptorTables];
	uint32_t RootIndex;

	// Sum the maximum assigned offsets of stale descriptor tables to determine total needed space
	uint32_t StaleParams = m_StaleRootParamsBitMap;
	while (_BitScanForward( (unsigned long*)&RootIndex, StaleParams ))
	{
		RootIndices[StaleParamCount] = RootIndex;
		StaleParams ^= (1 << RootIndex);
		uint32_t MaxSetHandle;
		ASSERT( TRUE == _BitScanReverse( (unsigned long*)&MaxSetHandle, m_RootDescriptorTable[RootIndex].AssignedHandlesBitMap ) );
		TableSize[StaleParamCount] = MaxSetHandle + 1;
		++StaleParamCount;
	}

	ASSERT( StaleParamCount <= DescriptorHandleCache::kMaxNumDescriptorTables );
	m_StaleRootParamsBitMap = 0;

	static const uint32_t kMaxDescriptorsPerCopy = 16;
	const uint32_t kDescriptorSize = DynamicDescriptorHeap::GetDescriptorSize();
	UINT NumDestDescriptorRanges = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE pDestDescriptorRangeStarts[kMaxDescriptorsPerCopy];
	UINT pDestDescriptorRangeSizes[kMaxDescriptorsPerCopy];
	UINT NumSrcDescriptorRanges = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE pSrcDescriptorRangeStarts[kMaxDescriptorsPerCopy];
	UINT pSrcDescriptorRangeSizes[kMaxDescriptorsPerCopy];

	for (uint32_t i = 0; i < StaleParamCount; ++i)
	{
		RootIndex = RootIndices[i];
		(CmdList->*SetFunc)(RootIndex, DestHandleStart.GetGPUHandle());
		DescriptorTableCache& RootDescTable = m_RootDescriptorTable[RootIndex];
		D3D12_CPU_DESCRIPTOR_HANDLE* SrcHandles = RootDescTable.TableStart;
		uint64_t SetHandles = (uint64_t)RootDescTable.AssignedHandlesBitMap;
		D3D12_CPU_DESCRIPTOR_HANDLE CurDest = DestHandleStart.GetCPUHandle();
		DestHandleStart += TableSize[i] * kDescriptorSize;

		unsigned long SkipCount;
		while (_BitScanForward64( &SkipCount, SetHandles ))
		{
			SetHandles >>= SkipCount;
			SrcHandles += SkipCount;
			CurDest.ptr += SkipCount * kDescriptorSize;

			unsigned long DescriptorCount;
			_BitScanForward64( &DescriptorCount, ~SetHandles );
			SetHandles >>= DescriptorCount;

			// If we run out of temp room, copy what we've got so far
			if (NumSrcDescriptorRanges + DescriptorCount > kMaxDescriptorsPerCopy)
			{
				Graphics::g_device->CopyDescriptors( NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
				NumSrcDescriptorRanges = 0;
				NumDestDescriptorRanges = 0;
			}

			// Setup destination range
			pDestDescriptorRangeStarts[NumDestDescriptorRanges] = CurDest;
			pDestDescriptorRangeSizes[NumDestDescriptorRanges] = DescriptorCount;
			++NumDestDescriptorRanges;

			// Setup source ranges 
			for (UINT i = 0; i < DescriptorCount; ++i)
			{
				pSrcDescriptorRangeStarts[NumSrcDescriptorRanges] = SrcHandles[i];
				pSrcDescriptorRangeSizes[NumSrcDescriptorRanges] = 1;
				++NumSrcDescriptorRanges;
			}

			// Move the destination pointer forward by the number of  descriptors we will copy
			SrcHandles += DescriptorCount;
			CurDest.ptr += DescriptorCount * kDescriptorSize;
		}
	}

	Graphics::g_device->CopyDescriptors( NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
		NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}

void DynamicDescriptorHeap::DescriptorHandleCache::UnbindAllValid()
{
	m_StaleRootParamsBitMap = 0;
	unsigned long TableParams = m_RootDescriptorTablesBitMap;
	unsigned long RootIndex;
	while (_BitScanForward( &RootIndex, TableParams ))
	{
		TableParams ^= (1 << RootIndex);
		if (m_RootDescriptorTable[RootIndex].AssignedHandlesBitMap != 0)
			m_StaleRootParamsBitMap |= (1 << RootIndex);
	}
}

void DynamicDescriptorHeap::DescriptorHandleCache::StageDescriptorHandles( UINT RootIndex, UINT Offset, UINT NumHandles, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] )
{
	ASSERT( ((1 << RootIndex) & m_RootDescriptorTablesBitMap) != 0 );
	ASSERT( Offset + NumHandles <= m_RootDescriptorTable[RootIndex].TableSize );

	DescriptorTableCache& TableCache = m_RootDescriptorTable[RootIndex];
	D3D12_CPU_DESCRIPTOR_HANDLE* CopyDest = TableCache.TableStart + Offset;
	for (UINT i = 0; i < NumHandles; ++i)
		CopyDest[i] = Handles[i];
	TableCache.AssignedHandlesBitMap |= ((1 << NumHandles) - 1) << Offset;
	m_StaleRootParamsBitMap |= (1 << RootIndex);
}

void DynamicDescriptorHeap::DescriptorHandleCache::ParseRootSignature( const RootSignature& RootSig )
{
	UINT CurrentOffset = 0;
	ASSERT( RootSig.m_NumParameters <= 16 );

	m_StaleRootParamsBitMap = 0;
	m_RootDescriptorTablesBitMap = RootSig.m_DescriptorTableBitMap;

	unsigned long TableParams = m_RootDescriptorTablesBitMap;
	unsigned long RootIndex;
	while (_BitScanForward( &RootIndex, TableParams ))
	{
		TableParams ^= (1 << RootIndex);
		UINT TableSize = RootSig.m_DescriptorTableSize[RootIndex];
		ASSERT( TableSize > 0 );

		DescriptorTableCache& RootDescriptorTable = m_RootDescriptorTable[RootIndex];
		RootDescriptorTable.AssignedHandlesBitMap = 0;
		RootDescriptorTable.TableStart = m_HandleCache + CurrentOffset;
		RootDescriptorTable.TableSize = TableSize;

		CurrentOffset += TableSize;
	}

	m_MaxCachedDescriptors = CurrentOffset;
	ASSERT( m_MaxCachedDescriptors <= kMaxNumDescriptors );
}

ID3D12DescriptorHeap* DynamicDescriptorHeap::RequestDescriptorHeap()
{
	CriticalSectionScope LockGard( &sm_CS );
	while (!sm_RetiredDescriptorHeaps.empty() && Graphics::g_cmdListMngr.IsFenceComplete( sm_RetiredDescriptorHeaps.front().first ))
	{
		sm_AvailableDescriptorHeaps.push( sm_RetiredDescriptorHeaps.front().second );
		sm_RetiredDescriptorHeaps.pop();
	}
	if (!sm_AvailableDescriptorHeaps.empty())
	{
		ID3D12DescriptorHeap* HeapPtr = sm_AvailableDescriptorHeaps.front();
		sm_AvailableDescriptorHeaps.pop();
		return HeapPtr;
	}
	else
	{
		D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
		HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HeapDesc.NumDescriptors = kNumDescriptorsPerHeap;
		HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		HeapDesc.NodeMask = 1;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> HeapPtr;
		HRESULT hr;
		V( Graphics::g_device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &HeapPtr ) ) );
		sm_DescriptorHeapPool.emplace_back( HeapPtr );
		return HeapPtr.Get();
	}
}

void DynamicDescriptorHeap::DiscardDescriptorHeaps( uint64_t FenceValueForReset, const std::vector<ID3D12DescriptorHeap*>& UsedHeaps )
{
	CriticalSectionScope LockGard( &sm_CS );
	for (auto iter = UsedHeaps.begin(); iter != UsedHeaps.end(); ++iter)
		sm_RetiredDescriptorHeaps.push( std::make_pair( FenceValueForReset, *iter ) );
}

bool DynamicDescriptorHeap::HasSpace( uint32_t Count )
{
	return (m_CurrentHeapPtr != nullptr && m_CurrentOffset + Count <= kNumDescriptorsPerHeap);
}

void DynamicDescriptorHeap::RetireCurrentHeap()
{
	if (m_CurrentOffset == 0)
	{
		ASSERT( m_CurrentHeapPtr == nullptr );
		return;
	}

	ASSERT( m_CurrentHeapPtr != nullptr );
	m_RetiredHeaps.push_back( m_CurrentHeapPtr );
	m_CurrentHeapPtr = nullptr;
	m_CurrentOffset = 0;
}

void DynamicDescriptorHeap::RetireUsedHeaps( uint64_t FenceValue )
{
	DiscardDescriptorHeaps( FenceValue, m_RetiredHeaps );
	m_RetiredHeaps.clear();
}

DescriptorHandle DynamicDescriptorHeap::Allocate( UINT Count )
{
	DescriptorHandle ret = m_FirstDescriptor + m_CurrentOffset * GetDescriptorSize();
	m_CurrentOffset += Count;
	return ret;
}

void DynamicDescriptorHeap::CopyAndBindStagedTables( DescriptorHandleCache& HandleCache, ID3D12GraphicsCommandList* CmdList,
	void(STDMETHODCALLTYPE ID3D12GraphicsCommandList::*SetFunc)(UINT, D3D12_GPU_DESCRIPTOR_HANDLE) )
{
	uint32_t NeededSize = HandleCache.ComputeStagedSize();
	if (!HasSpace( NeededSize ))
	{
		RetireCurrentHeap();
		UnbindAllValid();
	}

	// This can trigger the creation of a new heap
	m_OwningContext.SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GetHeapPointer() );
	HandleCache.CopyAndBindStaleTables( Allocate( NeededSize ), CmdList, SetFunc );
}

void DynamicDescriptorHeap::UnbindAllValid()
{
	m_GraphicsHandleCache.UnbindAllValid();
	m_ComputeHandleCache.UnbindAllValid();
}