#pragma once
#include "SparseVolume.inl"
class SparseVolume
{
protected:
    enum BufferType {
        kStructuredBuffer = 0,
        kTypedBuffer,
        k3DTexBuffer,
        kNumBufferType
    };

    enum VolumeStructType {
        kVoxelsOnly = 0,
        kVoxelsAndBricks,
        kNumVolStructType
    };

    enum FilterType {
        kNoFilter = 0,
        kLinearFilter,
        kSamplerLinear,
        kSamplerAniso,
        kNumFilterType
    };
    enum ResourceState {
        kNormal = 0,
        kNewBufferCooking,
        kNewBufferReady,
        kRetiringOldBuffer,
        kOldBufferRetired,
        kNumStates
    };

    struct Ball {
        float fPower; // size of this metaball
        float fOribtRadius; // radius of orbit
        float fOribtSpeed; // speed of rotation
        float fOribtStartPhase; // initial phase
        DirectX::XMFLOAT4 f4Color; // color
    };

public:
    SparseVolume();
    ~SparseVolume();

    void OnCreateResource();
    void OnRender( CommandContext& cmdContext, DirectX::XMMATRIX wvp,
        DirectX::XMMATRIX mView, DirectX::XMFLOAT4 eyePos );
    void RenderGui();

protected:
    void CookVolume( uint32_t Width, uint32_t Height, uint32_t Depth,
        BufferType BufType, BufferType PreBufType);
    void UpdatePerCallData( PerCallDataCB& DataCB, DirectX::XMUINT3 VoxRes,
        float VoxSize, DirectX::XMFLOAT2 MinMaxDensity, uint VoxBrickRatio,
        uint NumOfBalls );
    void UpdatePerFrameData(DirectX::XMMATRIX wvp, DirectX::XMMATRIX mView,
        DirectX::XMFLOAT4 eyePos);

    void AddBall();

private:
    // Volume settings currently in use
    BufferType _currentBufferType = kStructuredBuffer;
    VolumeStructType _currentVolumeStructType = kVoxelsOnly;
    FilterType _currentFilterType = kNoFilter;
    uint32_t _currentWidth = 256;
    uint32_t _currentHeight = 256;
    uint32_t _currentDepth = 256;
    float _currentVoxelSize = 1.f / 256.f;
    float _minDensity = 0.8f;
    float _maxDensity = 1.2f;
    uint _numMetaBalls = 20;
    uint _voxelBrickRatio = 8;

    ComputePSO _computUpdatePSO[kNumBufferType][kNumVolStructType];
    GraphicsPSO 
        _graphicRenderPSO[kNumBufferType][kNumVolStructType][kNumFilterType];
    GraphicsPSO _graphicBrickMinMaxPSO;
    RootSignature _rootsignature;

    VolumeTexture _volumeTextureBuffer[2];
    StructuredBuffer _structVolumeBuffer[2];
    TypedBuffer _typedVolumeBuffer[2] =
        {DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};

    StructuredBuffer _vertexBuffer;
    ByteAddressBuffer _indexBuffer;

    PerFrameDataCB _perFrameConstantBufferData;
    PerCallDataCB _perCallConstantBufferData;

    // System will prepare new volume in bg so we need sync control
    uint8_t _onStageIndex = 0;
    // Foreground and background sync signal
    std::atomic<ResourceState> _resourceState = kNormal;
    // Thread variable for background volume preparation thread
    std::unique_ptr<thread_guard> _backgroundThread;
    // FenceValue for signal safe old buffer destroy
    uint64_t _fenceValue;
    // Default volume settings for preparing new volume
    BufferType _newBufferType = _currentBufferType;
    VolumeStructType _newVolumeStructType = _currentVolumeStructType;
    uint32_t _newWidth = _currentWidth;
    uint32_t _newHeight = _currentHeight;
    uint32_t _newDepth = _currentDepth;
    // info. to control volume update, processed by cpu
    std::vector<Ball> _ballsData;
    // Detect typeuav load support
    bool _typedLoadSupported = false;

    double _animateTime = 0.0;
    bool _isAnimated = true;
};