#include "stdafx.h"
#include "DenseVolume.h"
#include <ppl.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace std;

DenseVolume::DenseVolume()
	:m_BgThread()
{
	for (int i = 0; i < kNumBufferType; ++i)
	{
		m_CBData[i].shiftingColVals[0] = int4( 1, 0, 0, 0 );
		m_CBData[i].shiftingColVals[1] = int4( 0, 1, 0, 1 );
		m_CBData[i].shiftingColVals[2] = int4( 0, 0, 1, 2 );
		m_CBData[i].shiftingColVals[3] = int4( 1, 1, 0, 3 );
		m_CBData[i].shiftingColVals[4] = int4( 1, 0, 1, 4 );
		m_CBData[i].shiftingColVals[5] = int4( 0, 1, 1, 5 );
		m_CBData[i].shiftingColVals[6] = int4( 1, 1, 1, 6 );
	}
}

DenseVolume::~DenseVolume()
{
	if (m_BgThread.joinable()) m_BgThread.join();
}

void DenseVolume::OnCreateResource()
{
	HRESULT hr;
	ASSERT( Graphics::g_device );

	// Feature support checking
	D3D12_FEATURE_DATA_D3D12_OPTIONS FeatureData;
	ZeroMemory( &FeatureData, sizeof( FeatureData ) );
	V( Graphics::g_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &FeatureData, sizeof( FeatureData ) ) );
	if (SUCCEEDED( hr ))
	{
		// TypedUAVLoadAdditionalFormats contains a Boolean that tells you whether the feature is supported or not
		if (FeatureData.TypedUAVLoadAdditionalFormats)
		{
			// Can assume “all-or-nothing” subset is supported (e.g. R32G32B32A32_FLOAT)
			// Cannot assume other formats are supported, so we check:
			D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport = {DXGI_FORMAT_R8G8B8A8_UINT, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE};
			hr = Graphics::g_device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &FormatSupport, sizeof( FormatSupport ) );
			if (SUCCEEDED( hr ) && (FormatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
			{
				PRINTINFO( "DXGI_FORMAT_R8G8B8A8_UINT typed load is supported" );
				m_TypeLoadSupported = true;
			}
			else
				PRINTWARN( "DXGI_FORMAT_R8G8B8A8_UINT typed load is not supported" );
		}
		else
			PRINTWARN( "TypedUAVLoadAdditionalFormats load is not supported" );
	}

	// Compile Shaders
	ComPtr<ID3DBlob> BoundingCubeVS;
	ComPtr<ID3DBlob> RaycastPS[kNumBufferType];
	ComPtr<ID3DBlob> VolumeUpdateCS[kNumBufferType];

	uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	D3D_SHADER_MACRO macro[] =
	{
		{"__hlsl",			"1"},	// 0 
		{"VERTEX_SHADER",	"0"},	// 1 
		{"PIXEL_SHADER",	"0"},	// 2 
		{"COMPUTE_SHADER",	"0"},	// 3
		{"TYPED_UAV",		"0"},	// 4
		{"TYPED_LOAD_NOT_SUPPORTED", m_TypeLoadSupported ? "0" : "1"},	// 5
		{nullptr,		nullptr}
	};
	macro[1].Definition = "1"; // VERTEX_SHADER
	V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "DenseVolume.hlsl" ) ).c_str(),
		macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs_boundingcube_main", "vs_5_1", compileFlags, 0, &BoundingCubeVS ) );
	macro[1].Definition = "0"; // VERTEX_SHADER
	char temp[8];
	for (int i = 0; i < kNumBufferType; ++i)
	{
		sprintf( temp, "%d", i );
		macro[4].Definition = temp; // TYPED_UAV
		macro[2].Definition = "1"; // PIXEL_SHADER
		V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "DenseVolume.hlsl" ) ).c_str(),
			macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main", "ps_5_1", compileFlags, 0, &RaycastPS[i] ) );
		macro[2].Definition = "0"; // PIXEL_SHADER
		macro[3].Definition = "1"; // COMPUTE_SHADER
		V( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "DenseVolume.hlsl" ) ).c_str(),
			macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "cs_volumeupdate_main", "cs_5_1", compileFlags, 0, &VolumeUpdateCS[i] ) );
		macro[3].Definition = "0"; // COMPUTE_SHADER
	}

	// Create Rootsignature
	m_RootSignature.Reset( 3 );
	m_RootSignature[0].InitAsConstantBuffer( 0 );
	m_RootSignature[1].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1 );
	m_RootSignature[2].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1 );
	m_RootSignature.Finalize( D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS );

	m_GfxRenderPSO[kStructuredBuffer].SetRootSignature( m_RootSignature );
	m_CptUpdatePSO[kStructuredBuffer].SetRootSignature( m_RootSignature );

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	m_GfxRenderPSO[kStructuredBuffer].SetInputLayout( _countof( inputElementDescs ), inputElementDescs );
	m_GfxRenderPSO[kStructuredBuffer].SetRasterizerState( Graphics::g_RasterizerDefault );
	m_GfxRenderPSO[kStructuredBuffer].SetBlendState( Graphics::g_BlendDisable );
	m_GfxRenderPSO[kStructuredBuffer].SetDepthStencilState( Graphics::g_DepthStateReadWrite );
	m_GfxRenderPSO[kStructuredBuffer].SetSampleMask( UINT_MAX );
	m_GfxRenderPSO[kStructuredBuffer].SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
	m_GfxRenderPSO[kStructuredBuffer].SetRenderTargetFormats( 1, &ColorFormat, DepthFormat );
	m_GfxRenderPSO[kStructuredBuffer].SetVertexShader( BoundingCubeVS->GetBufferPointer(), BoundingCubeVS->GetBufferSize() );
	m_GfxRenderPSO[kTypedBuffer] = m_GfxRenderPSO[kStructuredBuffer];
	m_GfxRenderPSO[kStructuredBuffer].SetPixelShader( RaycastPS[kStructuredBuffer]->GetBufferPointer(), RaycastPS[kStructuredBuffer]->GetBufferSize() );
	m_GfxRenderPSO[kTypedBuffer].SetPixelShader( RaycastPS[kTypedBuffer]->GetBufferPointer(), RaycastPS[kTypedBuffer]->GetBufferSize() );

	m_CptUpdatePSO[kTypedBuffer] = m_CptUpdatePSO[kStructuredBuffer];
	m_CptUpdatePSO[kTypedBuffer].SetComputeShader( VolumeUpdateCS[kTypedBuffer]->GetBufferPointer(), VolumeUpdateCS[kTypedBuffer]->GetBufferSize() );
	m_CptUpdatePSO[kStructuredBuffer].SetComputeShader( VolumeUpdateCS[kStructuredBuffer]->GetBufferPointer(), VolumeUpdateCS[kStructuredBuffer]->GetBufferSize() );

	m_GfxRenderPSO[kStructuredBuffer].Finalize();
	m_GfxRenderPSO[kTypedBuffer].Finalize();
	m_CptUpdatePSO[kStructuredBuffer].Finalize();
	m_CptUpdatePSO[kTypedBuffer].Finalize();

	// Create Buffer Resources
	uint32_t volumeBufferElementCount = m_DepthInUse*m_HeightInUse*m_WidthInUse;
	if (m_BufferTypeInUse == kTypedBuffer)
		m_TypedVolBuf[m_OnStageIdx].Create( L"Typed Volume Buf", volumeBufferElementCount, 4 * sizeof( uint8_t ) );
	else
		m_StructuredVolBuf[m_OnStageIdx].Create( L"Volume Buffer", volumeBufferElementCount, 4 * sizeof( uint8_t ) );

	// Create the initial volume 
	m_State.store( kNewBufferCooking, memory_order_release );
	if (m_BgThread.joinable()) m_BgThread.join();
	m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_WidthInUse, m_HeightInUse, m_DepthInUse,
		m_BufferTypeInUse, m_BufferTypeInUse, m_VolContentInUse );

	// Define the geometry for a triangle.
	XMFLOAT3 cubeVertices[] =
	{
		{XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE )},
		{XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE )},
	};

	const uint32_t vertexBufferSize = sizeof( cubeVertices );
	m_VertexBuffer.Create( L"Vertex Buffer", ARRAYSIZE( cubeVertices ), sizeof( XMFLOAT3 ), (void*)cubeVertices );

	uint16_t cubeIndices[] =
	{
		0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
	};

	m_IndexBuffer.Create( L"Index Buffer", ARRAYSIZE( cubeIndices ), sizeof( uint16_t ), (void*)cubeIndices );
}

void DenseVolume::OnRender( CommandContext& cmdContext, DirectX::XMMATRIX wvp, DirectX::XMFLOAT4 eyePos )
{
	switch (m_State.load( memory_order_acquire ))
	{
	case kNewBufferReady:
		m_BufferTypeInUse = m_NewBufferType;
		m_WidthInUse = m_NewWidth;
		m_HeightInUse = m_NewHeight;
		m_DepthInUse = m_NewDepth;
		m_OnStageIdx = 1 - m_OnStageIdx;
		m_FenceValue = Graphics::g_stats.lastFrameEndFence;
		m_State.store( kRetiringOldBuffer, memory_order_release );
		break;
	case kRetiringOldBuffer:
		if (Graphics::g_cmdListMngr.IsFenceComplete( m_FenceValue ))
			m_State.store( kOldBufferRetired, memory_order_release );
		break;
	}
	m_CBData[m_OnStageIdx].wvp = wvp;
	m_CBData[m_OnStageIdx].viewPos = eyePos;
	GpuBuffer* VolumeBuffer = (m_BufferTypeInUse == kStructuredBuffer ?
		(GpuBuffer*)&m_StructuredVolBuf[m_OnStageIdx] : (GpuBuffer*)&m_TypedVolBuf[m_OnStageIdx]);
	ComputeContext& cptContext = cmdContext.GetComputeContext();
	{
		GPU_PROFILE( cptContext, L"Volume Updating" );
		cptContext.SetRootSignature( m_RootSignature );
		cptContext.SetPipelineState( m_CptUpdatePSO[m_BufferTypeInUse] );
		cptContext.TransitionResource( *VolumeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
		cptContext.SetDynamicConstantBufferView( 0, sizeof( DataCB ), (void*)&m_CBData[m_OnStageIdx] );
		cptContext.SetDynamicDescriptors( 1, 0, 1, &VolumeBuffer->GetSRV() );
		cptContext.SetDynamicDescriptors( 2, 0, 1, &VolumeBuffer->GetUAV() );
		cptContext.Dispatch( m_WidthInUse / THREAD_X, m_HeightInUse / THREAD_Y, m_DepthInUse / THREAD_Z );
	}
	GraphicsContext& gfxContext = cmdContext.GetGraphicsContext();
	{
		GPU_PROFILE( gfxContext, L"Rendering" );
		gfxContext.SetRootSignature( m_RootSignature );
		gfxContext.SetPipelineState( m_GfxRenderPSO[m_BufferTypeInUse] );
		gfxContext.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		gfxContext.TransitionResource( *VolumeBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		gfxContext.SetDynamicConstantBufferView( 0, sizeof( DataCB ), (void*)&m_CBData[m_OnStageIdx] );
		gfxContext.SetDynamicDescriptors( 1, 0, 1, &VolumeBuffer->GetSRV() );
		gfxContext.SetDynamicDescriptors( 2, 0, 1, &VolumeBuffer->GetUAV() );
		gfxContext.SetRenderTargets( 1, &Graphics::g_SceneColorBuffer, &Graphics::g_SceneDepthBuffer );
		gfxContext.SetViewport( Graphics::g_DisplayPlaneViewPort );
		gfxContext.SetScisor( Graphics::g_DisplayPlaneScissorRect );
		gfxContext.SetVertexBuffer( 0, m_VertexBuffer.VertexBufferView() );
		gfxContext.SetIndexBuffer( m_IndexBuffer.IndexBufferView() );
		gfxContext.DrawIndexed( 36 );
	}
}

void DenseVolume::RenderGui()
{
	static bool showPenal = true;
	if (ImGui::CollapsingHeader( "Dense Volume", 0, true, true ))
	{
		ImGui::Text( "Buffer Settings:" );
		static int uBufferChoice = m_BufferTypeInUse;
		ImGui::RadioButton( "Use Typed Buffer", &uBufferChoice, kTypedBuffer );
		ImGui::RadioButton( "Use Structured Buffer", &uBufferChoice, kStructuredBuffer );
		if (uBufferChoice != m_BufferTypeInUse && m_State.load( memory_order_acquire ) == kNormal)
		{
			m_State.store( kNewBufferCooking, memory_order_release );
			m_NewBufferType = (BufferType)uBufferChoice;
			if (m_BgThread.joinable()) m_BgThread.join();
			m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_WidthInUse, m_HeightInUse, m_DepthInUse,
				m_NewBufferType, m_BufferTypeInUse, m_VolContentInUse );
		}
		ImGui::Separator();

		ImGui::Text( "Volume Animation Settings:" );
		static int uVolContent = m_VolContentInUse;
		ImGui::RadioButton( "Sphere Animation", &uVolContent, kSphere );
		ImGui::RadioButton( "Cube Animation", &uVolContent, kDimond );
		if (uVolContent != m_VolContentInUse && m_State.load( memory_order_acquire ) == kNormal)
		{
			m_State.store( kNewBufferCooking, memory_order_release );
			m_NewVolContent = (VolumeContent)uVolContent;
			if (m_BgThread.joinable()) m_BgThread.join();
			m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_WidthInUse, m_HeightInUse, m_DepthInUse,
				m_BufferTypeInUse, m_BufferTypeInUse, m_NewVolContent );
		}
		ImGui::Separator();

		ImGui::Text( "Volume Size Settings:" );
		static int uiVolumeWide = m_WidthInUse;
		ImGui::AlignFirstTextHeightToWidgets();
		ImGui::Text( "X:" ); ImGui::SameLine();
		ImGui::RadioButton( "128##X", &uiVolumeWide, 128 ); ImGui::SameLine();
		ImGui::RadioButton( "256##X", &uiVolumeWide, 256 ); ImGui::SameLine();
		ImGui::RadioButton( "384##X", &uiVolumeWide, 384 );
		if (uiVolumeWide != m_WidthInUse && m_State.load( memory_order_acquire ) == kNormal)
		{
			m_State.store( kNewBufferCooking, memory_order_release );
			m_NewWidth = uiVolumeWide;
			if (m_BgThread.joinable()) m_BgThread.join();
			m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_NewWidth, m_HeightInUse, m_DepthInUse,
				m_BufferTypeInUse, m_BufferTypeInUse, m_VolContentInUse );
		}

		static int uiVolumeHeight = m_HeightInUse;
		ImGui::AlignFirstTextHeightToWidgets();
		ImGui::Text( "Y:" ); ImGui::SameLine();
		ImGui::RadioButton( "128##Y", &uiVolumeHeight, 128 ); ImGui::SameLine();
		ImGui::RadioButton( "256##Y", &uiVolumeHeight, 256 ); ImGui::SameLine();
		ImGui::RadioButton( "384##Y", &uiVolumeHeight, 384 );
		if (uiVolumeHeight != m_HeightInUse && m_State.load( memory_order_acquire ) == kNormal)
		{
			m_State.store( kNewBufferCooking, memory_order_release );
			m_NewHeight = uiVolumeHeight;
			if (m_BgThread.joinable()) m_BgThread.join();
			m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_WidthInUse, m_NewHeight, m_DepthInUse,
				m_BufferTypeInUse, m_BufferTypeInUse, m_VolContentInUse );
		}

		static int uiVolumeDepth = m_DepthInUse;
		ImGui::AlignFirstTextHeightToWidgets();
		ImGui::Text( "Z:" ); ImGui::SameLine();
		ImGui::RadioButton( "128##Z", &uiVolumeDepth, 128 ); ImGui::SameLine();
		ImGui::RadioButton( "256##Z", &uiVolumeDepth, 256 ); ImGui::SameLine();
		ImGui::RadioButton( "384##Z", &uiVolumeDepth, 384 );
		if (uiVolumeDepth != m_DepthInUse && m_State.load( memory_order_acquire ) == kNormal)
		{
			m_State.store( kNewBufferCooking, memory_order_release );
			m_NewDepth = uiVolumeDepth;
			if (m_BgThread.joinable()) m_BgThread.join();
			m_BgThread = std::thread( &DenseVolume::CookVolume, this, m_WidthInUse, m_HeightInUse, m_NewDepth,
				m_BufferTypeInUse, m_BufferTypeInUse, m_VolContentInUse );
		}
	}
}

void DenseVolume::CookVolume( uint32_t Width, uint32_t Height, uint32_t Depth, BufferType BufType, BufferType PreBufType, VolumeContent VolType )
{
	uint32_t BufferElmCount = Width * Height * Depth;
	uint32_t BufferSize = Width * Height * Depth * 4 * sizeof( uint8_t );
	uint8_t* pBufPtr = (uint8_t*)malloc( BufferSize );

	float a = Width / 2.f;
	float b = Height / 2.f;
	float c = Depth / 2.f;

	float radius = VolType == kSphere ? sqrt( a*a + b*b + c*c ) : (abs( a ) + abs( b ) + abs( c ));

	uint32_t bgMax = 32;

	Concurrency::parallel_for( uint32_t( 0 ), Depth, [&]( uint32_t z )
	{
		for (uint32_t y = 0; y < Height; y++)
			for (uint32_t x = 0; x < Width; x++)
			{
				float _x = x - Width / 2.f;
				float _y = y - Height / 2.f;
				float _z = z - Depth / 2.f;
				float currentRaidus = VolType == kSphere ? sqrt( _x*_x + _y*_y + _z*_z ) : (abs( _x ) + abs( _y ) + abs( _z ));
				float scale = currentRaidus / radius;
				uint32_t maxColCnt = 4;
				assert( maxColCnt < COLOR_COUNT );
				float currentScale = scale * maxColCnt + 0.1f;
				uint32_t idx = COLOR_COUNT - (uint32_t)(currentScale)-1;
				float intensity = currentScale - (uint32_t)currentScale;
				uint32_t col = (uint32_t)(intensity * (255 - bgMax)) + 1;
				pBufPtr[(x + y*Width + z*Height*Width) * 4 + 0] = 32 + col * shiftingColVals[idx].x;
				pBufPtr[(x + y*Width + z*Height*Width) * 4 + 1] = 32 + col * shiftingColVals[idx].y;
				pBufPtr[(x + y*Width + z*Height*Width) * 4 + 2] = 32 + col * shiftingColVals[idx].z;
				pBufPtr[(x + y*Width + z*Height*Width) * 4 + 3] = shiftingColVals[idx].w;
			}
	} );

	if (BufType == kTypedBuffer) m_TypedVolBuf[1 - m_OnStageIdx].Create( L"Typed Volume Buffer", BufferElmCount, 4 * sizeof( uint8_t ), pBufPtr );
	if (BufType == kStructuredBuffer) m_StructuredVolBuf[1 - m_OnStageIdx].Create( L"Struct Volume Buffer", BufferElmCount, 4 * sizeof( uint8_t ), pBufPtr );

	m_CBData[1 - m_OnStageIdx].bgCol = XMINT4( 32, 32, 32, 32 );
	m_CBData[1 - m_OnStageIdx].voxelResolution = XMINT3( Width, Height, Depth );
	m_CBData[1 - m_OnStageIdx].boxMin = XMFLOAT3( VOLUME_SIZE_SCALE*-0.5f*Width, VOLUME_SIZE_SCALE*-0.5f*Height, VOLUME_SIZE_SCALE*-0.5f*Depth );
	m_CBData[1 - m_OnStageIdx].boxMax = XMFLOAT3( VOLUME_SIZE_SCALE*0.5f*Width, VOLUME_SIZE_SCALE*0.5f*Height, VOLUME_SIZE_SCALE*0.5f*Depth );
	m_NewBufferType = BufType;
	m_NewWidth = Width;
	m_NewHeight = Height;
	m_NewDepth = Depth;

	m_State.store( kNewBufferReady, memory_order_release );

	while (m_State.load( memory_order_acquire ) != kOldBufferRetired)
	{
		this_thread::yield();
	}

	if (PreBufType == kTypedBuffer) m_TypedVolBuf[1 - m_OnStageIdx].Destroy();
	if (PreBufType == kStructuredBuffer) m_StructuredVolBuf[1 - m_OnStageIdx].Destroy();

	m_BufferTypeInUse = BufType;
	m_VolContentInUse = m_NewVolContent;
	m_WidthInUse = m_NewWidth;
	m_HeightInUse = m_NewHeight;
	m_DepthInUse = m_NewDepth;

	m_State.store( kNormal, memory_order_release );
}