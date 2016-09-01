#pragma once
#include "DenseVolume_SharedHeader.inl"
class DenseVolume
{
protected:
    enum BufferType {
        kStructuredBuffer = 0,
        kTypedBuffer = 1,
        kNumBufferType
    };

    enum VolumeContent {
        kDimond = 0,
        kSphere = 1,
        kNumContentTye
    };

    enum ResourceState {
        kNormal = 0,
        kNewBufferCooking = 1,
        kNewBufferReady = 2,
        kRetiringOldBuffer = 3,
        kOldBufferRetired = 4,
        kNumStates
    };

public:
    DenseVolume();
    ~DenseVolume();

    void OnCreateResource();
    void OnRender( CommandContext& cmdContext, DirectX::XMMATRIX wvp, 
        DirectX::XMFLOAT4 eyePos );
    void RenderGui();

protected:
    void CookVolume( uint32_t Width, uint32_t Height, uint32_t Depth, 
        BufferType BufType, BufferType PreBufType, VolumeContent VolType );

private:
    // Volume settings current in use
    BufferType _currentBufferType = kStructuredBuffer;
    VolumeContent _currentVolumeContent = kDimond;
    uint32_t _currentWidth = 256;
    uint32_t _currentHeight = 256;
    uint32_t _currentDepth = 256;
    float _voxelSize = 1.f / 256.f;

    ComputePSO _computeUpdatePSO[kNumBufferType];
    GraphicsPSO _graphicRenderPSO[kNumBufferType];

    RootSignature _rootsignature;

    StructuredBuffer _structuredVolumeBuffer[2];
    TypedBuffer _typedVolumeBuffer[2] = 
        {DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R8G8B8A8_UINT};

    StructuredBuffer _vertexBuffer;
    ByteAddressBuffer _indexBuffer;

    DataCB _constantBufferData[2];

    // Since we use other thread updating modified volume, we need 2 copy, 
    // and m_OnStageIdx indicate the one in use
    uint8_t _onStageIndex = 0;

    std::atomic<ResourceState> _resourceState = kNormal;

    // Thread variable for handling thread
    std::unique_ptr<thread_guard> _backgroundThread;

    // FenceValue for destroy old buffer
    uint64_t _fenceValue;

    // Volume settings will be used for new volume
    BufferType _newBufferType = _currentBufferType;
    VolumeContent _newVolumeContent = _currentVolumeContent;

    uint32_t _newWidth = _currentWidth;
    uint32_t _newHeight = _currentHeight;
    uint32_t _newDepth = _currentDepth;

    bool _typedLoadSupported = false;
};