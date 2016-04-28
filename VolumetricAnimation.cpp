
#include "stdafx.h"
#include "VolumetricAnimation.h"
#include <ppl.h>

#include "VolumetricAnimation_SharedHeader.inl"

namespace
{
	struct VolumeConfig
	{
		uint32_t width;
		uint32_t height;
		uint32_t depth;
		XMINT4 bg;
		int sphereAnim;
	};

	bool					_TypedLoadSupported = false;
	uint64_t				_fenceValue = 0;
	uint8_t					_onStageIdx = 0;
	bool					_inTransaction = false;
	bool					_needRecordFenceValue = false;
	bool					_ready2DestoryOldBuf = false;

	bool					_switchingBuffer = false;
	int						_readyBufferIdx ; // 0 for structured buffer, 1 for typed buffer

	std::atomic<bool>		_bufferReady(false);
	VolumeConfig			_volConfig;
	uint8_t*				_bufPtr;

	void PrepareBuffer( VolumeConfig& volConfig )
	{
		uint32_t width = volConfig.width;
		uint32_t height = volConfig.height;
		uint32_t depth = volConfig.depth;
		XMINT4 bg = volConfig.bg;
		int sphereAnim = volConfig.sphereAnim;

		uint32_t volumeBufferElementCount = width*height*depth;

		_bufPtr = (uint8_t*)malloc( volumeBufferElementCount * 4 * sizeof( uint8_t ) );

		float a = width / 2.f;
		float b = height / 2.f;
		float c = depth / 2.f;

		float radius = sphereAnim ? sqrt( a*a + b*b + c*c ) : (abs( a ) + abs( b ) + abs( c ));

		uint32_t bgMax = max( max( bg.x, bg.y ), bg.z );

		Concurrency::parallel_for( uint32_t( 0 ), depth, [&]( uint32_t z )
		{
			for (uint32_t y = 0; y < height; y++)
				for (uint32_t x = 0; x < width; x++)
				{
					float _x = x - width / 2.f;
					float _y = y - height / 2.f;
					float _z = z - depth / 2.f;
					float currentRaidus = sphereAnim ? sqrt( _x*_x + _y*_y + _z*_z ) : (abs( _x ) + abs( _y ) + abs( _z ));
					float scale = currentRaidus / radius;
					uint32_t maxColCnt = 4;
					assert( maxColCnt < COLOR_COUNT );
					float currentScale = scale * maxColCnt + 0.1f;
					uint32_t idx = COLOR_COUNT - (uint32_t)(currentScale)-1;
					float intensity = currentScale - (uint32_t)currentScale;
					uint32_t col = (uint32_t)(intensity * (255 - bgMax)) + 1;
					_bufPtr[(x + y*width + z*height*width) * 4 + 0] = bg.x + col * shiftingColVals[idx].x;
					_bufPtr[(x + y*width + z*height*width) * 4 + 1] = bg.y + col * shiftingColVals[idx].y;
					_bufPtr[(x + y*width + z*height*width) * 4 + 2] = bg.z + col * shiftingColVals[idx].z;
					_bufPtr[(x + y*width + z*height*width) * 4 + 3] = shiftingColVals[idx].w;
				}
		} );
	}

	void SwapVolume( VolumeConfig& volConfig )
	{
		PrepareBuffer( volConfig );
		_bufferReady.store( true );
	}
}

VolumetricAnimation::VolumetricAnimation( uint32_t width, uint32_t height, std::wstring name )
{
	_readyBufferIdx = m_UseTypedBuffer;

	m_pConstantBufferData = new ConstantBuffer();
	m_pConstantBufferData->bgCol = XMINT4( 32, 32, 32, 32 );
	m_pConstantBufferData->voxelResolution = XMINT3( m_volumeWidth, m_volumeHeight, m_volumeDepth );
	m_pConstantBufferData->boxMin = XMFLOAT3( VOLUME_SIZE_SCALE*-0.5f*m_volumeWidth, VOLUME_SIZE_SCALE*-0.5f*m_volumeHeight, VOLUME_SIZE_SCALE*-0.5f*m_volumeDepth );
	m_pConstantBufferData->boxMax = XMFLOAT3( VOLUME_SIZE_SCALE*0.5f*m_volumeWidth, VOLUME_SIZE_SCALE*0.5f*m_volumeHeight, VOLUME_SIZE_SCALE*0.5f*m_volumeDepth );
	m_pConstantBufferData->reversedWidthHeightDepth = XMFLOAT3( 1.f / m_volumeWidth, 1.f / m_volumeHeight, 1.f / m_volumeDepth );

	_volConfig.width = m_volumeWidth;
	_volConfig.height = m_volumeHeight;
	_volConfig.depth = m_volumeDepth;
	_volConfig.sphereAnim = m_SphereAnimation;
	_volConfig.bg = m_pConstantBufferData->bgCol;

	m_width = width;
	m_height = height;

#if !STATIC_ARRAY
	for (uint32_t i = 0; i < ARRAY_COUNT( shiftingColVals ); i++)
		m_pConstantBufferData->shiftingColVals[i] = shiftingColVals[i];
#endif
}

VolumetricAnimation::~VolumetricAnimation()
{
	delete m_pConstantBufferData;
}

void VolumetricAnimation::ResetCameraView()
{
	auto center = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	auto radius = m_camOrbitRadius;
	auto maxRadius = m_camMaxOribtRadius;
	auto minRadius = m_camMinOribtRadius;
	auto longAngle = 4.50f;
	auto latAngle = 1.45f;
	m_camera.View( center, radius, minRadius, maxRadius, longAngle, latAngle );
}

void VolumetricAnimation::OnConfiguration()
{
	Core::g_config.swapChainDesc.BufferCount = 5;
	Core::g_config.swapChainDesc.Width = m_width;
	Core::g_config.swapChainDesc.Height = m_height;
}

HRESULT VolumetricAnimation::OnCreateResource()
{
	ASSERT( Graphics::g_device );

	D3D12_FEATURE_DATA_D3D12_OPTIONS FeatureData;
	ZeroMemory( &FeatureData, sizeof( FeatureData ) );
	HRESULT hr = Graphics::g_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &FeatureData, sizeof( FeatureData ) );
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
				_TypedLoadSupported = true;
			}
			else
				PRINTWARN( "DXGI_FORMAT_R8G8B8A8_UINT typed load is not supported" );
		}
		else
			PRINTWARN( "TypedUAVLoadAdditionalFormats load is not supported" );
	}

	VRET( LoadSizeDependentResource() );
	VRET( LoadAssets() );

	return S_OK;
}


// Load the assets.
HRESULT VolumetricAnimation::LoadAssets()
{
	HRESULT	hr;

	D3D12_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	m_RootSignature.Reset( 3, 1 );
	m_RootSignature.InitStaticSampler( 0, sampler );
	m_RootSignature[0].InitAsConstantBuffer( 0 );

	m_RootSignature[1].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1 );
	m_RootSignature[2].InitAsDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1 );

	m_RootSignature.Finalize( D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS );

	m_GraphicsPSO.SetRootSignature( m_RootSignature );
	m_ComputePSO.SetRootSignature( m_RootSignature );
	m_ComputePSOTyped.SetRootSignature( m_RootSignature );

	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	m_GraphicsPSO.SetInputLayout( _countof( inputElementDescs ), inputElementDescs );
	m_GraphicsPSO.SetRasterizerState( Graphics::g_RasterizerDefault );
	m_GraphicsPSO.SetBlendState( Graphics::g_BlendDisable );
	m_GraphicsPSO.SetDepthStencilState( Graphics::g_DepthStateReadWrite );
	m_GraphicsPSO.SetSampleMask( UINT_MAX );
	m_GraphicsPSO.SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE );
	DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
	m_GraphicsPSO.SetRenderTargetFormats( 1, &ColorFormat, DepthFormat );

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;
	ComPtr<ID3DBlob> pixelShaderTyped;
	ComPtr<ID3DBlob> computeShader;
	ComPtr<ID3DBlob> computeShaderTyped;

	uint32_t compileFlags = 0;
	D3D_SHADER_MACRO macro[] =
	{
		{ "__hlsl",			"1"},	// 0 
		{ "VERTEX_SHADER",	"1"},	// 1 
		{ "PIXEL_SHADER",	"0" },	// 2 
		{ "COMPUTE_SHADER",	"0"},	// 3
		{ "TYPED_UAV",		"0" },	// 4
		{ "TYPED_LOAD_NOT_SUPPORTED", _TypedLoadSupported ? "0" : "1" },	// 5
		{ nullptr,		nullptr }
	};
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vsmain", "vs_5_0", compileFlags, 0, &vertexShader ) );
	macro[1].Definition = "0";
	macro[2].Definition = "1";
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShader ) );
	macro[4].Definition = "1";
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "psmain", "ps_5_0", compileFlags, 0, &pixelShaderTyped ) );
	macro[2].Definition = "0";
	macro[3].Definition = "1";
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShaderTyped ) );
	macro[4].Definition = "0";
	VRET( Graphics::CompileShaderFromFile( Core::GetAssetFullPath( _T( "VolumetricAnimation_shader.hlsl" ) ).c_str(), macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "csmain", "cs_5_0", compileFlags, 0, &computeShader ) );

	m_GraphicsPSO.SetVertexShader( vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() );
	m_GraphicsPSOTyped = m_GraphicsPSO;
	m_GraphicsPSOTyped.SetPixelShader( pixelShaderTyped->GetBufferPointer(), pixelShaderTyped->GetBufferSize() );
	m_GraphicsPSO.SetPixelShader( pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() );
	m_ComputePSO.SetComputeShader( computeShader->GetBufferPointer(), computeShader->GetBufferSize() );
	m_ComputePSOTyped.SetComputeShader( computeShaderTyped->GetBufferPointer(), computeShaderTyped->GetBufferSize() );

	m_ComputePSO.Finalize();
	m_ComputePSOTyped.Finalize();
	m_GraphicsPSO.Finalize();
	m_GraphicsPSOTyped.Finalize();

	uint32_t volumeBufferElementCount = m_volumeDepth*m_volumeHeight*m_volumeWidth;
	if (m_UseTypedBuffer)
		m_TypedVolumeBuffer[_onStageIdx].Create( L"Typed Volume Buf", volumeBufferElementCount, 4 * sizeof( uint8_t ) );
	else
		m_VolumeBuffer[_onStageIdx].Create( L"Volume Buffer", volumeBufferElementCount, 4 * sizeof( uint8_t ) );

	// Define the geometry for a triangle.
	Vertex cubeVertices[] =
	{
		{ XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE ) },
		{ XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE ) },
		{ XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE ) },
		{ XMFLOAT3( -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE ) },
		 { XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE ) },
		 { XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE ) },
		 { XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE, -1 * 0.5f * VOLUME_SIZE_SCALE ) },
		 { XMFLOAT3( 1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE,  1 * 0.5f * VOLUME_SIZE_SCALE ) },
	};

	const uint32_t vertexBufferSize = sizeof( cubeVertices );
	m_VertexBuffer.Create( L"Vertex Buffer", ARRAYSIZE( cubeVertices ), sizeof( XMFLOAT3 ), (void*)cubeVertices );

	uint16_t cubeIndices[] =
	{
		0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,  2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
	};

	m_IndexBuffer.Create( L"Index Buffer", ARRAYSIZE( cubeIndices ), sizeof( uint16_t ), (void*)cubeIndices );

	ResetCameraView();

	return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::LoadSizeDependentResource()
{
	uint32_t width = Core::g_config.swapChainDesc.Width;
	uint32_t height = Core::g_config.swapChainDesc.Height;

	float fAspectRatio = width / (FLOAT)height;
	m_camera.Projection( XM_PIDIV2 / 2, fAspectRatio );
	return S_OK;
}

// Update frame-based values.
void VolumetricAnimation::OnUpdate()
{
	m_camera.ProcessInertia();
	static bool showPenal = true;
	if (ImGui::Begin( "VolumetricAnimation", &showPenal ))
	{
		ImGui::Text( "Buffer Settings:" );
		static int uBufferChoice = m_UseTypedBuffer;
		ImGui::RadioButton( "Use Typed Buffer", &uBufferChoice, 1 );
		ImGui::RadioButton( "Use Structured Buffer", &uBufferChoice, 0 );
		if (!_inTransaction && uBufferChoice != m_UseTypedBuffer)
		{
			_inTransaction = true;
			m_UseTypedBuffer = uBufferChoice;
			_switchingBuffer = true;
			std::thread threadCreateVolume( &SwapVolume, _volConfig );
			threadCreateVolume.detach();
		}
		ImGui::Separator();

		ImGui::Text( "Command Queue Settings:" );
		ImGui::RadioButton( "Use single context", &m_OneContext, 1 );
		ImGui::RadioButton( "Use mult-context & sync", &m_OneContext, 0 );
		ImGui::Separator();

		ImGui::Text( "Volume Animation Settings:" );
		static int uiAnimation = 1 - m_SphereAnimation;
		ImGui::RadioButton( "Sphere Animation", &uiAnimation, 1 );
		ImGui::RadioButton( "Cube Animation", &uiAnimation, 0 );
		if (!_inTransaction && uiAnimation != m_SphereAnimation)
		{
			_inTransaction = true;
			m_SphereAnimation = uiAnimation;
			_volConfig.sphereAnim = uiAnimation;
			std::thread threadCreateVolume( &SwapVolume, _volConfig );
			threadCreateVolume.detach();
		}
		ImGui::Separator();

		ImGui::Text( "Volume Size Settings:" );
		static int uiVolumeSize = m_selectedVolumeSize;
		ImGui::RadioButton( "128^3", &uiVolumeSize, 128 );
		ImGui::RadioButton( "256^3", &uiVolumeSize, 256 );
		ImGui::RadioButton( "384^3", &uiVolumeSize, 384 );
		if (!_inTransaction && uiVolumeSize != m_selectedVolumeSize)
		{
			_inTransaction = true;
			m_selectedVolumeSize = uiVolumeSize;
			_volConfig.width = uiVolumeSize;
			_volConfig.height = uiVolumeSize;
			_volConfig.depth = uiVolumeSize;
			std::thread threadCreateVolume( &SwapVolume, _volConfig );
			threadCreateVolume.detach();
		}
	}
	ImGui::End();

	if (_inTransaction)
	{
		if (_bufferReady.load())
		{
			_bufferReady.store( false );

			uint32_t bufferElementCount = _volConfig.width * _volConfig.height * _volConfig.depth;
			if (m_UseTypedBuffer == 0)
				m_VolumeBuffer[1 - _onStageIdx].Create( L"Volume Buffer", bufferElementCount, 4 * sizeof( uint8_t ), _bufPtr );
			else
				m_TypedVolumeBuffer[1 - _onStageIdx].Create( L"Typed Volume Buf", bufferElementCount, 4 * sizeof( uint8_t ), _bufPtr );
			_onStageIdx = 1 - _onStageIdx;

			if (_switchingBuffer)
				_readyBufferIdx = 1 - _readyBufferIdx;
			delete _bufPtr;

			_needRecordFenceValue = true;
			_ready2DestoryOldBuf = true;

			m_volumeWidth = m_selectedVolumeSize;
			m_volumeHeight = m_selectedVolumeSize;
			m_volumeDepth = m_selectedVolumeSize;
			m_pConstantBufferData->voxelResolution = XMINT3( m_volumeWidth, m_volumeHeight, m_volumeDepth );
			m_pConstantBufferData->boxMin = XMFLOAT3( VOLUME_SIZE_SCALE*-0.5f*m_volumeWidth, VOLUME_SIZE_SCALE*-0.5f*m_volumeHeight, VOLUME_SIZE_SCALE*-0.5f*m_volumeDepth );
			m_pConstantBufferData->boxMax = XMFLOAT3( VOLUME_SIZE_SCALE*0.5f*m_volumeWidth, VOLUME_SIZE_SCALE*0.5f*m_volumeHeight, VOLUME_SIZE_SCALE*0.5f*m_volumeDepth );
			m_pConstantBufferData->reversedWidthHeightDepth = XMFLOAT3( 1.f / m_volumeWidth, 1.f / m_volumeHeight, 1.f / m_volumeDepth );
		}

		if (_ready2DestoryOldBuf && !_needRecordFenceValue && Graphics::g_cmdListMngr.IsFenceComplete( _fenceValue ))
		{
			_ready2DestoryOldBuf = false;
			if (_switchingBuffer)
			{
				_switchingBuffer = false;
				if (m_UseTypedBuffer == 1)
					m_VolumeBuffer[1 - _onStageIdx].Destroy();
				else
					m_TypedVolumeBuffer[1 - _onStageIdx].Destroy();
			}
			else
			{
				if (m_UseTypedBuffer == 0)
					m_VolumeBuffer[1 - _onStageIdx].Destroy();
				else
					m_TypedVolumeBuffer[1 - _onStageIdx].Destroy();
			}
			_inTransaction = false;
		}
	}
}

// Render the scene.
void VolumetricAnimation::OnRender( CommandContext& EngineContext )
{
	XMMATRIX view = m_camera.View();
	XMMATRIX proj = m_camera.Projection();

	XMMATRIX world = XMMatrixIdentity();
	m_pConstantBufferData->invWorld = XMMatrixInverse( nullptr, world );
	m_pConstantBufferData->wvp = XMMatrixMultiply( XMMatrixMultiply( world, view ), proj );
	XMStoreFloat4( &m_pConstantBufferData->viewPos, m_camera.Eye() );

	GpuBuffer* VolumeBuffer = (_readyBufferIdx == 0 ? (GpuBuffer*)&m_VolumeBuffer[_onStageIdx] : (GpuBuffer*)&m_TypedVolumeBuffer[_onStageIdx]);
	ComputeContext& cptContext = m_OneContext ? EngineContext.GetComputeContext() : ComputeContext::Begin( L"Update Volume" );
	{
		GPU_PROFILE( cptContext, L"Volume Updating" );
		cptContext.SetRootSignature( m_RootSignature );
		cptContext.SetPipelineState( _readyBufferIdx == 0 ? m_ComputePSO : m_ComputePSOTyped );
		cptContext.TransitionResource( *VolumeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
		cptContext.SetDynamicConstantBufferView( 0, sizeof( ConstantBuffer ), m_pConstantBufferData );
		cptContext.SetDynamicDescriptors( 1, 0, 1, &VolumeBuffer->GetSRV() );
		cptContext.SetDynamicDescriptors( 2, 0, 1, &VolumeBuffer->GetUAV() );
		cptContext.Dispatch( m_volumeWidth / THREAD_X, m_volumeHeight / THREAD_Y, m_volumeDepth / THREAD_Z );
	}
	if (!m_OneContext)
	{
		Graphics::g_cmdListMngr.GetQueue( D3D12_COMMAND_LIST_TYPE_DIRECT ).WaitForFence( _fenceValue );
		_fenceValue = cptContext.Finish();
	}

	GraphicsContext& gfxContext = m_OneContext ? EngineContext.GetGraphicsContext() : GraphicsContext::Begin( L"Render Volume" );
	{
		GPU_PROFILE( gfxContext, L"Rendering" );

		gfxContext.ClearColor( Graphics::g_SceneColorBuffer );
		gfxContext.ClearDepth( Graphics::g_SceneDepthBuffer );
		gfxContext.SetRootSignature( m_RootSignature );
		gfxContext.SetPipelineState( _readyBufferIdx == 0 ? m_GraphicsPSO : m_GraphicsPSOTyped );
		gfxContext.SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		gfxContext.TransitionResource( *VolumeBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		gfxContext.SetDynamicConstantBufferView( 0, sizeof( ConstantBuffer ), m_pConstantBufferData );
		gfxContext.SetDynamicDescriptors( 1, 0, 1, &VolumeBuffer->GetSRV() );
		gfxContext.SetDynamicDescriptors( 2, 0, 1, &VolumeBuffer->GetUAV() );
		gfxContext.SetRenderTargets( 1, &Graphics::g_SceneColorBuffer, &Graphics::g_SceneDepthBuffer );
		gfxContext.SetViewport( Graphics::g_DisplayPlaneViewPort );
		gfxContext.SetScisor( Graphics::g_DisplayPlaneScissorRect );
		gfxContext.SetVertexBuffer( 0, m_VertexBuffer.VertexBufferView() );
		gfxContext.SetIndexBuffer( m_IndexBuffer.IndexBufferView() );
		gfxContext.DrawIndexed( 36 );

		TextContext Text( gfxContext );
		Text.Begin();
		Text.SetViewSize( (float)Core::g_config.swapChainDesc.Width, (float)Core::g_config.swapChainDesc.Height );
		Text.SetFont( L"xerox.fnt" );
		Text.ResetCursor( 10, 90 );
		Text.SetTextSize( 20.f );
		Text.DrawString( m_OneContext ? "Current State: Using one cmdqueue" : "Current State: Using two cmdqueue and sync" );
		Text.End();
	}

	if (_needRecordFenceValue)
	{
		_needRecordFenceValue = false;
		_fenceValue = Graphics::g_cmdListMngr.GetQueue( D3D12_COMMAND_LIST_TYPE_DIRECT ).IncrementFence();
	}

	if (!m_OneContext)
	{
		//Graphics::g_cmdListMngr.GetQueue( D3D12_COMMAND_LIST_TYPE_DIRECT ).WaitForFence( _fenceValue );
		_fenceValue = gfxContext.Finish();
	}
}

HRESULT VolumetricAnimation::OnSizeChanged()
{
	HRESULT hr;
	VRET( LoadSizeDependentResource() );
	return S_OK;
}

void VolumetricAnimation::OnDestroy()
{
}

bool VolumetricAnimation::OnEvent( MSG* msg )
{
	switch (msg->message)
	{
	case WM_MOUSEWHEEL:
	{
		auto delta = GET_WHEEL_DELTA_WPARAM( msg->wParam );
		m_camera.ZoomRadius( -0.007f*delta );
		return true;
	}
	case WM_POINTERDOWN:
	case WM_POINTERUPDATE:
	case WM_POINTERUP:
	{
		auto pointerId = GET_POINTERID_WPARAM( msg->wParam );
		POINTER_INFO pointerInfo;
		if (GetPointerInfo( pointerId, &pointerInfo )) {
			if (msg->message == WM_POINTERDOWN) {
				// Compute pointer position in render units
				POINT p = pointerInfo.ptPixelLocation;
				ScreenToClient( Core::g_hwnd, &p );
				RECT clientRect;
				GetClientRect( Core::g_hwnd, &clientRect );
				p.x = p.x * Core::g_config.swapChainDesc.Width / (clientRect.right - clientRect.left);
				p.y = p.y * Core::g_config.swapChainDesc.Height / (clientRect.bottom - clientRect.top);
				// Camera manipulation
				m_camera.AddPointer( pointerId );
			}
		}

		// Otherwise send it to the camera controls
		m_camera.ProcessPointerFrames( pointerId, &pointerInfo );
		if (msg->message == WM_POINTERUP) m_camera.RemovePointer( pointerId );
		return true;
	}
	case WM_KEYDOWN:
		switch (msg->wParam) {
		case 'S':
			m_OneContext = !m_OneContext;
			PRINTINFO( "OneContext is %s", m_OneContext ? "on" : "off" );
			return 0;
		}
		return 0;
	}
	return false;
}