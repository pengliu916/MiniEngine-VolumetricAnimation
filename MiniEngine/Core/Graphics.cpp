#include "LibraryHeader.h"

#include "CommandContext.h"
#include "CmdListMngr.h"
#include "DescriptorHeap.h"
#include "LinearAllocator.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "SamplerMngr.h"
#include "CommandSignature.h"
#include "GpuResource.h"
#include "GuiRenderer.h"
#include "Graphics.h"
#include "FXAA.h"
#include "GPU_Profiler.h"
#include "imgui.h"
#include "TextRenderer.h"
#include "DX12Framework.h"

using namespace Microsoft::WRL;
using namespace std;
namespace
{
	// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
	// If no such adapter can be found, *ppAdapter will be set to nullptr.
	_Use_decl_annotations_
		void GetHardwareAdapter( IDXGIFactory4* pFactory, IDXGIAdapter3** ppAdapter )
	{
		ComPtr<IDXGIAdapter1> adapter;
		*ppAdapter = nullptr;
		bool found = false;

		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1( &desc );

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				// If you want a software adapter, pass in "/warp" on the command line.
				PRINTINFO( L"D3D12-capable hardware found:  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
				continue;
			}

			// Check to see if the adapter supports Direct3D 12, but don't create the
			// actual device yet.
			if (SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof( ID3D12Device ), nullptr ) ))
			{
				adapter->GetDesc1( &desc );
				PRINTINFO( L"D3D12-capable hardware found (selected):  %s (%u MB)", desc.Description, desc.DedicatedVideoMemory >> 20 );
				if (!found) adapter.Detach()->QueryInterface( ppAdapter );
				found = true;
			}
		}
		if (!found) adapter.Detach()->QueryInterface( ppAdapter );
	}
}

namespace Graphics
{
	Stats						g_stats;

	// Framework level gfx resource
	ComPtr<IDXGIFactory4>		g_factory;
	ComPtr<IDXGIAdapter3>		g_adaptor;
	ComPtr<ID3D12Device>		g_device;
	ComPtr<IDXGISwapChain3>		g_swapChain;
	CmdListMngr					g_cmdListMngr;
	ContextManager				g_ContextMngr;
	DescriptorHeap*				g_pRTVDescriptorHeap;
	DescriptorHeap*				g_pDSVDescriptorHeap;
	DescriptorHeap*				g_pSMPDescriptorHeap;
	DescriptorHeap*				g_pCSUDescriptorHeap;
	ColorBuffer*				g_pDisplayPlanes;
	uint32_t					g_CurrentDPIdx;
	D3D12_VIEWPORT				g_DisplayPlaneViewPort;
	D3D12_RECT					g_DisplayPlaneScissorRect;

	SamplerDesc					g_SamplerLinearClampDesc;
	SamplerDescriptor			g_SamplerLinearClamp;
	SamplerDesc					g_SamplerLinearWrapDesc;
	SamplerDescriptor			g_SamplerLinearWrap;

	D3D12_RASTERIZER_DESC		g_RasterizerDefault;
	D3D12_RASTERIZER_DESC		g_RasterizerDefaultCW;
	D3D12_RASTERIZER_DESC		g_RasterizerTwoSided;

	D3D12_BLEND_DESC			g_BlendNoColorWrite;
	D3D12_BLEND_DESC			g_BlendDisable;
	D3D12_BLEND_DESC			g_BlendPreMultiplied;
	D3D12_BLEND_DESC			g_BlendTraditional;
	D3D12_BLEND_DESC			g_BlendAdditive;
	D3D12_BLEND_DESC			g_BlendTraditionalAdditive;

	D3D12_DEPTH_STENCIL_DESC	g_DepthStateDisabled;
	D3D12_DEPTH_STENCIL_DESC	g_DepthStateReadWrite;
	D3D12_DEPTH_STENCIL_DESC	g_DepthStateReadOnly;
	D3D12_DEPTH_STENCIL_DESC	g_DepthStateReadOnlyReversed;
	D3D12_DEPTH_STENCIL_DESC	g_DepthStateTestEqual;

	ColorBuffer					g_SceneColorBuffer;
	DepthBuffer					g_SceneDepthBuffer;

	CommandSignature			g_DispatchIndirectCommandSignature(1);
	CommandSignature			g_DrawIndirectCommandSignature(1);

	RootSignature				s_PresentRS;
	GraphicsPSO					s_BufferCopyPSO;

	void Init()
	{
		// Initial system setting with default
		Core::g_config.enableFullScreen = false;
		Core::g_config.warpDevice = false;
		Core::g_config.vsync = false;
		Core::g_config.FXAA = true;
		Core::g_config.swapChainDesc = {};
		Core::g_config.swapChainDesc.Width = 1280;
		Core::g_config.swapChainDesc.Height = 800;
		Core::g_config.swapChainDesc.BufferCount = 5;
		Core::g_config.swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Core::g_config.swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Core::g_config.swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		Core::g_config.swapChainDesc.Stereo = FALSE;
		Core::g_config.swapChainDesc.SampleDesc.Count = 1;
		Core::g_config.swapChainDesc.SampleDesc.Quality = 0;
		Core::g_config.swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		Core::g_config.swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // Not used
		Core::g_config.swapChainDesc.Flags = 0;

		RootSignature::Initialize();
		PSO::Initialize();
		DynamicDescriptorHeap::Initialize();
		GuiRenderer::Initialize();
#ifndef RELEASE
		GPU_Profiler::Initialize();
#endif
	}

	void Shutdown()
	{
		GuiRenderer::Shutdown();
		FXAA::Shutdown();

		GPU_Profiler::ShutDown();
		TextRenderer::ShutDown();
		CommandContext::DestroyAllContexts();
		g_cmdListMngr.Shutdown();
		PSO::DestroyAll();
		RootSignature::DestroyAll();
		DynamicDescriptorHeap::Shutdown();

		LinearAllocator::DestroyAll();

		g_SceneColorBuffer.Destroy();
		g_SceneDepthBuffer.Destroy();
		for (uint8_t i = 0; i < Core::g_config.swapChainDesc.BufferCount; ++i)
			g_pDisplayPlanes[i].Destroy();

		delete g_pRTVDescriptorHeap;
		delete g_pDSVDescriptorHeap;
		delete g_pSMPDescriptorHeap;
		delete g_pCSUDescriptorHeap;

		delete[] g_pDisplayPlanes;

		g_swapChain.Reset();
		g_adaptor.Reset();
		g_factory.Reset();
#ifdef _DEBUG
		ID3D12DebugDevice* debugInterface;
		if (SUCCEEDED( g_device.Get()->QueryInterface( &debugInterface ) ))
		{
			debugInterface->ReportLiveDeviceObjects( D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL );
			debugInterface->Release();
		}
#endif
		g_device.Reset();
	}

	HRESULT CreateResource()
	{
		HRESULT hr;
#ifdef _DEBUG
		// Enable the D3D12 debug layer.
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ))
		{
			debugController->EnableDebugLayer();
		}
		else
		{
			PRINTWARN( L"Unable to enable D3D12 debug validation layer." )
		}
#endif

		// Create directx device
		VRET( CreateDXGIFactory1( IID_PPV_ARGS( &g_factory ) ) );
		if (Core::g_config.warpDevice)
		{
			VRET( g_factory->EnumWarpAdapter( IID_PPV_ARGS( &g_adaptor ) ) );
			VRET( D3D12CreateDevice( g_adaptor.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &g_device ) ) );
			PRINTWARN( L"Warp Device created." )
		}
		else
		{
			GetHardwareAdapter( g_factory.Get(), &g_adaptor );
			VRET( D3D12CreateDevice( g_adaptor.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &g_device ) ) );
		}

		// Check Direct3D 12 feature hardware support (more usage refer Direct3D 12 sdk Capability Querying)
		D3D12_FEATURE_DATA_D3D12_OPTIONS options;
		g_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) );
		switch (options.ResourceBindingTier)
		{
		case D3D12_RESOURCE_BINDING_TIER_1:
			PRINTWARN( L"Tier 1 is supported." );
			break;
		case D3D12_RESOURCE_BINDING_TIER_2:
			PRINTWARN( L"Tier 1 and 2 are supported." );
			break;
		case D3D12_RESOURCE_BINDING_TIER_3:
			PRINTWARN( L"Tier 1, 2 and 3 are supported." );
			break;
		default:
			break;
		}

		// Suppress specific validation layer warnings or errors
#if _DEBUG
		ID3D12InfoQueue* pInfoQueue = nullptr;
		if (SUCCEEDED( g_device.Get()->QueryInterface( IID_PPV_ARGS( &pInfoQueue ) ) ))
		{
			// Suppress whole categories of messages
			//D3D12_MESSAGE_CATEGORY Categories[] = {};

			// Suppress messages based on their severity level
			D3D12_MESSAGE_SEVERITY Severities[] =
			{
				D3D12_MESSAGE_SEVERITY_INFO
			};

			// Suppress individual messages by their ID
			D3D12_MESSAGE_ID DenyIds[] = {D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED};

			D3D12_INFO_QUEUE_FILTER NewFilter = {};
			//NewFilter.DenyList.NumCategories = _countof(Categories);
			//NewFilter.DenyList.pCategoryList = Categories;
			NewFilter.DenyList.NumSeverities = _countof( Severities );
			NewFilter.DenyList.pSeverityList = Severities;
			NewFilter.DenyList.NumIDs = _countof( DenyIds );
			NewFilter.DenyList.pIDList = DenyIds;

			pInfoQueue->PushStorageFilter( &NewFilter );
			pInfoQueue->Release();
		}
#endif

#ifndef RELEASE
		// Prevent the GPU from overclocking or underclocking to get consistent timings
		g_device->SetStablePowerState( TRUE );
#endif
		g_cmdListMngr.Create( g_device.Get() );

		g_pRTVDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
		g_pDSVDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_DSV, D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
		g_pSMPDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_SMP, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
		g_pCSUDescriptorHeap = new DescriptorHeap( g_device.Get(), Core::NUM_CSU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true );

		ASSERT( Core::g_config.swapChainDesc.BufferCount <= DXGI_MAX_SWAP_CHAIN_BUFFERS );
		// Create the swap chain
		ComPtr<IDXGISwapChain1> swapChain;
		// Swap chain needs the queue so that it can force a flush on it.
		VRET( g_factory->CreateSwapChainForHwnd( g_cmdListMngr.GetCommandQueue(), Core::g_hwnd, &Core::g_config.swapChainDesc, NULL, NULL, &swapChain ) );
		VRET( swapChain.As( &g_swapChain ) );
		DXDebugName( g_swapChain );
		// Create swapchain buffer resource
		g_pDisplayPlanes = new ColorBuffer[Core::g_config.swapChainDesc.BufferCount];
		for (uint8_t i = 0; i < Core::g_config.swapChainDesc.BufferCount; i++)
		{
			ComPtr<ID3D12Resource> DisplayPlane;
			VRET( g_swapChain->GetBuffer( i, IID_PPV_ARGS( &DisplayPlane ) ) );
			g_pDisplayPlanes[i].CreateFromSwapChain( L"SwapChain Buffer", DisplayPlane.Detach() );
		}
		g_CurrentDPIdx = g_swapChain->GetCurrentBackBufferIndex();

		uint32_t Width = Core::g_config.swapChainDesc.Width;
		uint32_t Height= Core::g_config.swapChainDesc.Height;

		// Create initial viewport and scissor rect
		g_DisplayPlaneViewPort.Width = static_cast<float>(Width);
		g_DisplayPlaneViewPort.Height = static_cast<float>(Height);
		g_DisplayPlaneViewPort.MaxDepth = 1.0f;

		g_DisplayPlaneScissorRect.right = static_cast<LONG>(Width);
		g_DisplayPlaneScissorRect.bottom = static_cast<LONG>(Height);

		// Enable or disable full screen
		if (!Core::g_config.enableFullScreen) VRET( g_factory->MakeWindowAssociation( Core::g_hwnd, DXGI_MWA_NO_ALT_ENTER ) );

		// Create the main scene related buffers
		g_SceneColorBuffer.Create( L"Main Color Buffer", Width, Height, 1, DXGI_FORMAT_R11G11B10_FLOAT );
		g_SceneDepthBuffer.Create( L"Scene Depth Buffer", Width, Height, DXGI_FORMAT_D32_FLOAT );

		// Create engine level predefined resource
		// Sampler states
		g_SamplerLinearClampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		g_SamplerLinearClampDesc.SetTextureAddressMode( D3D12_TEXTURE_ADDRESS_MODE_CLAMP );
		g_SamplerLinearClamp.Create( g_SamplerLinearClampDesc );
		g_SamplerLinearWrapDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		g_SamplerLinearWrapDesc.SetTextureAddressMode( D3D12_TEXTURE_ADDRESS_MODE_WRAP );
		g_SamplerLinearWrap.Create( g_SamplerLinearWrapDesc );

		// Rasterizer states
		g_RasterizerDefault.FillMode = D3D12_FILL_MODE_SOLID;
		g_RasterizerDefault.CullMode = D3D12_CULL_MODE_BACK;
		g_RasterizerDefault.FrontCounterClockwise = TRUE;
		g_RasterizerDefault.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		g_RasterizerDefault.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		g_RasterizerDefault.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		g_RasterizerDefault.DepthClipEnable = TRUE;
		g_RasterizerDefault.MultisampleEnable = FALSE;
		g_RasterizerDefault.AntialiasedLineEnable = FALSE;
		g_RasterizerDefault.ForcedSampleCount = 0;
		g_RasterizerDefault.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		g_RasterizerDefaultCW = g_RasterizerDefault;
		g_RasterizerDefaultCW.FrontCounterClockwise = FALSE;

		g_RasterizerTwoSided = g_RasterizerDefault;
		g_RasterizerTwoSided.CullMode = D3D12_CULL_MODE_NONE;

		// Blend states
		D3D12_BLEND_DESC alphaBlend = {};
		alphaBlend.IndependentBlendEnable = FALSE;
		alphaBlend.RenderTarget[0].BlendEnable = FALSE;
		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		alphaBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		alphaBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		alphaBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		alphaBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		alphaBlend.RenderTarget[0].RenderTargetWriteMask = 0;
		g_BlendNoColorWrite = alphaBlend;

		alphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		g_BlendDisable = alphaBlend;

		alphaBlend.RenderTarget[0].BlendEnable = TRUE;
		g_BlendTraditional = alphaBlend;

		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		g_BlendPreMultiplied = alphaBlend;

		alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		g_BlendAdditive = alphaBlend;

		alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		g_BlendTraditionalAdditive = alphaBlend;

		// Depth states
		g_DepthStateDisabled.DepthEnable = FALSE;
		g_DepthStateDisabled.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		g_DepthStateDisabled.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		g_DepthStateDisabled.StencilEnable = FALSE;
		g_DepthStateDisabled.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		g_DepthStateDisabled.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		g_DepthStateDisabled.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		g_DepthStateDisabled.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		g_DepthStateDisabled.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		g_DepthStateDisabled.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		g_DepthStateDisabled.BackFace = g_DepthStateDisabled.FrontFace;

		g_DepthStateReadWrite = g_DepthStateDisabled;
		g_DepthStateReadWrite.DepthEnable = TRUE;
		g_DepthStateReadWrite.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		g_DepthStateReadWrite.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

		g_DepthStateReadOnly = g_DepthStateReadWrite;
		g_DepthStateReadOnly.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

		g_DepthStateReadOnlyReversed = g_DepthStateReadOnly;
		g_DepthStateReadOnlyReversed.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

		g_DepthStateTestEqual = g_DepthStateReadOnly;
		g_DepthStateTestEqual.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;

		g_DispatchIndirectCommandSignature[0].Dispatch();
		g_DispatchIndirectCommandSignature.Finalize();

		g_DrawIndirectCommandSignature[0].Draw();
		g_DrawIndirectCommandSignature.Finalize();

		s_PresentRS.Reset( 1 );
		s_PresentRS[0].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1 );
		s_PresentRS.Finalize();

		ComPtr<ID3DBlob> QuadVS;
		ComPtr<ID3DBlob> CopyPS;
		uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3; 
		D3D_SHADER_MACRO macro[] =
		{
			{"QuadVS"					,	"1"}, // 0
			{"CopyPS"					,	"0"}, // 1
			{nullptr					,	nullptr}
		};
		V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "Graphics.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_1", compileFlags, 0, &QuadVS ) );
		macro[0].Definition = "0";
		macro[1].Definition = "1";
		V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "Graphics.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_1", compileFlags, 0, &CopyPS ) );

		s_BufferCopyPSO.SetRootSignature( s_PresentRS );
		s_BufferCopyPSO.SetRasterizerState( g_RasterizerTwoSided );
		s_BufferCopyPSO.SetBlendState( g_BlendDisable );
		s_BufferCopyPSO.SetDepthStencilState( g_DepthStateDisabled );
		s_BufferCopyPSO.SetSampleMask( 0xFFFFFFFF );
		s_BufferCopyPSO.SetInputLayout( 0, nullptr );
		s_BufferCopyPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
		s_BufferCopyPSO.SetVertexShader( QuadVS->GetBufferPointer(), QuadVS->GetBufferSize() );
		s_BufferCopyPSO.SetPixelShader( CopyPS->GetBufferPointer(), CopyPS->GetBufferSize() );
		s_BufferCopyPSO.SetRenderTargetFormats(1, &Core::g_config.swapChainDesc.Format, DXGI_FORMAT_UNKNOWN);
		s_BufferCopyPSO.Finalize();

#ifndef RELEASE
		GPU_Profiler::CreateResource();
#endif
		// Create graphics resources for text renderer
		TextRenderer::CreateResource();
		GuiRenderer::CreateResource();
		FXAA::CreateResource();

		return S_OK;
	}

	HRESULT CompileShaderFromFile( LPCWSTR pFileName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
		LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode )
	{
		HRESULT hr;
#if defined( DEBUG ) || defined( _DEBUG )
		// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
		// Setting this flag improves the shader debugging experience, but still allows 
		// the shaders to be optimized and to run exactly the way they will run in 
		// the release configuration of this program.
		Flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		ID3DBlob* pErrorBlob = nullptr;
		hr = D3DCompileFromFile( pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, &pErrorBlob );
		if (pErrorBlob)
		{
			PRINTERROR( reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()) );
			pErrorBlob->Release();
		}

		return hr;
	}

	void Resize()
	{
		HRESULT hr;
		uint32_t Width = Core::g_config.swapChainDesc.Width;
		uint32_t Height = Core::g_config.swapChainDesc.Height;

		g_DisplayPlaneViewPort.Width = static_cast<float>(Width);
		g_DisplayPlaneViewPort.Height = static_cast<float>(Height);

		g_DisplayPlaneScissorRect.right = static_cast<LONG>(Width);
		g_DisplayPlaneScissorRect.bottom = static_cast<LONG>(Height);

		g_cmdListMngr.IdleGPU();
		
		g_SceneColorBuffer.Destroy();
		g_SceneDepthBuffer.Destroy();

		g_SceneColorBuffer.Create( L"Main Color Buffer", Width, Height, 1, DXGI_FORMAT_R11G11B10_FLOAT );
		g_SceneDepthBuffer.Create( L"Scene Depth Buffer", Width, Height, DXGI_FORMAT_D32_FLOAT );

		for (uint8_t i = 0; i < Core::g_config.swapChainDesc.BufferCount; i++)
			g_pDisplayPlanes[i].Destroy();

		V( g_swapChain->ResizeBuffers( Core::g_config.swapChainDesc.BufferCount,
			Core::g_config.swapChainDesc.Width,
			Core::g_config.swapChainDesc.Height,
			Core::g_config.swapChainDesc.Format,
			Core::g_config.swapChainDesc.Flags ) );

		for (uint8_t i = 0; i < Core::g_config.swapChainDesc.BufferCount; i++)
		{
			ComPtr<ID3D12Resource> DisplayPlane;
			V( g_swapChain->GetBuffer( i, IID_PPV_ARGS( &DisplayPlane ) ) );
			g_pDisplayPlanes[i].CreateFromSwapChain( L"SwapChain Buffer", DisplayPlane.Detach() );
		}

		g_CurrentDPIdx = g_swapChain->GetCurrentBackBufferIndex();

		FXAA::Resize();
	}

	void Present( CommandContext& EngineContext )
	{
		HRESULT hr;
		GraphicsContext& Context = EngineContext.GetGraphicsContext();
		{
			GPU_PROFILE( Context, L"Copy To BackBuffer" );
			Context.TransitionResource( g_SceneColorBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
			Context.SetRootSignature( s_PresentRS );
			Context.SetPipelineState( s_BufferCopyPSO );
			Context.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			Context.SetDynamicDescriptors( 0, 0, 1, &g_SceneColorBuffer.GetSRV() );
			Context.SetRenderTargets( 1, &g_pDisplayPlanes[g_CurrentDPIdx] );
			Context.SetViewport( g_DisplayPlaneViewPort );
			Context.SetScisor( g_DisplayPlaneScissorRect );
			Context.Draw( 3 );
		}

		GuiRenderer::Render( Context );

#ifndef RELEASE
		GPU_Profiler::ProcessAndReadback( EngineContext );
		GPU_Profiler::DrawStats( Context );
#endif

		Context.TransitionResource( g_pDisplayPlanes[g_CurrentDPIdx], D3D12_RESOURCE_STATE_PRESENT );
		g_stats.lastFrameEndFence = Context.Finish();

		DXGI_PRESENT_PARAMETERS param;
		param.DirtyRectsCount = 0;
		param.pDirtyRects = NULL;
		param.pScrollRect = NULL;
		param.pScrollOffset = NULL;

		// Present the frame.
		V( g_swapChain->Present1( Core::g_config.vsync ? 1 : 0, 0, &param ) );
		g_CurrentDPIdx = (g_CurrentDPIdx + 1) % Core::g_config.swapChainDesc.BufferCount;
	}

	void UpdateGUI()
	{
		if (ImGui::CollapsingHeader( "Stats", (const char*)0, true, true ))
		{
			HRESULT hr;
			V( Graphics::g_adaptor->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Graphics::g_stats.localVideoMemoryInfo ) );
			float memBudget = Graphics::g_stats.localVideoMemoryInfo.Budget / 1024.f / 1024.f;
			float memUsed = Graphics::g_stats.localVideoMemoryInfo.CurrentUsage / 1024.f / 1024.f;
			float usedMemPct = memUsed / memBudget;
			char buf[32];
			sprintf( buf, "%4.2fMB/%4.fMB", memUsed, memBudget );
			ImGui::Text( "GPU memory usage" );
			ImGui::ProgressBar( usedMemPct, ImVec2( -1.f, 0.f ), buf );

			ImGui::Columns( 3, "cmdAllocatorInfo" );
			ImGui::Separator();
			ImGui::Text( "Allocator Type" ); ImGui::NextColumn();
			ImGui::Text( "Created" ); ImGui::NextColumn();
			ImGui::Text( "Ready" ); ImGui::NextColumn();
			ImGui::Separator();
			const char* name[4] = {"Direct", "Bundle", "Compute", "Copy"};
			for (int i = 0; i < 4; ++i)
			{
				//ImGui::NextColumn();
				ImGui::Text( name[i] ); ImGui::NextColumn();
				ImGui::Text( "%d", Graphics::g_stats.allocatorCreated[i] ); ImGui::NextColumn();
				ImGui::Text( "%d", Graphics::g_stats.allocatorReady[i] ); ImGui::NextColumn();
			}
			ImGui::Columns( 1 );
			ImGui::Separator();
			ImGui::Text( "RenderThread Stall Count: %d/frame  Time:%4.2fms", Graphics::g_stats.cpuStallCountPerFrame, Graphics::g_stats.cpuStallTimePerFrame );
			Graphics::g_stats.cpuStallCountPerFrame = 0;
			Graphics::g_stats.cpuStallTimePerFrame = 0;
		}
	}
}