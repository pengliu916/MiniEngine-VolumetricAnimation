#pragma once
#include "ManagedBuf.h"
#include "SparseVolume.inl"
class SparseVolume
{
public:
    enum VolumeStruct {
        kVoxel = 0,
        kFlagVol,
        kNumStruct
    };

    enum FilterType {
        kNoFilter = 0,
        kLinearFilter,
        kSamplerLinear,
        kSamplerAniso,
        kNumFilter
    };

    enum RaycastNormal {
        kNoNormal = 0,
        kUseNormal,
        kNumNormal
    };

    struct Ball {
        float fPower; // size of this metaball
        float fOribtRadius; // radius of orbit
        float fOribtSpeed; // speed of rotation
        float fOribtStartPhase; // initial phase
        DirectX::XMFLOAT4 f4Color; // color
    };

    SparseVolume();
    ~SparseVolume();
    void OnCreateResource();
    void OnDestory();
    void OnResize();
    void OnUpdate();
    void OnRender(CommandContext& cmdContext, const DirectX::XMMATRIX& wvp,
        const DirectX::XMMATRIX& mView, const DirectX::XMFLOAT4& eyePos);
    void RenderGui();

private:
    void _AddBall();
    void _CreateBrickVolume(const uint3& reso, const uint ratio);
    // Data update
    void _UpdatePerFrameData(const DirectX::XMMATRIX& wvp,
        const DirectX::XMMATRIX& mView,const DirectX::XMFLOAT4& eyePos);
    void _UpdateVolumeSettings(const uint3 reso);
    // Render subroutine
    void _CleanBrickVolume(ComputeContext& cptContext);
    void _UpdateVolume(CommandContext& cmdContext,
        const ManagedBuf::BufInterface& buf, bool usePS);
    void _RenderVolume(GraphicsContext& gfxContext,
        const ManagedBuf::BufInterface& buf);
    void _RenderNearFar(GraphicsContext& gfxContext);
    void _RenderBrickGrid(GraphicsContext& gfxContext);

    // Volume settings currently in use
    VolumeStruct _curVolStruct = kVoxel;
    FilterType _filterType = kNoFilter;
    uint3 _curReso;
    // new vol reso setting sent to ManagedBuf _volBuf
    uint3 _submittedReso;

    // per instance buffer resource
    ManagedBuf _volBuf;
    VolumeTexture _flagVol;
    ColorBuffer _stepInfoTex;
    PerFrameDataCB _cbPerFrame;
    PerCallDataCB _cbPerCall;
    // point to vol data section in _cbPerCall
    VolumeParam* _volParam;

    // pointers/handlers currently available
    ManagedBuf::BufInterface _curBufInterface;

    // info. to control volume update, processed by cpu
    std::vector<Ball> _ballsData;


    // available ratios for current volume resolution
    std::vector<uint16_t> _ratios;
    // current selected ratio idx
    uint _ratioIdx;

    double _animateTime = 0.0;
    bool _isAnimated = true;
    bool _needVolumeRebuild = true;
};