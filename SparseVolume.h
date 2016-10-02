#pragma once
#include "ManagedBuf.h"
#include "SparseVolume.inl"
class SparseVolume
{
protected:
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

    struct Ball {
        float fPower; // size of this metaball
        float fOribtRadius; // radius of orbit
        float fOribtSpeed; // speed of rotation
        float fOribtStartPhase; // initial phase
        DirectX::XMFLOAT4 f4Color; // color
    };

public:
    SparseVolume();
    void OnCreateResource();
    void OnResize();
    void OnRender(CommandContext& cmdContext, const DirectX::XMMATRIX& wvp,
        const DirectX::XMMATRIX& mView, const DirectX::XMFLOAT4& eyePos);
    void RenderGui();

private:
    void _AddBall();
    void _CreateBrickVolume();
    inline bool _IsResolutionChanged(const uint3& a, const uint3& b) {
        return a.x != b.x || a.y != b.y || a.z != b.z;
    }
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

    RootSignature _rootsig;
    ComputePSO _cptUpdatePSO[ManagedBuf::kNumType][kNumStruct];
    GraphicsPSO _gfxUpdatePSO[ManagedBuf::kNumType][kNumStruct];
    GraphicsPSO _gfxRenderPSO[ManagedBuf::kNumType][kNumStruct][kNumFilter];
    GraphicsPSO _gfxStepInfoPSO;
    GraphicsPSO _gfxStepInfoDebugPSO;
    ComputePSO _cptFlagVolResetPSO;

    ManagedBuf _volBuf;
    VolumeTexture _flagVol;
    ColorBuffer _stepInfoTex;
    StructuredBuffer _cubeVB;
    ByteAddressBuffer _cubeTriangleStripIB;
    ByteAddressBuffer _cubeLineStripIB;

    // info. to control volume update, processed by cpu
    std::vector<Ball> _ballsData;
};