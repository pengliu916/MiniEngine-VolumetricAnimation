#include "stdafx.h"
#include "SparseVolume.h"

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace std;

#define frand() ((float)rand()/RAND_MAX)
namespace {
    const DXGI_FORMAT _stepInfoTexFormat = DXGI_FORMAT_R16G16_FLOAT;
    bool _typedLoadSupported = false;

    bool _useStepInfoTex = false;
    bool _stepInfoDebug = false;
    bool _usePSUpdate = false;
    bool _isoRender = false;
    bool _useNormal = false;
    bool _writeDepth = false;

    // define the geometry for a triangle.
    const XMFLOAT3 cubeVertices[] = {
        {XMFLOAT3(-0.5f, -0.5f, -0.5f)},{XMFLOAT3(-0.5f, -0.5f,  0.5f)},
        {XMFLOAT3(-0.5f,  0.5f, -0.5f)},{XMFLOAT3(-0.5f,  0.5f,  0.5f)},
        {XMFLOAT3(0.5f, -0.5f, -0.5f)},{XMFLOAT3(0.5f, -0.5f,  0.5f)},
        {XMFLOAT3(0.5f,  0.5f, -0.5f)},{XMFLOAT3(0.5f,  0.5f,  0.5f)},
    };
    // the index buffer for triangle strip
    const uint16_t cubeTrianglesStripIndices[CUBE_TRIANGLESTRIP_LENGTH] = {
        6, 4, 2, 0, 1, 4, 5, 7, 1, 3, 2, 7, 6, 4
    };
    // the index buffer for cube wire frame (0xffff is the cut value set later)
    const uint16_t cubeLineStripIndices[CUBE_LINESTRIP_LENGTH] = {
        0, 1, 5, 4, 0, 2, 3, 7, 6, 2, 0xffff, 6, 4, 0xffff, 7, 5, 0xffff, 3, 1
    };

    std::once_flag _psoCompiled_flag;
    RootSignature _rootsig;
    ComputePSO _cptUpdatePSO[ManagedBuf::kNumType][SparseVolume::kNumStruct];
    GraphicsPSO _gfxUpdatePSO[ManagedBuf::kNumType][SparseVolume::kNumStruct];
    GraphicsPSO _gfxVolumeRenderPSO[ManagedBuf::kNumType]
        [SparseVolume::kNumStruct][SparseVolume::kNumFilter];
    GraphicsPSO _gfxISOSurfRenderPSO
        [ManagedBuf::kNumType][SparseVolume::kNumStruct]
        [SparseVolume::kNumFilter][SparseVolume::kNumNormal][2];
    GraphicsPSO _gfxStepInfoPSO;
    GraphicsPSO _gfxStepInfoDebugPSO[2];
    ComputePSO _cptFlagVolResetPSO;
    StructuredBuffer _cubeVB;
    ByteAddressBuffer _cubeTriangleStripIB;
    ByteAddressBuffer _cubeLineStripIB;

    BOOLEAN GetBaseAndPower2(uint16_t input, uint16_t& base, uint16_t& power2)
    {
        uint32_t index;
        BOOLEAN isZero = _BitScanForward((unsigned long*)&index, input);
        power2 = (uint16_t)index;
        base = input >> power2;
        return isZero;
    }

    void _BuildBrickRatioVector(uint16_t minDimension,
        std::vector<uint16_t>& ratios)
    {
        uint16_t base, power2;
        GetBaseAndPower2(minDimension, base, power2);
        ASSERT(power2 > 3);
        ratios.clear();
        for (uint16_t i = base == 1 ? 1 : 0;
            i < (base == 1 ? power2 - 1 : power2); ++i) {
            ratios.push_back(1 << i);
        }
    }

    inline bool _IsResolutionChanged(const uint3& a, const uint3& b)
    {
        return a.x != b.x || a.y != b.y || a.z != b.z;
    }

    inline HRESULT _Compile(LPCWSTR fileName, LPCSTR target,
        const D3D_SHADER_MACRO* macro, ID3DBlob** bolb)
    {
        return Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            fileName).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
            target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, bolb);
    }

    void _CreatePSOs()
    {
        HRESULT hr;
        // Feature support checking
        D3D12_FEATURE_DATA_D3D12_OPTIONS FeatureData = {};
        V(Graphics::g_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
            &FeatureData, sizeof(FeatureData)));
        if (SUCCEEDED(hr)) {
            if (FeatureData.TypedUAVLoadAdditionalFormats) {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport =
                {DXGI_FORMAT_R8G8B8A8_UINT,D3D12_FORMAT_SUPPORT1_NONE,
                    D3D12_FORMAT_SUPPORT2_NONE};
                V(Graphics::g_device->CheckFeatureSupport(
                    D3D12_FEATURE_FORMAT_SUPPORT, &FormatSupport,
                    sizeof(FormatSupport)));
                if (FAILED(hr)) {
                    PRINTERROR("Checking Feature Support Failed");
                }
                if ((FormatSupport.Support2 &
                    D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0) {
                    _typedLoadSupported = true;
                    PRINTINFO("Typed load is supported");
                } else {
                    PRINTWARN("Typed load is not supported");
                }
            } else {
                PRINTWARN("TypedLoad AdditionalFormats load is not supported");
            }
        }

        // Compile Shaders
        ComPtr<ID3DBlob>
            volUpdateCS[ManagedBuf::kNumType][SparseVolume::kNumStruct];
        ComPtr<ID3DBlob> cubeVS, stepInfoVS, volUpdateVS;
        ComPtr<ID3DBlob> volUpdateGS;
        ComPtr<ID3DBlob> raycastPS[ManagedBuf::kNumType]
            [SparseVolume::kNumStruct][SparseVolume::kNumFilter];
        ComPtr<ID3DBlob>
            volUpdatePS[ManagedBuf::kNumType][SparseVolume::kNumStruct];
        ComPtr<ID3DBlob> isoRenderPS[ManagedBuf::kNumType]
            [SparseVolume::kNumStruct][SparseVolume::kNumFilter]
            [SparseVolume::kNumNormal][2];

        D3D_SHADER_MACRO macro[] = {
            {"__hlsl", "1"},//0
            {"TYPED_UAV", "0"},//1
            {"STRUCT_UAV", "0"},//2
            {"TEX3D_UAV", "0"},//3
            {"FILTER_READ", "0"},//4
            {"ENABLE_BRICKS", "0"},//5
            {"ISO_SURFACE", "0"},//6
            {"USE_NORMAL", "0"},//7
            {"DEPTH_OUT", "0"},//8
            {nullptr, nullptr}
        };

        V(_Compile(L"SparseVolume_RayCast_vs.hlsl", "vs_5_1", macro, &cubeVS));
        V(_Compile(L"SparseVolume_VolumeUpdate_vs.hlsl", "vs_5_1",
            macro,&volUpdateVS));
        V(_Compile(L"SparseVolume_VolumeUpdate_gs.hlsl", "gs_5_1",
            macro,&volUpdateGS));

        uint DefIdx;
        for (int j = 0; j < SparseVolume::kNumStruct; ++j) {
            macro[5].Definition = j == SparseVolume::kVoxel ? "0" : "1";
            for (int i = 0; i < ManagedBuf::kNumType; ++i) {
                switch ((ManagedBuf::Type)i) {
                case ManagedBuf::kStructuredBuffer: DefIdx = 2; break;
                case ManagedBuf::kTypedBuffer: DefIdx = 1; break;
                case ManagedBuf::k3DTexBuffer: DefIdx = 3; break;
                }
                macro[DefIdx].Definition = "1";
                V(_Compile(L"SparseVolume_VolumeUpdate_cs.hlsl", "cs_5_1",
                    macro, &volUpdateCS[i][j]));
                V(_Compile(L"SparseVolume_VolumeUpdate_ps.hlsl", "ps_5_1",
                    macro, &volUpdatePS[i][j]));
                for (int k = 0; k < SparseVolume::kNumFilter; ++k) {
                    char tmp[8];
                    sprintf_s(tmp, 8, "%d", k);
                    macro[4].Definition = tmp;
                    V(_Compile(L"SparseVolume_RayCast_ps.hlsl", "ps_5_1",
                        macro, &raycastPS[i][j][(SparseVolume::FilterType)k]));
                    macro[6].Definition = "1"; // ISO_SURFACE
                    V(_Compile(L"SparseVolume_RayCast_ps.hlsl", "ps_5_1", macro,
                        &isoRenderPS[i][j][(SparseVolume::FilterType)k][0][0]));
                    macro[8].Definition = "1"; // DEPTH_OUT
                    V(_Compile(L"SparseVolume_RayCast_ps.hlsl", "ps_5_1", macro,
                        &isoRenderPS[i][j][(SparseVolume::FilterType)k][0][1]));
                    macro[8].Definition = "0"; // DEPTH_OUT
                    macro[7].Definition = "1"; // USE_NORMAL
                    V(_Compile(L"SparseVolume_RayCast_ps.hlsl", "ps_5_1", macro,
                        &isoRenderPS[i][j][(SparseVolume::FilterType)k][1][0]));
                    macro[8].Definition = "1"; // DEPTH_OUT
                    V(_Compile(L"SparseVolume_RayCast_ps.hlsl", "ps_5_1", macro,
                        &isoRenderPS[i][j][(SparseVolume::FilterType)k][1][1]));
                    macro[8].Definition = "0"; // DEPTH_OUT
                    macro[7].Definition = "0"; // USE_NORMAL
                    macro[6].Definition = "0"; // ISO_SURFACE
                }
                macro[4].Definition = "0"; // FILTER_READ
                macro[DefIdx].Definition = "0";
            }
        }
        // Create Rootsignature
        _rootsig.Reset(4, 2);
        _rootsig.InitStaticSampler(0, Graphics::g_SamplerLinearClampDesc);
        _rootsig.InitStaticSampler(1, Graphics::g_SamplerAnisoWrapDesc);
        _rootsig[0].InitAsConstantBuffer(0);
        _rootsig[1].InitAsConstantBuffer(1);
        _rootsig[2].InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 2);
        _rootsig[3].InitAsDescriptorRange(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
        _rootsig.Finalize(L"SparseVolume",
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS);

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        // Create PSO for volume update and volume render
        DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
        DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
        DXGI_FORMAT Tex3DFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        for (int k = 0; k < SparseVolume::kNumStruct; ++k) {
            for (int i = 0; i < ManagedBuf::kNumType; ++i) {
                for (int j = 0; j < SparseVolume::kNumFilter; ++j) {
                    _gfxVolumeRenderPSO[i][k][j].SetRootSignature(_rootsig);
                    _gfxVolumeRenderPSO[i][k][j].SetInputLayout(
                        _countof(inputElementDescs), inputElementDescs);
                    _gfxVolumeRenderPSO[i][k][j].SetRasterizerState(
                        Graphics::g_RasterizerDefault);
                    _gfxVolumeRenderPSO[i][k][j].SetBlendState(
                        Graphics::g_BlendDisable);
                    _gfxVolumeRenderPSO[i][k][j].SetDepthStencilState(
                        Graphics::g_DepthStateReadWrite);
                    _gfxVolumeRenderPSO[i][k][j].SetSampleMask(UINT_MAX);
                    _gfxVolumeRenderPSO[i][k][j].SetPrimitiveTopologyType(
                        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
                    _gfxVolumeRenderPSO[i][k][j].SetRenderTargetFormats(
                        1, &ColorFormat, DepthFormat);
                    _gfxVolumeRenderPSO[i][k][j].SetVertexShader(
                        cubeVS->GetBufferPointer(), cubeVS->GetBufferSize());

                    _gfxISOSurfRenderPSO[i][k][j][0][0] =
                        _gfxVolumeRenderPSO[i][k][j];
                    _gfxISOSurfRenderPSO[i][k][j][0][0].SetPixelShader(
                        isoRenderPS[i][k][j][0][0]->GetBufferPointer(),
                        isoRenderPS[i][k][j][0][0]->GetBufferSize());
                    _gfxISOSurfRenderPSO[i][k][j][0][0].Finalize();
                    _gfxISOSurfRenderPSO[i][k][j][0][1] =
                        _gfxVolumeRenderPSO[i][k][j];
                    _gfxISOSurfRenderPSO[i][k][j][0][1].SetPixelShader(
                        isoRenderPS[i][k][j][0][1]->GetBufferPointer(),
                        isoRenderPS[i][k][j][0][1]->GetBufferSize());
                    _gfxISOSurfRenderPSO[i][k][j][0][1].Finalize();

                    _gfxISOSurfRenderPSO[i][k][j][1][0] =
                        _gfxVolumeRenderPSO[i][k][j];
                    _gfxISOSurfRenderPSO[i][k][j][1][0].SetPixelShader(
                        isoRenderPS[i][k][j][1][0]->GetBufferPointer(),
                        isoRenderPS[i][k][j][1][0]->GetBufferSize());
                    _gfxISOSurfRenderPSO[i][k][j][1][0].Finalize();
                    _gfxISOSurfRenderPSO[i][k][j][1][1] =
                        _gfxVolumeRenderPSO[i][k][j];
                    _gfxISOSurfRenderPSO[i][k][j][1][1].SetPixelShader(
                        isoRenderPS[i][k][j][1][1]->GetBufferPointer(),
                        isoRenderPS[i][k][j][1][1]->GetBufferSize());
                    _gfxISOSurfRenderPSO[i][k][j][1][1].Finalize();

                    _gfxVolumeRenderPSO[i][k][j].SetPixelShader(
                        raycastPS[i][k][j]->GetBufferPointer(),
                        raycastPS[i][k][j]->GetBufferSize());
                    _gfxVolumeRenderPSO[i][k][j].Finalize();
                }
                _cptUpdatePSO[i][k].SetRootSignature(_rootsig);
                _cptUpdatePSO[i][k].SetComputeShader(
                    volUpdateCS[i][k]->GetBufferPointer(),
                    volUpdateCS[i][k]->GetBufferSize());
                _cptUpdatePSO[i][k].Finalize();

                _gfxUpdatePSO[i][k].SetRootSignature(_rootsig);
                _gfxUpdatePSO[i][k].SetInputLayout(
                    _countof(inputElementDescs), inputElementDescs);
                _gfxUpdatePSO[i][k].SetRasterizerState(
                    Graphics::g_RasterizerDefault);
                _gfxUpdatePSO[i][k].SetBlendState(Graphics::g_BlendDisable);
                _gfxUpdatePSO[i][k].SetDepthStencilState(
                    Graphics::g_DepthStateDisabled);
                _gfxUpdatePSO[i][k].SetSampleMask(UINT_MAX);
                _gfxUpdatePSO[i][k].SetPrimitiveTopologyType(
                    D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
                _gfxUpdatePSO[i][k].SetRenderTargetFormats(
                    1, &Tex3DFormat, DXGI_FORMAT_UNKNOWN);
                _gfxUpdatePSO[i][k].SetVertexShader(
                    volUpdateVS->GetBufferPointer(),
                    volUpdateVS->GetBufferSize());
                _gfxUpdatePSO[i][k].SetGeometryShader(
                    volUpdateGS->GetBufferPointer(),
                    volUpdateGS->GetBufferSize());
                _gfxUpdatePSO[i][k].SetPixelShader(
                    volUpdatePS[i][k]->GetBufferPointer(),
                    volUpdatePS[i][k]->GetBufferSize());
                _gfxUpdatePSO[i][k].Finalize();
            }
        }

        // Create PSO for render near far plane
        ComPtr<ID3DBlob> stepInfoPS, stepInfoDebugPS, resetCS;
        D3D_SHADER_MACRO macro1[] = {
            {"__hlsl", "1"},
            {"DEBUG_VIEW", "0"},
            {nullptr, nullptr}
        };
        V(_Compile(L"SparseVolume_StepInfo_cs.hlsl", "cs_5_1",
            macro1, &resetCS));
        V(_Compile(L"SparseVolume_StepInfo_ps.hlsl", "ps_5_1",
            macro1, &stepInfoPS));
        V(_Compile(L"SparseVolume_StepInfo_vs.hlsl", "vs_5_1",
            macro1, &stepInfoVS));
        macro[1].Definition = "1";
        V(_Compile(L"SparseVolume_StepInfo_ps.hlsl", "ps_5_1",
            macro1, &stepInfoDebugPS));

        // Create PSO for clean brick volume
        _cptFlagVolResetPSO.SetRootSignature(_rootsig);
        _cptFlagVolResetPSO.SetComputeShader(
            resetCS->GetBufferPointer(), resetCS->GetBufferSize());
        _cptFlagVolResetPSO.Finalize();

        _gfxStepInfoPSO.SetRootSignature(_rootsig);
        _gfxStepInfoPSO.SetPrimitiveRestart(
            D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF);
        _gfxStepInfoPSO.SetInputLayout(
            _countof(inputElementDescs), inputElementDescs);
        _gfxStepInfoPSO.SetDepthStencilState(Graphics::g_DepthStateDisabled);
        _gfxStepInfoPSO.SetSampleMask(UINT_MAX);
        _gfxStepInfoPSO.SetVertexShader(
            stepInfoVS->GetBufferPointer(), stepInfoVS->GetBufferSize());
        _gfxStepInfoDebugPSO[0] = _gfxStepInfoPSO;
        _gfxStepInfoPSO.SetRasterizerState(Graphics::g_RasterizerTwoSided);
        D3D12_RASTERIZER_DESC rastDesc = Graphics::g_RasterizerTwoSided;
        rastDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
        _gfxStepInfoDebugPSO[0].SetRasterizerState(rastDesc);
        _gfxStepInfoDebugPSO[0].SetBlendState(Graphics::g_BlendDisable);
        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = false;
        blendDesc.IndependentBlendEnable = false;
        blendDesc.RenderTarget[0].BlendEnable = true;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_MIN;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_MAX;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN;
        _gfxStepInfoPSO.SetBlendState(blendDesc);
        _gfxStepInfoPSO.SetPrimitiveTopologyType(
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        _gfxStepInfoDebugPSO[0].SetPrimitiveTopologyType(
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
        _gfxStepInfoDebugPSO[0].SetRenderTargetFormats(
            1, &ColorFormat, DepthFormat);
        _gfxStepInfoPSO.SetRenderTargetFormats(
            1, &_stepInfoTexFormat, DXGI_FORMAT_UNKNOWN);
        _gfxStepInfoPSO.SetPixelShader(
            stepInfoPS->GetBufferPointer(), stepInfoPS->GetBufferSize());
        _gfxStepInfoDebugPSO[0].SetPixelShader(
            stepInfoPS->GetBufferPointer(), stepInfoDebugPS->GetBufferSize());
        _gfxStepInfoPSO.Finalize();
        _gfxStepInfoDebugPSO[1] = _gfxStepInfoDebugPSO[0];
        _gfxStepInfoDebugPSO[1].SetDepthStencilState(
            Graphics::g_DepthStateReadOnly);
        _gfxStepInfoDebugPSO[1].Finalize();
        _gfxStepInfoDebugPSO[0].Finalize();

        const uint32_t vertexBufferSize = sizeof(cubeVertices);
        _cubeVB.Create(L"Vertex Buffer", ARRAYSIZE(cubeVertices),
            sizeof(XMFLOAT3), (void*)cubeVertices);

        _cubeTriangleStripIB.Create(L"Cube TriangleStrip Index Buffer",
            ARRAYSIZE(cubeTrianglesStripIndices), sizeof(uint16_t),
            (void*)cubeTrianglesStripIndices);

        _cubeLineStripIB.Create(L"Cube LineStrip Index Buffer",
            ARRAYSIZE(cubeLineStripIndices), sizeof(uint16_t),
            (void*)cubeLineStripIndices);
    }
}

SparseVolume::SparseVolume()
    : _volBuf(DXGI_FORMAT_R16G16B16A16_FLOAT, XMUINT3(256, 256, 128)),
    _stepInfoTex(XMVectorSet(MAX_DEPTH,0,0,0))
{
    _volParam = &_cbPerCall.vParam;
    _volParam->fMaxDensity = 1.2f;
    _volParam->fMinDensity = 0.8f;
    _volParam->fVoxelSize = 1.f / 256.f;
    _ratioIdx = 0;
    _cbPerCall.uNumOfBalls = 20;
}

SparseVolume::~SparseVolume()
{
    OnDestory();
}

void
SparseVolume::OnCreateResource()
{
    ASSERT(Graphics::g_device);
    // Create resource for volume
    _volBuf.CreateResource();

    const uint3 reso = _volBuf.GetReso();
    _submittedReso = reso;
    _UpdateVolumeSettings(reso);
    _ratioIdx = (uint)(_ratios.size() - 2);
    _volParam->uVoxelBrickRatio = _ratios[_ratioIdx];
    
    // Create Spacial Structure Buffer
    _CreateBrickVolume(reso, _ratios[_ratioIdx]);

    for (int i = 0; i < MAX_BALLS; ++i) {
        _AddBall();
    }

    std::call_once(_psoCompiled_flag, _CreatePSOs);
}

void
SparseVolume::OnDestory()
{
    _volBuf.Destory();
    _flagVol.Destroy();
    _stepInfoTex.Destroy();
    _cubeVB.Destroy();
    _cubeTriangleStripIB.Destroy();
    _cubeLineStripIB.Destroy();
}

void
SparseVolume::OnResize()
{
    _stepInfoTex.Destroy();
    // Create MinMax Buffer
    _stepInfoTex.Create(L"StepInfoTex", Graphics::g_SceneColorBuffer.GetWidth(),
        Graphics::g_SceneColorBuffer.GetHeight(), 0, _stepInfoTexFormat);
}

void
SparseVolume::OnUpdate()
{
    ManagedBuf::BufInterface newBufInterface = _volBuf.GetResource();
    _needVolumeRebuild = _curBufInterface.resource != newBufInterface.resource; 
    _curBufInterface = newBufInterface;

    const uint3& reso = _volBuf.GetReso();
    if (_IsResolutionChanged(reso, _curReso)) {
        _curReso = reso;
        _UpdateVolumeSettings(reso);
        _CreateBrickVolume(reso, _ratios[_ratioIdx]);
    }
}

void
SparseVolume::OnRender(CommandContext& cmdContext, const DirectX::XMMATRIX& wvp,
    const DirectX::XMMATRIX& mView, const DirectX::XMFLOAT4& eyePos)
{
    static bool usePS = _usePSUpdate;
    _UpdatePerFrameData(wvp, mView, eyePos);

    cmdContext.BeginResourceTransition(Graphics::g_SceneColorBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdContext.BeginResourceTransition(Graphics::g_SceneDepthBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    if (_isAnimated || _needVolumeRebuild) {
        ComputeContext& cptContext = cmdContext.GetComputeContext();
        if (_useStepInfoTex) {
            cptContext.TransitionResource(_flagVol,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            _CleanBrickVolume(cptContext);
            cptContext.TransitionResource(_flagVol,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        cmdContext.TransitionResource(*_curBufInterface.resource,
            usePS && _curBufInterface.type == ManagedBuf::k3DTexBuffer
            ? D3D12_RESOURCE_STATE_RENDER_TARGET
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        _UpdateVolume(cmdContext, _curBufInterface, usePS);
        cmdContext.BeginResourceTransition(*_curBufInterface.resource,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (_useStepInfoTex) {
            cmdContext.BeginResourceTransition(_flagVol,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }

    GraphicsContext& gfxContext = cmdContext.GetGraphicsContext();
    gfxContext.TransitionResource(Graphics::g_SceneColorBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    gfxContext.TransitionResource(Graphics::g_SceneDepthBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, !_useStepInfoTex);
    
    if (_useStepInfoTex) {
        gfxContext.TransitionResource(_stepInfoTex,
            D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        gfxContext.ClearColor(_stepInfoTex);
    }
    
    gfxContext.ClearColor(Graphics::g_SceneColorBuffer);
    gfxContext.ClearDepth(Graphics::g_SceneDepthBuffer);
    
    gfxContext.SetRootSignature(_rootsig);
    gfxContext.SetDynamicConstantBufferView(
        0, sizeof(_cbPerFrame), (void*)&_cbPerFrame);
    gfxContext.SetDynamicConstantBufferView(
        1, sizeof(_cbPerCall), (void*)&_cbPerCall);
    gfxContext.SetViewport(Graphics::g_DisplayPlaneViewPort);
    gfxContext.SetScisor(Graphics::g_DisplayPlaneScissorRect);
    gfxContext.SetVertexBuffer(0, _cubeVB.VertexBufferView());
    
    if (_useStepInfoTex) {
        gfxContext.TransitionResource(_flagVol,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _RenderNearFar(gfxContext);
        gfxContext.TransitionResource(_stepInfoTex,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    
    gfxContext.TransitionResource(*_curBufInterface.resource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    _RenderVolume(gfxContext, _curBufInterface);
    usePS = _usePSUpdate;
    gfxContext.BeginResourceTransition(*_curBufInterface.resource,
        usePS && _curBufInterface.type == ManagedBuf::k3DTexBuffer
        ? D3D12_RESOURCE_STATE_RENDER_TARGET
        : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    
    if (_useStepInfoTex) {
        if (_stepInfoDebug) {
            _RenderBrickGrid(gfxContext);
        }
        cmdContext.BeginResourceTransition(_stepInfoTex,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
}

void
SparseVolume::RenderGui()
{
    static bool showPenal = true;
    if (ImGui::CollapsingHeader("Sparse Volume", 0, true, true)) {
        ImGui::Checkbox("Animation", &_isAnimated);
        ImGui::Separator();
        if (ImGui::Checkbox("StepInfoTex", &_useStepInfoTex) &&
            _useStepInfoTex) {
            _needVolumeRebuild |= true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Debug", &_stepInfoDebug);
        ImGui::Checkbox("Use PS Update", &_usePSUpdate);

        ImGui::Checkbox("ISOSurface", &_isoRender);
        if (_isoRender) {
            ImGui::SameLine();
            ImGui::Checkbox("Use Normal", &_useNormal);
            ImGui::SameLine();
            ImGui::Checkbox("WriteDepth", &_writeDepth);
        }
        ImGui::Separator();

        static int iFilterType = (int)_filterType;
        ImGui::RadioButton("No Filter", &iFilterType, kNoFilter);
        ImGui::RadioButton("Linear Filter", &iFilterType, kLinearFilter);
        ImGui::RadioButton("Linear Sampler", &iFilterType, kSamplerLinear);
        ImGui::RadioButton("Aniso Sampler", &iFilterType, kSamplerAniso);
        if (_volBuf.GetType() != ManagedBuf::k3DTexBuffer &&
            iFilterType > kLinearFilter) {
            iFilterType = _filterType;
        }
        _filterType = (FilterType)iFilterType;
        static int uMetaballCount =
            (int)_cbPerCall.uNumOfBalls;
        if (ImGui::DragInt("Metaball Count",
            (int*)&_cbPerCall.uNumOfBalls, 0.5f, 5, 128)) {
            _needVolumeRebuild |= true;
        }

        ImGui::Separator();
        ImGui::Text("Buffer Settings:");
        static int uBufferBitChoice = _volBuf.GetBit();
        static int uBufferTypeChoice = _volBuf.GetType();
        ImGui::RadioButton("Use 16Bit Buffer", &uBufferBitChoice,
            ManagedBuf::k16Bit); ImGui::SameLine();
        ImGui::RadioButton("Use 32Bit Buffer", &uBufferBitChoice,
            ManagedBuf::k32Bit);
        if (uBufferTypeChoice == ManagedBuf::kStructuredBuffer) {
            uBufferBitChoice = ManagedBuf::k32Bit;
        }
        ImGui::RadioButton("Use Typed Buffer", &uBufferTypeChoice,
            ManagedBuf::kTypedBuffer);
        ImGui::RadioButton("Use Structured Buffer", &uBufferTypeChoice,
            ManagedBuf::kStructuredBuffer);
        ImGui::RadioButton("Use Texture3D Buffer", &uBufferTypeChoice,
            ManagedBuf::k3DTexBuffer);
        if ((iFilterType > kLinearFilter &&
            uBufferTypeChoice != ManagedBuf::k3DTexBuffer) ||
            (uBufferTypeChoice == ManagedBuf::kStructuredBuffer &&
            uBufferBitChoice == ManagedBuf::k16Bit)) {
            uBufferTypeChoice = _volBuf.GetType();
        }
        if ((uBufferTypeChoice != _volBuf.GetType() ||
            uBufferBitChoice != _volBuf.GetBit()) &&
            !_volBuf.ChangeResource(_volBuf.GetReso(),
                (ManagedBuf::Type)uBufferTypeChoice,
                (ManagedBuf::Bit)uBufferBitChoice)) {
                uBufferTypeChoice = _volBuf.GetType();
        }

        ImGui::Separator();
        ImGui::Text("Spacial Structure:");
        static int iIdx = _ratioIdx;
        ImGui::SliderInt("BrickBox Ratio", &iIdx, 0,
            (uint)(_ratios.size() - 1), "");
        iIdx = iIdx >= (int)_ratios.size() ? (int)_ratios.size() - 1 : iIdx;
        if (iIdx != _ratioIdx) {
            _ratioIdx = iIdx;
            PRINTINFO("Ratio:%d", _ratios[_ratioIdx]);
            _volParam->uVoxelBrickRatio = _ratios[_ratioIdx];
            _CreateBrickVolume(_curReso, _ratios[_ratioIdx]);
            _needVolumeRebuild |= true;
        }
        ImGui::Separator();

        ImGui::Text("Volume Size Settings:");
        static uint3 uiReso = _volBuf.GetReso();
        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("X:"); ImGui::SameLine();
        ImGui::RadioButton("128##X", (int*)&uiReso.x, 128); ImGui::SameLine();
        ImGui::RadioButton("256##X", (int*)&uiReso.x, 256); ImGui::SameLine();
        ImGui::RadioButton("384##X", (int*)&uiReso.x, 384);

        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("Y:"); ImGui::SameLine();
        ImGui::RadioButton("128##Y", (int*)&uiReso.y, 128); ImGui::SameLine();
        ImGui::RadioButton("256##Y", (int*)&uiReso.y, 256); ImGui::SameLine();
        ImGui::RadioButton("384##Y", (int*)&uiReso.y, 384);

        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("Z:"); ImGui::SameLine();
        ImGui::RadioButton("128##Z", (int*)&uiReso.z, 128); ImGui::SameLine();
        ImGui::RadioButton("256##Z", (int*)&uiReso.z, 256); ImGui::SameLine();
        ImGui::RadioButton("384##Z", (int*)&uiReso.z, 384);
        if ((_IsResolutionChanged(uiReso, _submittedReso) ||
            _volBuf.GetType() != uBufferTypeChoice) &&
            _volBuf.ChangeResource(
                uiReso, _volBuf.GetType(), ManagedBuf::k32Bit)) {
            PRINTINFO("Reso:%dx%dx%d", uiReso.x, uiReso.y, uiReso.z);
            _submittedReso = uiReso;
        } else {
            uiReso = _submittedReso;
        }
    }
}

void
SparseVolume::_CreateBrickVolume(const uint3& reso, const uint ratio)
{
    Graphics::g_cmdListMngr.IdleGPU();
    _flagVol.Destroy();
    _flagVol.Create(L"FlagVol", reso.x / ratio, reso.y / ratio,
        reso.z / ratio, 1, DXGI_FORMAT_R8_UINT);
}

void
SparseVolume::_UpdatePerFrameData(const DirectX::XMMATRIX& wvp,
    const DirectX::XMMATRIX& mView, const DirectX::XMFLOAT4& eyePos)
{
    _cbPerFrame.mWorldViewProj = wvp;
    _cbPerFrame.mView = mView;
    _cbPerFrame.f4ViewPos = eyePos;
    if (_isAnimated || _needVolumeRebuild) {
        _animateTime += Core::g_deltaTime;
        for (uint i = 0; i < _cbPerCall.uNumOfBalls; i++) {
            Ball ball = _ballsData[i];
            _cbPerFrame.f4Balls[i].x =
                ball.fOribtRadius * (float)cosf((float)_animateTime *
                    ball.fOribtSpeed + ball.fOribtStartPhase);
            _cbPerFrame.f4Balls[i].y =
                ball.fOribtRadius * (float)sinf((float)_animateTime *
                    ball.fOribtSpeed + ball.fOribtStartPhase);
            _cbPerFrame.f4Balls[i].z =
                0.3f * ball.fOribtRadius * (float)sinf(
                    2.f * (float)_animateTime * ball.fOribtSpeed +
                    ball.fOribtStartPhase);
            _cbPerFrame.f4Balls[i].w = ball.fPower;
            _cbPerFrame.f4BallsCol[i] = ball.f4Color;
        }
    }
}

void
SparseVolume::_AddBall()
{
    Ball ball;
    float r = (0.6f * frand() + 0.7f) * _cbPerCall.vParam.u3VoxelReso.x *
        _cbPerCall.vParam.fVoxelSize * 0.05f;
    ball.fPower = r * r;
    ball.fOribtRadius = _cbPerCall.vParam.u3VoxelReso.x *
        _cbPerCall.vParam.fVoxelSize * (0.3f + (frand() - 0.3f) * 0.2f);

    if (ball.fOribtRadius + r > 0.45f * _cbPerCall.vParam.u3VoxelReso.x *
        _cbPerCall.vParam.fVoxelSize) {
        r = 0.45f * _cbPerCall.vParam.u3VoxelReso.x *
            _cbPerCall.vParam.fVoxelSize - ball.fOribtRadius;
        ball.fPower = r * r;
    }
    float speedF = 6.f * (frand() - 0.5f);
    if (abs(speedF) < 1.f) {
        speedF = (speedF > 0.f ? 1.f : -1.f) * 1.f;
    }
    ball.fOribtSpeed = 1.0f / ball.fPower * 0.0005f * speedF;
    ball.fOribtStartPhase = frand() * 6.28f;

    float alpha = frand() * 6.28f;
    float beta = frand() * 6.28f;
    float gamma = frand() * 6.28f;

    XMFLOAT3 xPositive = XMFLOAT3(1.f, 0.f, 0.f);
    XMMATRIX rMatrix = XMMatrixRotationRollPitchYaw(alpha, beta, gamma);
    XMVECTOR colVect = XMVector3TransformNormal(
        XMLoadFloat3(&xPositive), rMatrix);
    XMFLOAT4 col;
    XMStoreFloat4(&col, colVect);
    col.x = abs(col.x);
    col.y = abs(col.y);
    col.z = abs(col.z);
    col.w = 1.f;
    ball.f4Color = col;

    if (_ballsData.size() < MAX_BALLS) {
        _ballsData.push_back(ball);
    }
}

void
SparseVolume::_UpdateVolumeSettings(const uint3 reso)
{
    uint3& xyz = _volParam->u3VoxelReso;
    if (xyz.x == reso.x && xyz.y == reso.y && xyz.z == reso.z) {
        return;
    }
    xyz = reso;
    const float voxelSize = _volParam->fVoxelSize;
    _volParam->f3InvVolSize = float3(1.f / reso.x, 1.f / reso.y, 1.f / reso.z);
    _volParam->f3BoxMax = float3(0.5f * reso.x * voxelSize,
        0.5f * reso.y * voxelSize, 0.5f * reso.z * voxelSize);
    _volParam->f3BoxMin = float3(-0.5f * reso.x * voxelSize,
        -0.5f * reso.y * voxelSize, -0.5f * reso.z * voxelSize);
    _BuildBrickRatioVector(min(reso.x, min(reso.y, reso.z)), _ratios);
    _ratioIdx = _ratioIdx >= (int)_ratios.size()
        ? (int)_ratios.size() - 1 : _ratioIdx;
    _volParam->uVoxelBrickRatio = _ratios[_ratioIdx];
}

void
SparseVolume::_CleanBrickVolume(ComputeContext& cptContext)
{
    GPU_PROFILE(cptContext, L"Volume Reset");
    cptContext.SetPipelineState(_cptFlagVolResetPSO);
    cptContext.SetRootSignature(_rootsig);
    cptContext.SetDynamicDescriptors(2, 1, 1, &_flagVol.GetUAV());
    const uint3 xyz = _volParam->u3VoxelReso;
    const uint ratio = _volParam->uVoxelBrickRatio;
    cptContext.Dispatch3D(xyz.x / ratio, xyz.y / ratio, xyz.z / ratio,
        THREAD_X, THREAD_Y, THREAD_Z);
}

void
SparseVolume::_UpdateVolume(CommandContext& cmdCtx,
    const ManagedBuf::BufInterface& buf, bool usePS)
{
    GPU_PROFILE(cmdCtx, L"Volume Updating");
    VolumeStruct type = _useStepInfoTex ? kFlagVol : kVoxel;
    const uint3 xyz = _volParam->u3VoxelReso;
    if (usePS) {
        GraphicsContext& gfxCtx = cmdCtx.GetGraphicsContext();
        gfxCtx.TransitionResource(
            *buf.dummyResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
        gfxCtx.SetPipelineState(_gfxUpdatePSO[buf.type][type]);
        gfxCtx.SetRootSignature(_rootsig);
        gfxCtx.SetDynamicConstantBufferView(
            0, sizeof(_cbPerFrame), (void*)&_cbPerFrame);
        gfxCtx.SetDynamicConstantBufferView(
            1, sizeof(_cbPerCall), (void*)&_cbPerCall);
        gfxCtx.SetDynamicDescriptors(2, 1, 1, &_flagVol.GetUAV());
        gfxCtx.SetDynamicDescriptors(2, 0, 1, &buf.UAV);
        D3D12_VIEWPORT viewPort = {};
        viewPort.Width = (FLOAT)xyz.x;
        viewPort.Height = (FLOAT)xyz.y;
        viewPort.MaxDepth = 1.f;
        D3D12_RECT scisserRect = {};
        scisserRect.right = (LONG)xyz.x;
        scisserRect.bottom = (LONG)xyz.y;
        gfxCtx.SetViewport(viewPort);
        gfxCtx.SetScisor(scisserRect);
        gfxCtx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        gfxCtx.SetRenderTarget(buf.RTV);
        gfxCtx.SetVertexBuffer(0, _cubeVB.VertexBufferView());
        gfxCtx.Draw(xyz.z);
    } else {
        ComputeContext& cptCtx = cmdCtx.GetComputeContext();
        cptCtx.SetPipelineState(_cptUpdatePSO[buf.type][type]);
        cptCtx.SetRootSignature(_rootsig);
        cptCtx.SetDynamicDescriptors(2, 0, 1, &buf.UAV);
        cptCtx.SetDynamicDescriptors(2, 1, 1, &_flagVol.GetUAV());
        cptCtx.SetDynamicConstantBufferView(
            0, sizeof(_cbPerFrame), (void*)&_cbPerFrame);
        cptCtx.SetDynamicConstantBufferView(
            1, sizeof(_cbPerCall), (void*)&_cbPerCall);
        cptCtx.Dispatch3D(xyz.x, xyz.y, xyz.z, THREAD_X, THREAD_Y, THREAD_Z);
    }
}

void
SparseVolume::_RenderNearFar(GraphicsContext& gfxContext)
{
    GPU_PROFILE(gfxContext, L"Render NearFar");
    gfxContext.SetRootSignature(_rootsig);
    gfxContext.SetPipelineState(_gfxStepInfoPSO);
    gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    gfxContext.SetRenderTargets(1, &_stepInfoTex.GetRTV());
    gfxContext.SetDynamicDescriptors(3, 1, 1, &_flagVol.GetSRV());
    gfxContext.SetIndexBuffer(_cubeTriangleStripIB.IndexBufferView());
    const uint3 xyz = _volParam->u3VoxelReso;
    const uint ratio = _volParam->uVoxelBrickRatio;
    uint BrickCount = xyz.x * xyz.y * xyz.z / ratio / ratio / ratio;
    gfxContext.DrawIndexedInstanced(
        CUBE_TRIANGLESTRIP_LENGTH, BrickCount, 0, 0, 0);
}

void
SparseVolume::_RenderVolume(GraphicsContext& gfxContext,
    const ManagedBuf::BufInterface& buf)
{
    GPU_PROFILE(gfxContext, L"Rendering");
    VolumeStruct type = _useStepInfoTex ? kFlagVol : kVoxel;
    if (_isoRender) {
        gfxContext.SetPipelineState(
            _gfxISOSurfRenderPSO
            [buf.type][type][_filterType][_useNormal][_writeDepth]);
    } else {
        gfxContext.SetPipelineState(
            _gfxVolumeRenderPSO[buf.type][type][_filterType]);
    }
    gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    gfxContext.SetDynamicDescriptors(3, 0, 1, &buf.SRV);
    if (_useStepInfoTex) {
        gfxContext.SetDynamicDescriptors(3, 1, 1, &_stepInfoTex.GetSRV());
    }
    gfxContext.SetRenderTargets(1, &Graphics::g_SceneColorBuffer.GetRTV(),
        Graphics::g_SceneDepthBuffer.GetDSV());
    gfxContext.SetIndexBuffer(_cubeTriangleStripIB.IndexBufferView());
    gfxContext.DrawIndexed(CUBE_TRIANGLESTRIP_LENGTH);
}

void
SparseVolume::_RenderBrickGrid(GraphicsContext& gfxContext)
{
    GPU_PROFILE(gfxContext, L"Render BrickGrid");
    gfxContext.SetPipelineState(_gfxStepInfoDebugPSO[_writeDepth]);
    gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    if (_writeDepth) {
        gfxContext.SetRenderTargets(1, &Graphics::g_SceneColorBuffer.GetRTV(),
            Graphics::g_SceneDepthBuffer.GetDSV());
    } else {
        gfxContext.SetRenderTargets(1, &Graphics::g_SceneColorBuffer.GetRTV());
    }
    gfxContext.SetDynamicDescriptors(3, 1, 1, &_flagVol.GetSRV());
    gfxContext.SetIndexBuffer(_cubeLineStripIB.IndexBufferView());
    const uint3 xyz = _volParam->u3VoxelReso;
    const uint ratio = _volParam->uVoxelBrickRatio;
    uint BrickCount = xyz.x * xyz.y * xyz.z / ratio / ratio / ratio;
    gfxContext.DrawIndexedInstanced(CUBE_LINESTRIP_LENGTH, BrickCount, 0, 0, 0);
}