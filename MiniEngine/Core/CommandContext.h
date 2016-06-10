#pragma once

#include "LinearAllocator.h"
#include "GpuResource.h"
#include "Utility.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "CommandSignature.h"
#include "DynamicDescriptorHeap.h"
#include "CmdListMngr.h"
#include "Graphics.h"
#include <vector>
#include <queue>

class CmdListMngr;
class GraphicsContext;
class ComputeContext;

#define VALID_COMPUTE_QUEUE_RESOURCE_STATES \
	( D3D12_RESOURCE_STATE_UNORDERED_ACCESS \
	| D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE \
	| D3D12_RESOURCE_STATE_COPY_DEST \
	| D3D12_RESOURCE_STATE_COPY_SOURCE )

struct DWParam
{
	DWParam( FLOAT f ) :Float( f ) {}
	DWParam( UINT u ) :Uint( u ) {}
	DWParam( INT i ) :Int( i ) {}
	void operator=( FLOAT f ) { Float = f; }
	void operator=( UINT u ) { Uint = u; }
	void operator=( INT i ) { Int = i; }
	union
	{
		FLOAT Float;
		UINT Uint;
		INT Int;
	};
};

//--------------------------------------------------------------------------------------
// ContextManager
//--------------------------------------------------------------------------------------
class ContextManager
{
public:
	ContextManager();
	~ContextManager();

	CommandContext* AllocateContext( D3D12_COMMAND_LIST_TYPE Type );
	void FreeContext( CommandContext* );
	void DestroyAllContexts();

private:
	std::vector<std::unique_ptr<CommandContext> > sm_ContextPool[4];
	std::queue<CommandContext*> sm_AvailableContexts[4];
	CRITICAL_SECTION sm_ContextAllocationCS;
};

//--------------------------------------------------------------------------------------
// CommandContext
//--------------------------------------------------------------------------------------
class CommandContext
{
	friend ContextManager;
private:
	CommandContext( D3D12_COMMAND_LIST_TYPE Type );
	void Reset();

public:
	CommandContext() = delete;
	~CommandContext();

	static void DestroyAllContexts();
	static CommandContext& Begin( const std::wstring ID = L"" );

	uint64_t Flush( bool WaitForCompletion = false );
	uint64_t Finish( bool WaitForCompletion = false );

	void Initialize();

	GraphicsContext& GetGraphicsContext();
	ComputeContext& GetComputeContext();

	void CopyBufferRegion( GpuResource& Dest, size_t DestOffset, GpuResource& Src, size_t SrcOffset, size_t NumBytes );
	void CopySubResource( GpuResource& Dest, UINT DestSubIndex, GpuResource& Src, UINT SrcSubIndex );
	void ResetCounter( StructuredBuffer& Buf, uint32_t Value = 0 );

	static void InitializeBuffer( GpuResource& Dest, const void* Data, size_t NumBytes, bool UseOffset = false, size_t Offset = 0 );
	static void InitializeTexture( GpuResource& Dest, UINT NumSubresources, D3D12_SUBRESOURCE_DATA SubData[] );

	void FillBuffer( GpuResource& Dest, size_t DestOffset, DWParam Value, size_t NumByte );

	void TransitionResource( GpuResource&  Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate = false );
	void BeginResourceTransition( GpuResource& Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate = false );

	void InsertUAVBarrier( GpuResource& Resource, bool FlushImmediate = false );
	void FlushResourceBarriers();

	void InsertTimeStamp( ID3D12QueryHeap* pQueryHeap, uint32_t QueryIdx );
	void ResolveTimeStamps( ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t NumQueries );
	void PIXBeginEvent( const wchar_t* label );
	void PIXEndEvent();
	void PIXSetMarker( const wchar_t* label );

	void SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE Type, ID3D12DescriptorHeap* HeapPtr );
	void SetDescriptorHeaps( UINT HeapCount, D3D12_DESCRIPTOR_HEAP_TYPE Type[], ID3D12DescriptorHeap* HeapPtrs[] );

	// For resource view (SRV, CBV, RTV...) allocation
	LinearAllocator m_CpuLinearAllocator;
	LinearAllocator m_GpuLinearAllocator;

protected:
	void BindDescriptorHeaps();

	void SetID( const std::wstring& ID ) { m_ID = ID; }

	CmdListMngr* m_OwningManager;
	ID3D12GraphicsCommandList* m_CommandList;
	ID3D12CommandAllocator* m_CurCmdAllocator;

	ID3D12RootSignature* m_CurGraphicsRootSignature;
	ID3D12PipelineState* m_CurGraphicsPipelineState;
	ID3D12RootSignature* m_CurComputeRootSignature;
	ID3D12PipelineState* m_CurComputePipelineState;

	DynamicDescriptorHeap m_DynamicDescriptorHeap;

	D3D12_RESOURCE_BARRIER m_ResourceBarrierBuffer[16];
	UINT m_NumBarriersToFlush;

	ID3D12DescriptorHeap* m_CurrentDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	std::wstring m_ID;

	D3D12_COMMAND_LIST_TYPE m_Type;
};

inline void CommandContext::CopyBufferRegion( GpuResource& Dest, size_t DestOffset, GpuResource& Src, size_t SrcOffset, size_t NumBytes )
{
	TransitionResource( Dest, D3D12_RESOURCE_STATE_COPY_DEST );
	FlushResourceBarriers();
	m_CommandList->CopyBufferRegion( Dest.GetResource(), DestOffset, Src.GetResource(), SrcOffset, NumBytes );
}

inline void CommandContext::ResetCounter( StructuredBuffer& Buf, uint32_t Value /* = 0 */ )
{
	FillBuffer( Buf.GetCounterBuffer(), 0, Value, sizeof( uint32_t ) );
	TransitionResource( Buf.GetCounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
}

inline void CommandContext::SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE Type, ID3D12DescriptorHeap* HeapPtr )
{
	if (m_CurrentDescriptorHeaps[Type] != HeapPtr)
	{
		m_CurrentDescriptorHeaps[Type] = HeapPtr;
		BindDescriptorHeaps();
	}
}

inline void CommandContext::SetDescriptorHeaps( UINT HeapCount, D3D12_DESCRIPTOR_HEAP_TYPE Type[], ID3D12DescriptorHeap* HeapPtrs[] )
{
	bool AnyChanged = false;
	for (UINT i = 0; i < HeapCount; ++i)
	{
		if (m_CurrentDescriptorHeaps[Type[i]] != HeapPtrs[i])
		{
			m_CurrentDescriptorHeaps[Type[i]] = HeapPtrs[i];
			AnyChanged = true;
		}
	}
	if (AnyChanged)
		BindDescriptorHeaps();
}

inline void CommandContext::InsertTimeStamp( ID3D12QueryHeap* pQueryHeap, uint32_t QueryIdx )
{
	m_CommandList->EndQuery( pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, QueryIdx );
}

inline void CommandContext::ResolveTimeStamps( ID3D12Resource* pReadbackHeap, ID3D12QueryHeap* pQueryHeap, uint32_t NumQueries )
{
	m_CommandList->ResolveQueryData( pQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, NumQueries, pReadbackHeap, 0 );
}

inline void CommandContext::PIXBeginEvent( const wchar_t* label )
{
#if defined(RELEASE) || _MSC_VER < 1800
	(label);
#else
	::PIXBeginEvent( m_CommandList, 0, label );
#endif
}

inline void CommandContext::PIXEndEvent( void )
{
#if !defined(RELEASE) && _MSC_VER >= 1800
	::PIXEndEvent( m_CommandList );
#endif
}

inline void CommandContext::PIXSetMarker( const wchar_t* label )
{
#if defined(RELEASE) || _MSC_VER < 1800
	(label);
#else
	::PIXSetMarker( m_CommandList, 0, label );
#endif
}

//--------------------------------------------------------------------------------------
// GraphicsContext
//--------------------------------------------------------------------------------------
class GraphicsContext : public CommandContext
{
public:
	static GraphicsContext& Begin( const std::wstring& ID = L"" )
	{
		return CommandContext::Begin( ID ).GetGraphicsContext();
	}

	void ClearUAV( GpuBuffer& Target );
	void ClearColor( ColorBuffer& Target );
	void ClearDepth( DepthBuffer& Target );
	void ClearStencil( DepthBuffer& Target );
	void ClearDepthAndStencil( DepthBuffer& Target );

	void BeginQuery( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT HeapIndex );
	void EndQuery( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT HeapIndex );
	void ResolveQueryData( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource* DestinationBuffer, UINT64 DestinationBufferOffset );

	void SetRootSignature( const RootSignature& RootSig );
	void SetRenderTargets( UINT NumRTVs, ColorBuffer* RTVs, DepthBuffer* DSV = nullptr, bool ReadOnlyDepth = false );
	void SetViewport( const D3D12_VIEWPORT& vp );
	void SetViewports( UINT NumVPs, const D3D12_VIEWPORT* vps );
	void SetScisor( const D3D12_RECT& rect );
	void SetScisors( UINT NumScisor, const D3D12_RECT* rects );
	void SetPrimitiveTopology( D3D12_PRIMITIVE_TOPOLOGY Topology );
	void SetPipelineState( const GraphicsPSO& PSO );
	void SetConstants( UINT RootIndex, UINT NumConstants, const void* pConstants );
	void SetConstants( UINT RootIndex, DWParam X );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W );
	void SetConstantBuffer( UINT RootIndex, D3D12_GPU_VIRTUAL_ADDRESS CBV );
	void SetDynamicDescriptors( UINT RootIndex, UINT Offset, UINT Count, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] );
	void SetDynamicVB( UINT Slot, size_t NumVertices, size_t VertexStride, const void* VertexData );
	void SetDynamicVB( UINT Slot, D3D12_VERTEX_BUFFER_VIEW& VBView );
	void SetDynamicIB( size_t IndexCount, const uint16_t* IndexData );
	void SetDynamicIB( D3D12_INDEX_BUFFER_VIEW& IBView );
	void SetDynamicSRV( UINT RootIndex, size_t BufferSize, const void* BufferData );
	void SetDynamicConstantBufferView( UINT RootIndex, size_t BufferSize, const void* BufferData );
	void SetBufferSRV( UINT RootIndex, const GpuBuffer& SRV );
	void SetBufferUAV( UINT RootIndex, const GpuBuffer& UAV );
	void SetDescriptorTable( UINT RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE FirstHandle );

	void SetIndexBuffer( const D3D12_INDEX_BUFFER_VIEW& IBView );
	void SetVertexBuffer( UINT Slot, const D3D12_VERTEX_BUFFER_VIEW& VBView );
	void SetVertexBuffers( UINT StartSlot, UINT Count, const D3D12_VERTEX_BUFFER_VIEW VBViews[] );

	void Draw( UINT VertexCount, UINT VertexStartOffset = 0 );
	void DrawIndexed( UINT IndexCount, UINT StartIndexLocation = 0, INT BaseVertexLocation = 0 );
	void DrawInstanced( UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation = 0, UINT StartInstanceLocation = 0 );
	void DrawIndexedInstanced( UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
		INT BaseVertexLocation, UINT StartInstanceLocation );
	//void DrawIndirect(GpuBuffer& ArgumentBuffer, size_t ArgumentBufferOffset = 0);
};

inline void GraphicsContext::SetRootSignature( const RootSignature& RootSig )
{
	if (RootSig.GetSignature() == m_CurGraphicsRootSignature)
		return;
	m_CommandList->SetGraphicsRootSignature( m_CurGraphicsRootSignature = RootSig.GetSignature() );
	m_DynamicDescriptorHeap.ParseGraphicsRootSignature( RootSig );
}

inline void GraphicsContext::SetPrimitiveTopology( D3D12_PRIMITIVE_TOPOLOGY Topology )
{
	m_CommandList->IASetPrimitiveTopology( Topology );
}

inline void GraphicsContext::SetPipelineState( const GraphicsPSO& PSO )
{
	if (PSO.GetPipelineStateObject() == m_CurGraphicsPipelineState)
		return;
	m_CommandList->SetPipelineState( m_CurGraphicsPipelineState = PSO.GetPipelineStateObject() );
}

inline void GraphicsContext::SetConstants( UINT RootIndex, UINT NumConstants, const void* pConstants )
{
	m_CommandList->SetGraphicsRoot32BitConstants( RootIndex, NumConstants, pConstants, 0 );
}

inline void GraphicsContext::SetConstants( UINT RootIndex, DWParam X )
{
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, X.Uint, 0 );
}


inline void GraphicsContext::SetConstants( UINT RootIndex, DWParam X, DWParam Y )
{
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, X.Uint, 0 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, Y.Uint, 1 );
}

inline void GraphicsContext::SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z )
{
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, X.Uint, 0 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, Y.Uint, 1 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, Z.Uint, 2 );
}

inline void GraphicsContext::SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W )
{
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, X.Uint, 0 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, Y.Uint, 1 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, Z.Uint, 2 );
	m_CommandList->SetGraphicsRoot32BitConstant( RootIndex, W.Uint, 3 );
}

inline void GraphicsContext::SetConstantBuffer( UINT RootIndex, D3D12_GPU_VIRTUAL_ADDRESS CBV )
{
	m_CommandList->SetGraphicsRootConstantBufferView( RootIndex, CBV );
}

inline void GraphicsContext::SetDynamicDescriptors( UINT RootIndex, UINT Offset, UINT Count, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] )
{
	m_DynamicDescriptorHeap.SetGraphicsDescriptorHandles( RootIndex, Offset, Count, Handles );
}

inline void GraphicsContext::SetDynamicVB( UINT Slot, size_t NumVertices, size_t VertexStride, const void* VertexData )
{
	size_t BufferSize = NumVertices * VertexStride;
	DynAlloc vb = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( vb.DataPtr, VertexData, BufferSize );

	D3D12_VERTEX_BUFFER_VIEW VBView;
	VBView.BufferLocation = vb.GpuAddress;
	VBView.SizeInBytes = (UINT)BufferSize;
	VBView.StrideInBytes = (UINT)VertexStride;
	m_CommandList->IASetVertexBuffers( Slot, 1, &VBView );
}

inline void GraphicsContext::SetDynamicVB( UINT Slot, D3D12_VERTEX_BUFFER_VIEW& VBView )
{
	m_CommandList->IASetVertexBuffers( Slot, 1, &VBView );
}

inline void GraphicsContext::SetDynamicIB( size_t IndexCount, const uint16_t* IndexData )
{
	size_t BufferSize = IndexCount * sizeof( uint16_t );
	DynAlloc ib = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( ib.DataPtr, IndexData, BufferSize );

	D3D12_INDEX_BUFFER_VIEW IBView;
	IBView.BufferLocation = ib.GpuAddress;
	IBView.SizeInBytes = (UINT)BufferSize;
	IBView.Format = DXGI_FORMAT_R16_UINT;
	m_CommandList->IASetIndexBuffer( &IBView );
}

inline void GraphicsContext::SetDynamicIB( D3D12_INDEX_BUFFER_VIEW& IBView )
{
	m_CommandList->IASetIndexBuffer( &IBView );
}

inline void GraphicsContext::SetDynamicSRV( UINT RootIndex, size_t BufferSize, const void* BufferData )
{
	ASSERT( BufferData != nullptr && IsAligned( BufferData, 16 ) );
	DynAlloc cb = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( cb.DataPtr, BufferData, BufferSize );
	m_CommandList->SetGraphicsRootShaderResourceView( RootIndex, cb.GpuAddress );
}

inline void GraphicsContext::SetDynamicConstantBufferView( UINT RootIndex, size_t BufferSize, const void* BufferData )
{
	ASSERT( BufferData != nullptr && IsAligned( BufferData, 16 ) );
	DynAlloc cb = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( cb.DataPtr, BufferData, BufferSize );
	m_CommandList->SetGraphicsRootConstantBufferView( RootIndex, cb.GpuAddress );
}

inline void GraphicsContext::SetBufferSRV( UINT RootIndex, const GpuBuffer& SRV )
{
	ASSERT( (SRV.m_UsageState & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) != 0 );
	m_CommandList->SetGraphicsRootShaderResourceView( RootIndex, SRV.GetGpuVirtualAddress() );
}

inline void GraphicsContext::SetBufferUAV( UINT RootIndex, const GpuBuffer& UAV )
{
	ASSERT( (UAV.m_UsageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0 );
	m_CommandList->SetGraphicsRootUnorderedAccessView( RootIndex, UAV.GetGpuVirtualAddress() );
}

inline void GraphicsContext::SetDescriptorTable( UINT RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE FirstHandle )
{
	m_CommandList->SetGraphicsRootDescriptorTable( RootIndex, FirstHandle );
}

inline void GraphicsContext::SetIndexBuffer( const D3D12_INDEX_BUFFER_VIEW& IBView )
{
	m_CommandList->IASetIndexBuffer( &IBView );
}

inline void GraphicsContext::SetVertexBuffer( UINT Slot, const D3D12_VERTEX_BUFFER_VIEW& VBView )
{
	m_CommandList->IASetVertexBuffers( Slot, 1, &VBView );
}

inline void GraphicsContext::SetVertexBuffers( UINT StartSlot, UINT Count, const D3D12_VERTEX_BUFFER_VIEW VBViews[] )
{
	m_CommandList->IASetVertexBuffers( StartSlot, Count, VBViews );
}

inline void GraphicsContext::Draw( UINT VertexCount, UINT VertexStartOffset /* = 0 */ )
{
	DrawInstanced( VertexCount, 1, VertexStartOffset, 0 );
}

inline void GraphicsContext::DrawIndexed( UINT IndexCount, UINT StartIndexLocation /* = 0 */, INT BaseVertexLocation /* = 0 */ )
{
	DrawIndexedInstanced( IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0 );
}

inline void GraphicsContext::DrawInstanced( UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation /* = 0 */, UINT StartInstanceLocation /* = 0 */ )
{
	FlushResourceBarriers();
	m_DynamicDescriptorHeap.CommitGraphicsRootDescriptorTables( m_CommandList );
	m_CommandList->DrawInstanced( VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation );
}

inline void GraphicsContext::DrawIndexedInstanced( UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation )
{
	FlushResourceBarriers();
	m_DynamicDescriptorHeap.CommitGraphicsRootDescriptorTables( m_CommandList );
	m_CommandList->DrawIndexedInstanced( IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation );
}

//inline void GraphicsContext::DrawIndirect(GpuBuffer& ArgumentBuffer, size_t ArgumentBufferOffset /* = 0 */)
//{
//	FlushResourceBarriers();
//	m_CommandList->ExecuteIndirect(Graphics::draw)
//}

//--------------------------------------------------------------------------------------
// ComputeContext
//--------------------------------------------------------------------------------------
class ComputeContext : public CommandContext
{
public:
	static ComputeContext& Begin( const std::wstring& ID = L"", bool Async = false );

	void ClearUAV(GpuBuffer& Target);
	//void ClearUAV(ColorBuffer& Target);

	void SetRootSignature( const RootSignature& RootSig );
	void SetPipelineState( const ComputePSO& PSO );
	void SetConstants( UINT RootIndex, UINT NumConstants, const void* pConstants );
	void SetConstants( UINT RootIndex, DWParam X );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z );
	void SetConstants( UINT RootIndex, DWParam X, DWParam Y, DWParam Z, DWParam W );
	void SetConstantBuffer( UINT RootIndex, D3D12_GPU_VIRTUAL_ADDRESS CBV );
	void SetDynamicDescriptors( UINT RootIndex, UINT Offset, UINT Count, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] );
	void SetDynamicSRV( UINT RootIndex, size_t BufferSize, const void* BufferData );
	void SetDynamicConstantBufferView( UINT RootIndex, size_t BufferSize, const void* BufferData );
	void SetBufferSRV( UINT RootIndex, const GpuBuffer& SRV );
	void SetBufferUAV( UINT RootIndex, const GpuBuffer& UAV );
	void SetDescriptorTable( UINT RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE FirstHandle );

	void Dispatch( size_t GroupCountX = 1, size_t GroupCountY = 1, size_t GroupCountZ = 1 );
	void Dispatch1D( size_t ThreadCountX, size_t GroupSizeX = 64 );
	void Dispatch2D( size_t ThreadCountX, size_t ThreadCountY, size_t GroupSizeX = 8, size_t GroupSizey = 8 );
	void Dispatch3D( size_t ThreadCountX, size_t ThreadCountY, size_t ThreadCountZ, size_t GroupSizeX, size_t GroupSizeY, size_t GroupSizeZ );
	void DispatchIndirect( GpuBuffer& ArgumentBuffer, size_t ArgumentBUfferOffset );
};

inline void ComputeContext::SetRootSignature( const RootSignature& RootSig )
{
	if (RootSig.GetSignature() == m_CurComputeRootSignature)
		return;
	m_CommandList->SetComputeRootSignature( m_CurComputeRootSignature = RootSig.GetSignature() );
	m_DynamicDescriptorHeap.ParseComputeRootSignature( RootSig );
}

inline void ComputeContext::SetPipelineState( const ComputePSO& PSO )
{
	if (PSO.GetPipelineStateObject() == m_CurComputePipelineState)
		return;
	m_CommandList->SetPipelineState( m_CurComputePipelineState = PSO.GetPipelineStateObject() );
}

inline void ComputeContext::SetConstants( UINT RootEntry, UINT NumConstants, const void* pConstants )
{
	m_CommandList->SetComputeRoot32BitConstants( RootEntry, NumConstants, pConstants, 0 );
}

inline void ComputeContext::SetConstants( UINT RootEntry, DWParam X )
{
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, X.Uint, 0 );
}

inline void ComputeContext::SetConstants( UINT RootEntry, DWParam X, DWParam Y )
{
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, X.Uint, 0 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, Y.Uint, 1 );
}

inline void ComputeContext::SetConstants( UINT RootEntry, DWParam X, DWParam Y, DWParam Z )
{
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, X.Uint, 0 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, Y.Uint, 1 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, Z.Uint, 2 );
}

inline void ComputeContext::SetConstants( UINT RootEntry, DWParam X, DWParam Y, DWParam Z, DWParam W )
{
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, X.Uint, 0 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, Y.Uint, 1 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, Z.Uint, 2 );
	m_CommandList->SetComputeRoot32BitConstant( RootEntry, W.Uint, 3 );
}

inline void ComputeContext::SetConstantBuffer( UINT RootIndex, D3D12_GPU_VIRTUAL_ADDRESS CBV )
{
	m_CommandList->SetComputeRootConstantBufferView( RootIndex, CBV );
}

inline void ComputeContext::SetDynamicDescriptors( UINT RootIndex, UINT Offset, UINT Count, const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] )
{
	m_DynamicDescriptorHeap.SetComputeDescriptorHandles( RootIndex, Offset, Count, Handles );
}

inline void ComputeContext::SetDynamicSRV( UINT RootIndex, size_t BufferSize, const void* BufferData )
{
	ASSERT( BufferData != nullptr && IsAligned( BufferData, 16 ) );
	DynAlloc cb = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( cb.DataPtr, BufferData, BufferSize );
	m_CommandList->SetComputeRootShaderResourceView( RootIndex, cb.GpuAddress );
}

inline void ComputeContext::SetDynamicConstantBufferView( UINT RootIndex, size_t BufferSize, const void* BufferData )
{
	ASSERT( BufferData != nullptr && IsAligned( BufferData, 16 ) );
	DynAlloc cb = m_CpuLinearAllocator.Allocate( BufferSize );
	memcpy( cb.DataPtr, BufferData, BufferSize );
	m_CommandList->SetComputeRootConstantBufferView( RootIndex, cb.GpuAddress );
}

inline void ComputeContext::SetBufferSRV( UINT RootIndex, const GpuBuffer& SRV )
{
	ASSERT( (SRV.m_UsageState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0 );
	m_CommandList->SetComputeRootShaderResourceView( RootIndex, SRV.GetGpuVirtualAddress() );
}

inline void ComputeContext::SetBufferUAV( UINT RootIndex, const GpuBuffer& UAV )
{
	ASSERT( (UAV.m_UsageState & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0 );
	m_CommandList->SetComputeRootUnorderedAccessView( RootIndex, UAV.GetGpuVirtualAddress() );
}

inline void ComputeContext::SetDescriptorTable( UINT RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE FirstHandle )
{
	m_CommandList->SetComputeRootDescriptorTable( RootIndex, FirstHandle );
}

inline void ComputeContext::Dispatch( size_t GroupCountX /* = 1 */, size_t GroupCountY /* = 1 */, size_t GroupCountZ /* = 1 */ )
{
	FlushResourceBarriers();
	m_DynamicDescriptorHeap.CommitComputeRootDescriptorTables( m_CommandList );
	m_CommandList->Dispatch( (UINT)GroupCountX, (UINT)GroupCountY, (UINT)GroupCountZ );
}

inline void ComputeContext::Dispatch1D( size_t ThreadCountX, size_t GroupSizeX /* = 64 */ )
{
	Dispatch( DivideByMultiple( ThreadCountX, GroupSizeX ), 1, 1 );
}

inline void ComputeContext::Dispatch2D( size_t ThreadCountX, size_t ThreadCountY, size_t GroupSizeX, size_t GroupSizeY )
{
	Dispatch(
		DivideByMultiple( ThreadCountX, GroupSizeX ),
		DivideByMultiple( ThreadCountY, GroupSizeY ), 1 );
}

inline void ComputeContext::Dispatch3D( size_t ThreadCountX, size_t ThreadCountY, size_t ThreadCountZ, size_t GroupSizeX, size_t GroupSizeY, size_t GroupSizeZ )
{
	Dispatch(
		DivideByMultiple( ThreadCountX, GroupSizeX ),
		DivideByMultiple( ThreadCountY, GroupSizeY ),
		DivideByMultiple( ThreadCountZ, GroupSizeZ ) );
}

inline void ComputeContext::DispatchIndirect( GpuBuffer& ArgumentBuffer, size_t ArgumentBUfferOffset )
{
	FlushResourceBarriers();
	m_DynamicDescriptorHeap.CommitComputeRootDescriptorTables( m_CommandList );
	m_CommandList->ExecuteIndirect( Graphics::g_DispatchIndirectCommandSignature.GetSignature(), 1, ArgumentBuffer.GetResource(), (UINT64)ArgumentBUfferOffset, nullptr, 0 );
}