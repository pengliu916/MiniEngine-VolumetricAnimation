#pragma once
#include "DenseVolume_SharedHeader.inl"
class DenseVolume
{
public:
	enum BufferType
	{
		kStructuredBuffer = 0,
		kTypedBuffer = 1,
		kNumBufferType
	};

	enum VolumeContent
	{
		kDimond = 0,
		kSphere = 1,
		kNumContentTye
	};

	enum ResourceState
	{
		kNormal = 0,
		kNewBufferCooking = 1,
		kNewBufferReady = 2,
		kRetiringOldBuffer = 3,
		kOldBufferRetired = 4,
		kNumStates
	};

	DenseVolume();
	~DenseVolume();

	void OnCreateResource();
	void OnRender( CommandContext& cmdContext, DirectX::XMMATRIX wvp, DirectX::XMFLOAT4 eyePos );
	void RenderGui();

	void CookVolume( uint32_t Width, uint32_t Height, uint32_t Depth, BufferType BufType, BufferType PreBufType, VolumeContent VolType );

	// Volume settings current in use
	BufferType					m_BufferTypeInUse = kStructuredBuffer;
	VolumeContent				m_VolContentInUse = kDimond;
	uint32_t					m_WidthInUse = 256;
	uint32_t					m_HeightInUse = 256;
	uint32_t					m_DepthInUse = 256;

protected:
	ComputePSO					m_CptUpdatePSO[kNumBufferType];
	GraphicsPSO					m_GfxRenderPSO[kNumBufferType];

	RootSignature				m_RootSignature;

	StructuredBuffer			m_StructuredVolBuf[2];
	TypedBuffer					m_TypedVolBuf[2] = {DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R8G8B8A8_UINT};

	StructuredBuffer			m_VertexBuffer;
	ByteAddressBuffer			m_IndexBuffer;

	DataCB						m_CBData[2];

	// Since we use other thread updating modified volume, we need 2 copy, and m_OnStageIdx indicate the one in use
	uint8_t						m_OnStageIdx = 0;

	std::atomic<ResourceState>	m_State = kNormal;

	// Thread variable for handling thread
	std::thread					m_BgThread;

	// FenceValue for destroy old buffer
	uint64_t					m_FenceValue;

	// Volume settings will be used for new volume
	BufferType					m_NewBufferType = m_BufferTypeInUse;
	VolumeContent				m_NewVolContent = m_VolContentInUse;

	uint32_t					m_NewWidth = m_WidthInUse;
	uint32_t					m_NewHeight = m_HeightInUse;
	uint32_t					m_NewDepth = m_DepthInUse;

	bool						m_TypeLoadSupported = false;
};