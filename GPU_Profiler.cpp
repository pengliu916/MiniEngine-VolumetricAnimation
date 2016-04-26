#include "LibraryHeader.h"
#include "DX12Framework.h"
#include "Utility.h"
#include "DXHelper.h"
#include "CommandContext.h"
#include "CmdListMngr.h"
#include "TextRenderer.h"
#include "Graphics.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include "GPU_Profiler.h"

using namespace Microsoft::WRL;
using namespace std;
using namespace DirectX;

namespace {

	double							m_GPUTickDelta;
	uint8_t							m_timerCount;
	unordered_map<wstring, uint8_t>	m_timerNameIdxMap;
	wstring*						m_timerNameArray;
	XMFLOAT4*						m_timerColorArray;
	uint64_t*						m_timeStampBufferCopy;

	ID3D12Resource*					m_readbackBuffer;
	ID3D12QueryHeap*				m_queryHeap;
	uint64_t*						m_timeStampBuffer;

	uint64_t						m_fence = 0;

	RootSignature					m_RootSignature;
	GraphicsPSO						m_GraphPSO;

	CRITICAL_SECTION				m_critialSection;

	uint8_t							m_ResolveStampIdx = GPU_Profiler::MAX_TIMER_COUNT;

	vector<uint8_t>					m_ActiveTimer;

	bool compStartTime( uint8_t i, uint8_t j )
	{
		return m_timeStampBufferCopy[i * 2] < m_timeStampBufferCopy[j * 2];
	}

	struct RectAttr
	{
		XMFLOAT4	TLBR;
		XMFLOAT4	Col;
	};

	RectAttr*							m_RectData;
	uint16_t							m_BackgroundMargin;
	uint16_t							m_EntryMargin;
	uint16_t							m_EntryHeight;
	uint16_t							m_EntryWordHeight;
	uint16_t							m_MaxBarWidth;
	uint16_t							m_WorldSpace;
}

void GPU_Profiler::Initialize()
{
	m_BackgroundMargin = 5;
	m_EntryMargin = 5;
	m_EntryWordHeight = 15;
	m_EntryHeight = 2 * m_EntryMargin + m_EntryWordHeight;
	m_MaxBarWidth = 500;
	m_WorldSpace = 200;

	// Initialize the array to store all timer name
	m_timerNameArray = new wstring[MAX_TIMER_COUNT];

	// Initialize the array to copy from timer buffer
	m_timeStampBufferCopy = new uint64_t[MAX_TIMER_COUNT];

	m_timerColorArray = new XMFLOAT4[MAX_TIMER_COUNT];

	m_RectData = new RectAttr[MAX_TIMER_COUNT + 5];

	m_ActiveTimer.reserve( MAX_TIMER_COUNT );

	// Initialize output critical section
	InitializeCriticalSection( &m_critialSection );
}

HRESULT GPU_Profiler::CreateResource()
{
	HRESULT hr;
	uint64_t freq;
	Graphics::g_cmdListMngr.GetCommandQueue()->GetTimestampFrequency( &freq );
	m_GPUTickDelta = 1000.0 / static_cast<double>(freq);

	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.Type = D3D12_HEAP_TYPE_READBACK;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC BufferDesc;
	BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	BufferDesc.Alignment = 0;
	BufferDesc.Width = sizeof( uint64_t ) * MAX_TIMER_COUNT * 2;
	BufferDesc.Height = 1;
	BufferDesc.DepthOrArraySize = 1;
	BufferDesc.MipLevels = 1;
	BufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	BufferDesc.SampleDesc.Count = 1;
	BufferDesc.SampleDesc.Quality = 0;
	BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	BufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	VRET( Graphics::g_device->CreateCommittedResource( &HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_readbackBuffer ) ) );
	m_readbackBuffer->SetName( L"GPU_Profiler Readback Buffer" );

	D3D12_QUERY_HEAP_DESC QueryHeapDesc;
	QueryHeapDesc.Count = MAX_TIMER_COUNT * 2;
	QueryHeapDesc.NodeMask = 1;
	QueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	VRET( Graphics::g_device->CreateQueryHeap( &QueryHeapDesc, IID_PPV_ARGS( &m_queryHeap ) ) );
	PRINTINFO( "QueryHeap created" );
	m_queryHeap->SetName( L"GPU_Profiler QueryHeap" );

	// Create resource for drawing perf graph
	m_RootSignature.Reset( 1 );
	m_RootSignature[0].InitAsBufferSRV( 0, D3D12_SHADER_VISIBILITY_VERTEX );
	m_RootSignature.Finalize();

	m_GraphPSO.SetRootSignature( m_RootSignature );
	m_GraphPSO.SetRasterizerState( Graphics::g_RasterizerDefault );
	m_GraphPSO.SetBlendState( Graphics::g_BlendTraditional );
	m_GraphPSO.SetDepthStencilState( Graphics::g_DepthStateDisabled );
	m_GraphPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	m_GraphPSO.SetRenderTargetFormats( 1, &Graphics::g_pDisplayPlanes[0].GetFormat(), DXGI_FORMAT_UNKNOWN );

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	uint32_t compileFlags = 0;

	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "GPU_Profiler.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "GPU_Profiler.hlsl" ) ).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );

	m_GraphPSO.SetVertexShader( vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() );
	m_GraphPSO.SetPixelShader( pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() );
	m_GraphPSO.Finalize();

	return S_OK;
}

void GPU_Profiler::ShutDown()
{
	if (m_readbackBuffer != nullptr) m_readbackBuffer->Release();
	if (m_queryHeap != nullptr) m_queryHeap->Release();
	m_ActiveTimer.clear();
	delete[] m_RectData;
	delete[] m_timerNameArray;
	delete[] m_timerColorArray;
	delete[] m_timeStampBufferCopy;
	DeleteCriticalSection( &m_critialSection );
}

void GPU_Profiler::ProcessAndReadback( CommandContext& EngineContext )
{
	Graphics::g_cmdListMngr.WaitForFence( m_fence );

	HRESULT hr;
	D3D12_RANGE range;
	range.Begin = 0;
	range.End = MAX_TIMER_COUNT * 2 * sizeof( uint64_t );
	V( m_readbackBuffer->Map( 0, &range, reinterpret_cast<void**>(&m_timeStampBuffer) ) );
	memcpy( m_timeStampBufferCopy, m_timeStampBuffer, m_timerCount * 2 * sizeof( uint64_t ) );
	D3D12_RANGE EmptyRange = {};
	m_readbackBuffer->Unmap( 0, &EmptyRange );

	// based on previous frame end timestamp, creat an active timer idx vector
	m_ActiveTimer.clear();
	uint64_t preEndTime = m_ResolveStampIdx == MAX_TIMER_COUNT ? 0 : m_timeStampBufferCopy[m_ResolveStampIdx * 2 + 1];
	for (uint8_t idx = 0; idx < m_timerCount; ++idx)
	{
		if (m_timeStampBufferCopy[idx * 2] > preEndTime && idx != m_ResolveStampIdx)
			m_ActiveTimer.push_back( idx );
	}
	// sort timer based on timer's start time
	sort( m_ActiveTimer.begin(), m_ActiveTimer.end(), compStartTime );

	{
		GPU_PROFILE( EngineContext, L"ResolveQuery" );
		EngineContext.ResolveTimeStamps( m_readbackBuffer, m_queryHeap, 2 * m_timerCount );
	}
	m_fence = EngineContext.Flush();
}

uint16_t GPU_Profiler::FillVertexData()
{
	uint16_t instanceCount = 0;
	float marginPixel = 10.f;
	float ViewWidth = (float)Core::g_config.swapChainDesc.Width;
	float ViewHeight = (float)Core::g_config.swapChainDesc.Height;
	const float vpX = 0.0f;
	const float vpY = 0.0f;
	const float scaleX = 2.0f / ViewWidth;
	const float scaleY = -2.0f / ViewHeight;

	const float offsetX = -vpX*scaleX - 1.f;
	const float offsetY = -vpY*scaleY + 1.f;

	auto Corner = [&]( UINT TLx, UINT TLy, UINT BRx, UINT BRy )->XMFLOAT4 {
		return XMFLOAT4( TLx*scaleX + offsetX, TLy*scaleY + offsetY, BRx*scaleX + offsetX, BRy*scaleY + offsetY );
	};

	uint8_t NumActiveTimer = (uint8_t)m_ActiveTimer.size();
	uint8_t RectIdx = 0;
	m_RectData[RectIdx].TLBR = Corner( m_BackgroundMargin, m_BackgroundMargin, m_MaxBarWidth + m_WorldSpace, m_BackgroundMargin + NumActiveTimer*m_EntryHeight );
	m_RectData[RectIdx++].Col = XMFLOAT4( 0.f, 0.f, 0.f, 0.3f );
	instanceCount++;

	float scale = m_MaxBarWidth / 33.f;
	if (m_ActiveTimer.size() > 0)
	{
		double FrameStartTime = m_timeStampBufferCopy[m_ActiveTimer[0] * 2] * m_GPUTickDelta;
		uint16_t CurStartX = m_BackgroundMargin + m_EntryMargin + m_WorldSpace;
		uint16_t CurStartY = m_BackgroundMargin + m_EntryMargin;
		for (uint32_t idx = 0; idx < NumActiveTimer; idx++)
		{
			double LocalStartTime = m_timeStampBufferCopy[m_ActiveTimer[idx] * 2] * m_GPUTickDelta - FrameStartTime;
			double LocalEndTime = m_timeStampBufferCopy[m_ActiveTimer[idx] * 2 + 1] * m_GPUTickDelta - FrameStartTime;
			m_RectData[RectIdx].TLBR = Corner( CurStartX + (UINT)(LocalStartTime*scale), CurStartY, CurStartX + (UINT)(LocalEndTime*scale), CurStartY + m_EntryWordHeight );
			CurStartY += m_EntryHeight;
			m_RectData[RectIdx++].Col = m_timerColorArray[m_ActiveTimer[idx]];
			instanceCount++;
		}
	}
	return instanceCount;
}

void GPU_Profiler::DrawStats( GraphicsContext& gfxContext )
{
	uint16_t instanceCount = FillVertexData();
	gfxContext.SetRootSignature( m_RootSignature );
	gfxContext.SetPipelineState( m_GraphPSO );
	gfxContext.SetDynamicSRV( 0, sizeof( RectAttr )*(m_timerCount + 5), m_RectData );
	gfxContext.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	gfxContext.SetRenderTargets( 1, &Graphics::g_pDisplayPlanes[Graphics::g_CurrentDPIdx] );
	gfxContext.SetViewport( Graphics::g_DisplayPlaneViewPort );
	gfxContext.SetScisor( Graphics::g_DisplayPlaneScissorRect );
	gfxContext.DrawInstanced( 4, 1 );
	gfxContext.DrawInstanced( 4, instanceCount, 0, 1 );

	TextContext txtContext( gfxContext );
	txtContext.Begin();
	float curX = (float)(m_BackgroundMargin + m_EntryMargin);
	float curY = (float)(m_BackgroundMargin + m_EntryMargin);
	txtContext.ResetCursor( curX, curY );
	txtContext.SetTextSize( (float)m_EntryWordHeight );
	for (uint32_t idx = 0; idx < m_ActiveTimer.size(); idx++)
	{
		wchar_t temp[128];
		GPU_Profiler::GetTimingStr( m_ActiveTimer[idx], temp );
		txtContext.DrawString( wstring( temp ) );
		curY += m_EntryHeight;
		txtContext.ResetCursor( curX, curY );
	}
	txtContext.End();
}

double GPU_Profiler::ReadTimer( uint8_t idx, double* start, double* stop )
{
	ASSERT( idx < MAX_TIMER_COUNT );

	double _start = m_timeStampBufferCopy[idx * 2] * m_GPUTickDelta;
	double _stop = m_timeStampBufferCopy[idx * 2 + 1] * m_GPUTickDelta;
	if (start) *start = _start;
	if (stop) *stop = _stop;
	return _stop - _start;
}

uint16_t GPU_Profiler::GetTimingStr( uint8_t idx, wchar_t* outStr )
{
	ASSERT( idx < MAX_TIMER_COUNT );
	if (m_timerNameArray[idx].length() == 0)
		return 0;
	double result = m_timeStampBufferCopy[idx * 2 + 1] * m_GPUTickDelta - m_timeStampBufferCopy[idx * 2] * m_GPUTickDelta;
	swprintf( outStr, L"%-15.15s:%4.2fms \0", m_timerNameArray[idx].c_str(), result );
	return (uint16_t)wcslen( outStr );
}

GPUProfileScope::GPUProfileScope( CommandContext& Context, const wchar_t* szName )
	:m_Context( Context )
{
	Context.PIXBeginEvent( szName );
	auto iter = m_timerNameIdxMap.find( szName );
	if (iter == m_timerNameIdxMap.end())
	{
		CriticalSectionScope lock( &m_critialSection );
		if (m_ResolveStampIdx == GPU_Profiler::MAX_TIMER_COUNT)
		{
			if (wcscmp( szName, L"ResolveQuery" ) == 0)
				m_ResolveStampIdx = m_timerCount;
		}
		m_idx = m_timerCount;
		m_timerNameArray[m_idx] = szName;
		m_timerColorArray[m_idx] = XMFLOAT4( (float)rand() / RAND_MAX, (float)rand() / RAND_MAX, (float)rand() / RAND_MAX, 0.8f );
		m_timerNameIdxMap[szName] = m_timerCount++;
	}
	else
	{
		m_idx = iter->second;
	}
	ASSERT( m_idx < GPU_Profiler::MAX_TIMER_COUNT );
	m_Context.InsertTimeStamp( m_queryHeap, m_idx * 2 );
}

GPUProfileScope::~GPUProfileScope()
{
	ASSERT( m_idx < GPU_Profiler::MAX_TIMER_COUNT );
	m_Context.InsertTimeStamp( m_queryHeap, m_idx * 2 + 1 );
	m_Context.PIXEndEvent();
}