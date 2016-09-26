#pragma once

#include "DX12Framework.h"
#include "DescriptorHeap.h"
#include "TextRenderer.h"
#include "GpuResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "CommandContext.h"
#include "DenseVolume.h"
#include "SparseVolume.h"
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
    enum VolumeType {
        kSparseVolume = 0,
        kDenseVolume,
        kNumVolumeType,
    };

    uint32_t m_width;
    uint32_t m_height;
    float m_camOrbitRadius = 2.f;
    float m_camMaxOribtRadius = 5.f;
    float m_camMinOribtRadius = 1.f;

    OrbitCamera m_camera;

    DenseVolume m_DenseVolume;
    SparseVolume m_SparseVolume;

    int m_CurVolType = kSparseVolume;

    void ResetCameraView();
};
