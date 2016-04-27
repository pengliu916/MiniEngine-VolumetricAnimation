#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
#include "TextRenderer.h"
#include "GpuResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "Camera.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class VolumetricAnimation : public Core::IDX12Framework
{
public:
	VolumetricAnimation( uint32_t width, uint32_t height, std::wstring name );
	~VolumetricAnimation();

	virtual void OnConfiguration();
	virtual HRESULT OnCreateResource();
	virtual HRESULT OnSizeChanged();
	virtual void OnUpdate();
	virtual void OnRender( CommandContext& EngineContext );
	virtual void OnDestroy();
	virtual bool OnEvent( MSG* msg );

private:
	struct Vertex
	{
		XMFLOAT3 position;
	};

	uint8_t					m_onStageIdx;
	int						m_selectedVolumeSize;
	int						m_OneContext;
	int						m_SphereAnimation;
	uint64_t				m_fenceValue;
	uint32_t				m_width;
	uint32_t				m_height;

	float					m_camOrbitRadius;
	float					m_camMaxOribtRadius;
	float					m_camMinOribtRadius;

	StructuredBuffer		m_VertexBuffer;
	ByteAddressBuffer		m_IndexBuffer;
	GraphicsPSO				m_GraphicsPSO;
	GraphicsPSO				m_GraphicsPSOTyped;
	ComputePSO				m_ComputePSO;
	ComputePSO				m_ComputePSOTyped;
	RootSignature			m_RootSignature;
	StructuredBuffer		m_VolumeBuffer[2];
	TypedBuffer				m_TypedVolumeBuffer[2] = {DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R8G8B8A8_UINT};

	OrbitCamera				m_camera;
	struct ConstantBuffer*	m_pConstantBufferData;

	uint32_t				m_volumeWidth;
	uint32_t				m_volumeHeight;
	uint32_t				m_volumeDepth;

	HRESULT LoadAssets();
	HRESULT LoadSizeDependentResource();

	void ResetCameraView();
};
