#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
#include "TextRenderer.h"
#include "GpuResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "DenseVolume.h"
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

	uint32_t				m_width;
	uint32_t				m_height;

	float					m_camOrbitRadius = 10.f;
	float					m_camMaxOribtRadius = 100.f;
	float					m_camMinOribtRadius = 2.f;

	OrbitCamera				m_camera;

	DenseVolume				m_DenseVolume;

	void ResetCameraView();
};
