#include "LibraryHeader.h"
#include "CommandContext.h"

//--------------------------------------------------------------------------------------
// ContextManager
//--------------------------------------------------------------------------------------
ContextManager::ContextManager()
{
	InitializeCriticalSection( &sm_ContextAllocationCS );
}

ContextManager::~ContextManager()
{
	DeleteCriticalSection( &sm_ContextAllocationCS );
}

CommandContext* ContextManager::AllocateContext( D3D12_COMMAND_LIST_TYPE Type )
{
	CriticalSectionScope LockGuard( &sm_ContextAllocationCS );

	auto& AvailableContexts = sm_AvailableContexts[Type];
	CommandContext* ret = nullptr;
	if (AvailableContexts.empty())
	{
		ret = new CommandContext( Type );
		sm_ContextPool[Type].emplace_back( ret );
		ret->Initialize();
	}
	else
	{
		ret = AvailableContexts.front();
		AvailableContexts.pop();
		ret->Reset();
	}
	ASSERT( ret != nullptr );
	ASSERT( ret->m_Type == Type );
	return ret;
}

void ContextManager::FreeContext( CommandContext* UsedContext )
{
	ASSERT( UsedContext != nullptr );
	CriticalSectionScope LockGuard( &sm_ContextAllocationCS );
	sm_AvailableContexts[UsedContext->m_Type].push( UsedContext );
}

void ContextManager::DestroyAllContexts()
{
	for (uint32_t i = 0; i < 4; ++i)
		sm_ContextPool[i].clear();
}

//--------------------------------------------------------------------------------------
// CommandContext
//--------------------------------------------------------------------------------------
CommandContext::CommandContext( D3D12_COMMAND_LIST_TYPE Type ) :
	m_Type( Type ),
	m_DynamicDescriptorHeap( *this ),
	m_CpuLinearAllocator( kCpuWritable ),
	m_GpuLinearAllocator( kGpuExclusive )
{
	m_OwningManager = nullptr;
	m_CommandList = nullptr;
	m_CurCmdAllocator = nullptr;
	ZeroMemory( m_CurrentDescriptorHeaps, sizeof( m_CurrentDescriptorHeaps ) );

	m_CurGraphicsRootSignature = nullptr;
	m_CurGraphicsPipelineState = nullptr;
	m_CurComputeRootSignature = nullptr;
	m_CurComputePipelineState = nullptr;
	m_NumBarriersToFlush = 0;
}

void CommandContext::Reset()
{
	ASSERT( m_CommandList != nullptr && m_CurCmdAllocator == nullptr );
	m_CurCmdAllocator = Graphics::g_cmdListMngr.GetQueue( m_Type ).RequestAllocator();
	m_CommandList->Reset( m_CurCmdAllocator, nullptr );

	m_CurGraphicsRootSignature = nullptr;
	m_CurComputeRootSignature = nullptr;
	m_CurGraphicsPipelineState = nullptr;
	m_CurComputePipelineState = nullptr;
	m_NumBarriersToFlush = 0;

	BindDescriptorHeaps();
}

CommandContext::~CommandContext()
{
	if (m_CommandList != nullptr) m_CommandList->Release();
}

void CommandContext::DestroyAllContexts()
{
	LinearAllocator::DestroyAll();
	Graphics::g_ContextMngr.DestroyAllContexts();
}

CommandContext& CommandContext::Begin( const std::wstring ID )
{
	CommandContext* NewContext = Graphics::g_ContextMngr.AllocateContext( D3D12_COMMAND_LIST_TYPE_DIRECT );
	NewContext->SetID( ID );
	return *NewContext;
}

uint64_t CommandContext::Flush( bool WaitForCompletion )
{
	FlushResourceBarriers();

	ASSERT( m_CurCmdAllocator != nullptr );

	uint64_t FenceValue = Graphics::g_cmdListMngr.GetQueue( m_Type ).ExecuteCommandList( m_CommandList );

	if (WaitForCompletion)
		Graphics::g_cmdListMngr.WaitForFence( FenceValue );

	m_CommandList->Reset( m_CurCmdAllocator, nullptr );

	if (m_CurGraphicsRootSignature)
	{
		m_CommandList->SetGraphicsRootSignature( m_CurGraphicsRootSignature );
		m_CommandList->SetPipelineState( m_CurGraphicsPipelineState );
	}
	if (m_CurComputeRootSignature)
	{
		m_CommandList->SetComputeRootSignature( m_CurComputeRootSignature );
		m_CommandList->SetPipelineState( m_CurComputePipelineState );
	}

	BindDescriptorHeaps();

	return FenceValue;
}

uint64_t CommandContext::Finish( bool WaitForCompletion )
{
	ASSERT( m_Type == D3D12_COMMAND_LIST_TYPE_DIRECT || m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE );

	FlushResourceBarriers();

	ASSERT( m_CurCmdAllocator != nullptr );

	CommandQueue& Queue = Graphics::g_cmdListMngr.GetQueue( m_Type );
	uint64_t FenceValue = Queue.ExecuteCommandList( m_CommandList );
	Queue.DiscardAllocator( FenceValue, m_CurCmdAllocator );
	m_CurCmdAllocator = nullptr;
	m_CpuLinearAllocator.CleanupUsedPages( FenceValue );
	m_GpuLinearAllocator.CleanupUsedPages( FenceValue );
	m_DynamicDescriptorHeap.CleanupUsedHeaps( FenceValue );

	if (WaitForCompletion)
		Graphics::g_cmdListMngr.WaitForFence( FenceValue );

	Graphics::g_ContextMngr.FreeContext( this );

	return FenceValue;
}

void CommandContext::Initialize()
{
	Graphics::g_cmdListMngr.CreateNewCommandList( m_Type, &m_CommandList, &m_CurCmdAllocator );
}

GraphicsContext& CommandContext::GetGraphicsContext()
{
	ASSERT( m_Type != D3D12_COMMAND_LIST_TYPE_COMPUTE );
	return reinterpret_cast<GraphicsContext&>(*this);
}

ComputeContext& CommandContext::GetComputeContext()
{
	return reinterpret_cast<ComputeContext&>(*this);
}

void CommandContext::CopySubResource( GpuResource& Dest, UINT DestSubIndex, GpuResource& Src, UINT SrcSubIndex )
{
	TransitionResource( Dest, D3D12_RESOURCE_STATE_COPY_DEST );
	TransitionResource( Src, D3D12_RESOURCE_STATE_COPY_SOURCE );
	FlushResourceBarriers();

	D3D12_TEXTURE_COPY_LOCATION DestLocation =
	{
		Dest.GetResource(),
		D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		DestSubIndex
	};

	D3D12_TEXTURE_COPY_LOCATION SrcLocation =
	{
		Src.GetResource(),
		D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		SrcSubIndex
	};

	m_CommandList->CopyTextureRegion( &DestLocation, 0, 0, 0, &SrcLocation, nullptr );
}

void CommandContext::InitializeBuffer( GpuResource& Dest, const void* Data, size_t NumBytes, bool UseOffset /* = false */, size_t Offset /* = 0 */ )
{
	ID3D12Resource* UploadBuffer;

	CommandContext& InitContext = CommandContext::Begin();

	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC BufferDesc;
	BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	BufferDesc.Alignment = 0;
	BufferDesc.Width = NumBytes;
	BufferDesc.Height = 1;
	BufferDesc.DepthOrArraySize = 1;
	BufferDesc.MipLevels = 1;
	BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	BufferDesc.SampleDesc.Count = 1;
	BufferDesc.SampleDesc.Quality = 0;
	BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr;
	V( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE,
		&BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &UploadBuffer ) ) );

	void* DestAddress;
	UploadBuffer->Map( 0, nullptr, &DestAddress );
	memcpy( DestAddress, Data, NumBytes );
	UploadBuffer->Unmap( 0, nullptr );

	InitContext.TransitionResource( Dest, D3D12_RESOURCE_STATE_COPY_DEST, true );
	if (UseOffset)
		InitContext.m_CommandList->CopyBufferRegion( Dest.GetResource(), Offset, UploadBuffer, 0, NumBytes );
	else
		InitContext.m_CommandList->CopyResource( Dest.GetResource(), UploadBuffer );
	InitContext.TransitionResource( Dest, D3D12_RESOURCE_STATE_GENERIC_READ, true );

	InitContext.Finish( true );
	UploadBuffer->Release();
}

void CommandContext::InitializeTexture( GpuResource& Dest, UINT NumSubresources, D3D12_SUBRESOURCE_DATA SubData[] )
{
	ID3D12Resource* UploadBuffer;

	UINT64 uploadBufferSize = GetRequiredIntermediateSize( Dest.GetResource(), 0, NumSubresources );

	CommandContext& InitContext = CommandContext::Begin();

	D3D12_HEAP_PROPERTIES HeapProps = {};
	HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC BufferDesc = {};
	BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	BufferDesc.Alignment = 0;
	BufferDesc.Width = uploadBufferSize;
	BufferDesc.Height = 1;
	BufferDesc.DepthOrArraySize = 1;
	BufferDesc.MipLevels = 1;
	BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	BufferDesc.SampleDesc.Count = 1;
	BufferDesc.SampleDesc.Quality = 0;
	BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr;
	V( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE,
		&BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &UploadBuffer ) ) );

	// Copy data to the intermediate upload heap and then schedule a copy from the upload heap to the default texture
	InitContext.TransitionResource( Dest, D3D12_RESOURCE_STATE_COPY_DEST, true );
	UpdateSubresources( InitContext.m_CommandList, Dest.GetResource(), UploadBuffer, 0, 0, NumSubresources, SubData );
	InitContext.TransitionResource( Dest, D3D12_RESOURCE_STATE_GENERIC_READ, true );

	// Execute the command list and wait for it to finish then we can release the upload buffer
	InitContext.Finish( true );
	UploadBuffer->Release();
}

void CommandContext::FillBuffer( GpuResource& Dest, size_t DestOffset, DWParam Value, size_t NumByte )
{
	DynAlloc TempSpace = m_CpuLinearAllocator.Allocate( NumByte, 512 );
	DWParam* ptr = (DWParam*)TempSpace.DataPtr;
	for (int i = 0; i < DivideByMultiple( NumByte, sizeof(DWParam)); ++i)
		*(ptr + i) = Value.Float;
	CopyBufferRegion( Dest, DestOffset, TempSpace.Buffer, TempSpace.Offset, NumByte );
}

void CommandContext::TransitionResource( GpuResource& Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate /* = false */ )
{
	D3D12_RESOURCE_STATES OldState = Resource.m_UsageState;
	if (m_Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
		ASSERT( (OldState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == OldState );
		ASSERT( (NewState & VALID_COMPUTE_QUEUE_RESOURCE_STATES) == NewState );
	}
	if (OldState != NewState)
	{
		ASSERT( m_NumBarriersToFlush < 16 );
		D3D12_RESOURCE_BARRIER& BarrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];
		BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		BarrierDesc.Transition.pResource = Resource.GetResource();
		BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		BarrierDesc.Transition.StateBefore = OldState;
		BarrierDesc.Transition.StateAfter = NewState;

		// Check to see if we already started the transition
		if (NewState == Resource.m_TransitioningState)
		{
			BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
			Resource.m_TransitioningState = (D3D12_RESOURCE_STATES)-1;
		}
		else
			BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		Resource.m_UsageState = NewState;
	}
	else if (NewState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		InsertUAVBarrier( Resource, FlushImmediate );

	if (m_NumBarriersToFlush != 0 && (FlushImmediate || m_NumBarriersToFlush == 16))
	{
		m_CommandList->ResourceBarrier( m_NumBarriersToFlush, m_ResourceBarrierBuffer );
		m_NumBarriersToFlush = 0;
	}
}

void CommandContext::BeginResourceTransition( GpuResource& Resource, D3D12_RESOURCE_STATES NewState, bool FlushImmediate /* = false */ )
{
	// If it's already transitioning, finish that transition
	if (Resource.m_TransitioningState != (D3D12_RESOURCE_STATES)-1)
		TransitionResource( Resource, Resource.m_TransitioningState );

	D3D12_RESOURCE_STATES OldState = Resource.m_UsageState;

	if (OldState != NewState)
	{
		ASSERT( m_NumBarriersToFlush < 16 );
		D3D12_RESOURCE_BARRIER& BarrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

		BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		BarrierDesc.Transition.pResource = Resource.GetResource();
		BarrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		BarrierDesc.Transition.StateBefore = OldState;
		BarrierDesc.Transition.StateAfter = NewState;

		BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;

		Resource.m_TransitioningState = NewState;
	}

	if (m_NumBarriersToFlush != 0 && (FlushImmediate || m_NumBarriersToFlush == 16))
	{
		m_CommandList->ResourceBarrier( m_NumBarriersToFlush, m_ResourceBarrierBuffer );
		m_NumBarriersToFlush = 0;
	}
}

void CommandContext::InsertUAVBarrier( GpuResource& Resource, bool FlushImmediate /* = false */ )
{
	ASSERT( m_NumBarriersToFlush < 16 );
	D3D12_RESOURCE_BARRIER& BarrierDesc = m_ResourceBarrierBuffer[m_NumBarriersToFlush++];

	BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	BarrierDesc.UAV.pResource = Resource.GetResource();

	if (FlushImmediate)
	{
		m_CommandList->ResourceBarrier( m_NumBarriersToFlush, m_ResourceBarrierBuffer );
		m_NumBarriersToFlush = 0;
	}
}

void CommandContext::FlushResourceBarriers()
{
	if (m_NumBarriersToFlush == 0) return;
	m_CommandList->ResourceBarrier( m_NumBarriersToFlush, m_ResourceBarrierBuffer );
	m_NumBarriersToFlush = 0;
}

void CommandContext::BindDescriptorHeaps()
{
	UINT NonNullHeaps = 0;
	ID3D12DescriptorHeap* HeapsToBind[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	for (UINT i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		ID3D12DescriptorHeap* HeapItr = m_CurrentDescriptorHeaps[i];
		if (HeapItr != nullptr)
			HeapsToBind[NonNullHeaps++] = HeapItr;
	}
	if (NonNullHeaps > 0)
	{
		m_CommandList->SetDescriptorHeaps( NonNullHeaps, HeapsToBind );
	}
}

//--------------------------------------------------------------------------------------
// GraphicsContext
//--------------------------------------------------------------------------------------
void GraphicsContext::ClearUAV( GpuBuffer& Target )
{
	D3D12_GPU_DESCRIPTOR_HANDLE GpuVisibleHandle = m_DynamicDescriptorHeap.UploadDirect( Target.GetUAV() );
	const UINT ClearColor[4] = {};
	m_CommandList->ClearUnorderedAccessViewUint( GpuVisibleHandle, Target.GetUAV(), Target.GetResource(), ClearColor, 0, nullptr );
}

void GraphicsContext::ClearColor( ColorBuffer& Target )
{
	TransitionResource( Target, D3D12_RESOURCE_STATE_RENDER_TARGET, true );
	m_CommandList->ClearRenderTargetView( Target.GetRTV(), reinterpret_cast<float*>(&Target.GetClearColor()), 0, nullptr );
}

void GraphicsContext::ClearDepth( DepthBuffer& Target )
{
	TransitionResource( Target, D3D12_RESOURCE_STATE_DEPTH_WRITE, true );
	m_CommandList->ClearDepthStencilView( Target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, Target.GetClearDepth(), Target.GetClearStencil(), 0, nullptr );
}

void GraphicsContext::ClearStencil( DepthBuffer& Target )
{
	TransitionResource( Target, D3D12_RESOURCE_STATE_DEPTH_WRITE, true );
	m_CommandList->ClearDepthStencilView( Target.GetDSV(), D3D12_CLEAR_FLAG_STENCIL, Target.GetClearDepth(), Target.GetClearStencil(), 0, nullptr );
}

void GraphicsContext::ClearDepthAndStencil( DepthBuffer& Target )
{
	TransitionResource( Target, D3D12_RESOURCE_STATE_DEPTH_WRITE, true );
	m_CommandList->ClearDepthStencilView( Target.GetDSV(), D3D12_CLEAR_FLAG_STENCIL | D3D12_CLEAR_FLAG_DEPTH, Target.GetClearDepth(), Target.GetClearStencil(), 0, nullptr );
}

void GraphicsContext::BeginQuery( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT HeapIndex )
{
	m_CommandList->BeginQuery( QueryHeap, Type, HeapIndex );
}

void GraphicsContext::EndQuery( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT HeapIndex )
{
	m_CommandList->EndQuery( QueryHeap, Type, HeapIndex );
}

void GraphicsContext::ResolveQueryData( ID3D12QueryHeap* QueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource* DestinationBuffer, UINT64 DestinationBufferOffset )
{
	m_CommandList->ResolveQueryData( QueryHeap, Type, StartIndex, NumQueries, DestinationBuffer, DestinationBufferOffset );
}

void GraphicsContext::SetRenderTargets( UINT NumRTVs, ColorBuffer* RTVs, DepthBuffer* DSV /* = nullptr */, bool ReadOnlyDepth /* = false */ )
{
	D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[8];
	for (UINT i = 0; i < NumRTVs; ++i)
	{
		TransitionResource( RTVs[i], D3D12_RESOURCE_STATE_RENDER_TARGET );
		RTVHandles[i] = RTVs[i].GetRTV();
	}
	if (DSV)
	{
		if (ReadOnlyDepth)
		{
			TransitionResource( *DSV, D3D12_RESOURCE_STATE_DEPTH_READ );
			m_CommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, &DSV->GetDSV_DepthReadOnly() );
		}
		else
		{
			TransitionResource( *DSV, D3D12_RESOURCE_STATE_DEPTH_WRITE );
			m_CommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, &DSV->GetDSV() );
		}
	}
	else
	{
		m_CommandList->OMSetRenderTargets( NumRTVs, RTVHandles, FALSE, nullptr );
	}
}

void GraphicsContext::SetViewport( const D3D12_VIEWPORT& vp )
{
	m_CommandList->RSSetViewports( 1, &vp );
}

void GraphicsContext::SetViewports( UINT NumVPs, const D3D12_VIEWPORT* vps )
{
	m_CommandList->RSSetViewports( NumVPs, vps );
}

void GraphicsContext::SetScisor( const D3D12_RECT& rect )
{
	ASSERT( rect.left < rect.right && rect.top < rect.bottom );
	m_CommandList->RSSetScissorRects( 1, &rect );
}

void GraphicsContext::SetScisors( UINT NumScisor, const D3D12_RECT* rects )
{
	m_CommandList->RSSetScissorRects( NumScisor, rects );
}

//--------------------------------------------------------------------------------------
// ComputeContext
//--------------------------------------------------------------------------------------
ComputeContext& ComputeContext::Begin( const std::wstring& ID /* = L"" */, bool Async /* = false */ )
{
	ComputeContext& NewContext = Graphics::g_ContextMngr.AllocateContext(
		Async ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT )->GetComputeContext();
	NewContext.SetID( ID );
	return NewContext;
}

void ComputeContext::ClearUAV( GpuBuffer& Target )
{
	D3D12_GPU_DESCRIPTOR_HANDLE GpuVisibleHandle = m_DynamicDescriptorHeap.UploadDirect( Target.GetUAV() );
	const UINT ClearColor[4] = {};
	m_CommandList->ClearUnorderedAccessViewUint( GpuVisibleHandle, Target.GetUAV(), Target.GetResource(), ClearColor, 0, nullptr );
}