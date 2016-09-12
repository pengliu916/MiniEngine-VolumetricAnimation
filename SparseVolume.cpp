#include "stdafx.h"
#include "SparseVolume.h"
#include <ppl.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace std;

#define frand() ((float)rand()/RAND_MAX)

SparseVolume::SparseVolume()
    :_backgroundThread(new thread_guard(std::thread()))
{
    UpdatePerCallData(_perCallConstantBufferData,
        XMUINT3(_currentWidth, _currentHeight, _currentDepth), 
        _currentVoxelSize, XMFLOAT2(_minDensity, _maxDensity), 
        _voxelBrickRatio, _numMetaBalls);
    for (int i = 0; i < MAX_BALLS; ++i) {
        AddBall();
    }
}

SparseVolume::~SparseVolume()
{
    Graphics::g_cmdListMngr.WaitForFence(
        Graphics::g_stats.lastFrameEndFence);
    _resourceState.store(kOldBufferRetired, memory_order_release);
}

void 
SparseVolume::OnCreateResource()
{
    HRESULT hr;
    ASSERT(Graphics::g_device);

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
                PRINTINFO("DXGI_FORMAT_R8G8B8A8_UINT typed load is supported");
            } else {
                PRINTWARN(
                    "DXGI_FORMAT_R8G8B8A8_UINT typed load is not supported");
            }
        } else {
            PRINTWARN("TypedUAVLoadAdditionalFormats load is not supported");
        }
    }

    // Compile Shaders
    ComPtr<ID3DBlob> VolumeUpdate_CS[kNumBufferType];
    ComPtr<ID3DBlob> Cube_VS;
    ComPtr<ID3DBlob> Raycast_PS[kNumBufferType][kNumFilterType];

    uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    D3D_SHADER_MACRO macro[] = {
        {"__hlsl", "1"},//0
        {"TYPED_UAV", "0"},//1
        {"STRUCT_UAV", "0"},//2
        {"TEX3D_UAV", "0"},//3
        {"FILTER_READ", "0"},//4
        {nullptr, nullptr}
    };

    V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
        _T("SparseVolume_vs.hlsl")).c_str(),macro,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs_cube_main",
        "vs_5_1", compileFlags, 0, &Cube_VS));

    uint DefIdx;
    for (int i = 0; i < kNumBufferType; ++i) {
        switch ((BufferType)i) {
            case kStructuredBuffer: DefIdx = 2; break;
            case kTypedBuffer: DefIdx = 1; break;
            case k3DTexBuffer: DefIdx = 3; break;
        }
        macro[DefIdx].Definition = "1";
        V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            _T("SparseVolume_cs.hlsl")).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "cs_volumeupdate_main",
            "cs_5_1", compileFlags, 0, &VolumeUpdate_CS[i]));

        V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            _T("SparseVolume_ps.hlsl")).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main",
            "ps_5_1", compileFlags, 0, &Raycast_PS[i][kNoFilter]));
        macro[4].Definition = "1"; // FILTER_READ
        V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            _T("SparseVolume_ps.hlsl")).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main",
            "ps_5_1", compileFlags, 0, &Raycast_PS[i][kLinearFilter]));
        macro[4].Definition = "2"; // FILTER_READ
        V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            _T("SparseVolume_ps.hlsl")).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main",
            "ps_5_1", compileFlags, 0, &Raycast_PS[i][kSamplerLinear]));
        macro[4].Definition = "3"; // FILTER_READ
        V(Graphics::CompileShaderFromFile(Core::GetAssetFullPath(
            _T("SparseVolume_ps.hlsl")).c_str(), macro,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main",
            "ps_5_1", compileFlags, 0, &Raycast_PS[i][kSamplerAniso]));
        macro[4].Definition = "0"; // FILTER_READ
        macro[DefIdx].Definition = "0";
    }
    // Create Rootsignature
    _rootsignature.Reset(4, 2);
    _rootsignature.InitStaticSampler(0, Graphics::g_SamplerLinearClampDesc);
    _rootsignature.InitStaticSampler(1, Graphics::g_SamplerAnisoWrapDesc);
    _rootsignature[0].InitAsConstantBuffer(0);
    _rootsignature[1].InitAsConstantBuffer(1);
    _rootsignature[2].InitAsDescriptorRange(
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    _rootsignature[3].InitAsDescriptorRange(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    _rootsignature.Finalize(L"SparseVolume",
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
        D3D12_APPEND_ALIGNED_ELEMENT,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    for (int i = 0; i < kNumBufferType; ++i) {
        DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
        DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
        for (int j = 0; j < kNumFilterType; ++j) {
            _graphicRenderPSO[i][0][j].SetRootSignature(_rootsignature);
            _graphicRenderPSO[i][0][j].SetInputLayout(
                _countof(inputElementDescs), inputElementDescs);
            _graphicRenderPSO[i][0][j].SetRasterizerState(
                Graphics::g_RasterizerDefault);
            _graphicRenderPSO[i][0][j].SetBlendState(Graphics::g_BlendDisable);
            _graphicRenderPSO[i][0][j].SetDepthStencilState(
                Graphics::g_DepthStateReadWrite);
            _graphicRenderPSO[i][0][j].SetSampleMask(UINT_MAX);
            _graphicRenderPSO[i][0][j].SetPrimitiveTopologyType(
                D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            _graphicRenderPSO[i][0][j].SetRenderTargetFormats(
                1, &ColorFormat, DepthFormat);
            _graphicRenderPSO[i][0][j].SetVertexShader(
                Cube_VS->GetBufferPointer(), Cube_VS->GetBufferSize());
            _graphicRenderPSO[i][0][j].SetPixelShader(
                Raycast_PS[i][j]->GetBufferPointer(),
                Raycast_PS[i][j]->GetBufferSize());
            _graphicRenderPSO[i][0][j].Finalize();
        }
        _computUpdatePSO[i][0].SetRootSignature(_rootsignature);
        _computUpdatePSO[i][0].SetComputeShader(
            VolumeUpdate_CS[i]->GetBufferPointer(),
            VolumeUpdate_CS[i]->GetBufferSize());
        _computUpdatePSO[i][0].Finalize();
    }
    // Define the geometry for a triangle.
    XMFLOAT3 cubeVertices[] = {
        {XMFLOAT3(-0.5f, -0.5f, -0.5f)},
        {XMFLOAT3(-0.5f, -0.5f,  0.5f)},
        {XMFLOAT3(-0.5f,  0.5f, -0.5f)},
        {XMFLOAT3(-0.5f,  0.5f,  0.5f)},
        {XMFLOAT3(0.5f, -0.5f, -0.5f)},
        {XMFLOAT3(0.5f, -0.5f,  0.5f)},
        {XMFLOAT3(0.5f,  0.5f, -0.5f)},
        {XMFLOAT3(0.5f,  0.5f,  0.5f)},
    };

    const uint32_t vertexBufferSize = sizeof(cubeVertices);
    _vertexBuffer.Create(L"Vertex Buffer", ARRAYSIZE(cubeVertices),
        sizeof(XMFLOAT3), (void*)cubeVertices);
    
    uint16_t cubeIndices[] = {
        6, 4, 2, 0, 1, 4, 5, 7, 1, 3, 2, 7, 6, 4
    };

    _indexBuffer.Create(L"Index Buffer", ARRAYSIZE(cubeIndices),
        sizeof(uint16_t), (void*)cubeIndices);

    // Create Buffer Resources
    uint32_t volumeBufferElementCount = 
        _currentDepth*_currentHeight*_currentWidth;
    switch (_currentBufferType) {
        case SparseVolume::kStructuredBuffer:
            _structVolumeBuffer[_onStageIndex].Create(
                L"Struct Volume Buffer", volumeBufferElementCount,
                4 * sizeof(uint32_t));
            break;
        case SparseVolume::kTypedBuffer:
            _typedVolumeBuffer[_onStageIndex].Create(
                L"Typed Volume Buffer", volumeBufferElementCount,
                4 * sizeof(uint32_t));
            break;
        case SparseVolume::k3DTexBuffer:
            _volumeTextureBuffer[_onStageIndex].Create(
                L"Texture3D Volume Buffer", _currentWidth, 
                _currentHeight, _currentDepth, DXGI_FORMAT_R32G32B32A32_FLOAT);
            break;
    }

    // Create the initial volume 
    _resourceState.store(kNewBufferCooking, memory_order_release);
    _backgroundThread = std::unique_ptr<thread_guard>(
        new thread_guard(std::thread(&SparseVolume::CookVolume, this, 
            _currentWidth, _currentHeight, _currentDepth,
            _currentBufferType, _currentBufferType)));
}

void
SparseVolume::OnRender(CommandContext& cmdContext, DirectX::XMMATRIX wvp,
    DirectX::XMMATRIX mView, DirectX::XMFLOAT4 eyePos)
{
    cmdContext.BeginResourceTransition(Graphics::g_SceneColorBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdContext.BeginResourceTransition(Graphics::g_SceneDepthBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    switch (_resourceState.load(memory_order_acquire)) {
        case kNewBufferReady:
            _currentBufferType = _newBufferType;
            _currentWidth = _newWidth;
            _currentHeight = _newHeight;
            _currentDepth = _newDepth;
            _onStageIndex = 1 - _onStageIndex;
            _fenceValue = Graphics::g_stats.lastFrameEndFence;
            _resourceState.store(kRetiringOldBuffer, memory_order_release);
            break;
        case kRetiringOldBuffer:
            if (Graphics::g_cmdListMngr.IsFenceComplete(_fenceValue)) {
                _resourceState.store(kOldBufferRetired, memory_order_release);
            }
            break;
    }
    UpdatePerFrameData(wvp, mView, eyePos);

#define BindVolumeResource(ctx,volResource,state) \
    ctx.TransitionResource(volResource,state); \
    ctx.SetDynamicDescriptors(2,0,1,&volResource.GetUAV());

    if (_isAnimated) {
        ComputeContext& cptContext = cmdContext.GetComputeContext();
        {
            GPU_PROFILE(cptContext, L"Volume Updating");
            cptContext.SetRootSignature(_rootsignature);
            cptContext.SetPipelineState(
                _computUpdatePSO[_currentBufferType][0]);
            cptContext.SetDynamicConstantBufferView(
                0, sizeof(_perFrameConstantBufferData),
                (void*)&_perFrameConstantBufferData);
            cptContext.SetDynamicConstantBufferView(
                1, sizeof(_perCallConstantBufferData),
                (void*)&_perCallConstantBufferData);
            switch (_currentBufferType) {
                case kStructuredBuffer:
                    BindVolumeResource(cptContext, 
                        _structVolumeBuffer[_onStageIndex],
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    break;
                case kTypedBuffer:
                    BindVolumeResource(cptContext,
                        _typedVolumeBuffer[_onStageIndex],
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    break;
                case k3DTexBuffer:
                    BindVolumeResource(cptContext,
                        _volumeTextureBuffer[_onStageIndex],
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    break;
            }
            cptContext.Dispatch(_currentWidth / THREAD_X,
                _currentHeight / THREAD_Y, _currentDepth / THREAD_Z);
        }
    }
#undef BindVolumeResource
#define BindVolumeResource(ctx,volResource,state) \
    ctx.TransitionResource(volResource,state); \
    ctx.SetDynamicDescriptors(3,0,1,&volResource.GetSRV());

    switch (_currentBufferType) {
        case kStructuredBuffer:
            cmdContext.BeginResourceTransition(
                _structVolumeBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            break;
        case kTypedBuffer:
            cmdContext.BeginResourceTransition(
                _typedVolumeBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            break;
        case k3DTexBuffer:
            cmdContext.BeginResourceTransition(
                _volumeTextureBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            break;
    }

    GraphicsContext& gfxContext = cmdContext.GetGraphicsContext();
    gfxContext.TransitionResource(Graphics::g_SceneColorBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    gfxContext.TransitionResource(Graphics::g_SceneDepthBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    gfxContext.ClearColor(Graphics::g_SceneColorBuffer);
    gfxContext.ClearDepth(Graphics::g_SceneDepthBuffer);
    {
        GPU_PROFILE(gfxContext, L"Rendering");
        gfxContext.SetRootSignature(_rootsignature);
        gfxContext.SetPipelineState(
            _graphicRenderPSO[_currentBufferType][0][_currentFilterType]);
        gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        gfxContext.SetDynamicConstantBufferView(
            0, sizeof(_perFrameConstantBufferData),
            (void*)&_perFrameConstantBufferData);
        gfxContext.SetDynamicConstantBufferView(
            1, sizeof(_perCallConstantBufferData),
            (void*)&_perCallConstantBufferData);
        switch (_currentBufferType) {
            case kStructuredBuffer:
                BindVolumeResource(gfxContext,
                _structVolumeBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                break;
            case kTypedBuffer:
                BindVolumeResource(gfxContext,
                    _typedVolumeBuffer[_onStageIndex],
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                break;
            case k3DTexBuffer:
                BindVolumeResource(gfxContext, 
                    _volumeTextureBuffer[_onStageIndex],
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                break;
        }
        gfxContext.SetRenderTargets(1, &Graphics::g_SceneColorBuffer.GetRTV(),
            Graphics::g_SceneDepthBuffer.GetDSV());
        gfxContext.SetViewport(Graphics::g_DisplayPlaneViewPort);
        gfxContext.SetScisor(Graphics::g_DisplayPlaneScissorRect);
        gfxContext.SetVertexBuffer(0, _vertexBuffer.VertexBufferView());
        gfxContext.SetIndexBuffer(_indexBuffer.IndexBufferView());
        gfxContext.DrawIndexed(14);
    }
#undef BindVolumeResource
    switch (_currentBufferType) {
        case kStructuredBuffer:
            cmdContext.BeginResourceTransition(
                _structVolumeBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            break;
        case kTypedBuffer:
            cmdContext.BeginResourceTransition(
                _typedVolumeBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            break;
        case k3DTexBuffer:
            cmdContext.BeginResourceTransition(
                _volumeTextureBuffer[_onStageIndex],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            break;
    }
}

void
SparseVolume::RenderGui()
{
    static bool showPenal = true;
    if (ImGui::CollapsingHeader("Sparse Volume", 0, true, true)) {
        ImGui::Separator();
        static int iFilterType = (int)_currentFilterType;
        ImGui::RadioButton("No Filter", &iFilterType, kNoFilter);
        ImGui::RadioButton("Linear Filter", &iFilterType, kLinearFilter);
        ImGui::RadioButton("Linear Sampler", &iFilterType, kSamplerLinear);
        ImGui::RadioButton("Aniso Sampler", &iFilterType, kSamplerAniso);
        if (_currentBufferType != k3DTexBuffer && iFilterType > kLinearFilter) {
            iFilterType = _currentFilterType;
        }
        _currentFilterType = (FilterType)iFilterType;
        static int uMetaballCount = 
            (int)_perCallConstantBufferData.uNumOfBalls;
        if (ImGui::DragInt("Metaball Count", 
            (int*)&_perCallConstantBufferData.uNumOfBalls, 0.5f, 5, 128)) {
            _numMetaBalls = _perCallConstantBufferData.uNumOfBalls;
        }

        ImGui::Separator();
        ImGui::Text("Buffer Settings:");
        static int uBufferChoice = _currentBufferType;
        ImGui::RadioButton("Use Typed Buffer", &uBufferChoice, kTypedBuffer);
        ImGui::RadioButton("Use Structured Buffer", &uBufferChoice, 
            kStructuredBuffer);
        ImGui::RadioButton("Use Texture3D Buffer", 
            &uBufferChoice, k3DTexBuffer);
        if (iFilterType > kLinearFilter && uBufferChoice != k3DTexBuffer) {
            uBufferChoice = _currentBufferType;
        }
        if (uBufferChoice != _currentBufferType && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newBufferType = (BufferType)uBufferChoice;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&SparseVolume::CookVolume, this, 
                    _currentWidth, _currentHeight, _currentDepth,
                    _newBufferType, _currentBufferType)));
        }
        ImGui::Separator();

        ImGui::Text("Volume Size Settings:");
        static int uiVolumeWide = _currentWidth;
        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("X:"); ImGui::SameLine();
        ImGui::RadioButton("128##X", &uiVolumeWide, 128); ImGui::SameLine();
        ImGui::RadioButton("256##X", &uiVolumeWide, 256); ImGui::SameLine();
        ImGui::RadioButton("384##X", &uiVolumeWide, 384);
        if (uiVolumeWide != _currentWidth && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newWidth = uiVolumeWide;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&SparseVolume::CookVolume, this, 
                    _newWidth, _currentHeight, _currentDepth,
                    _currentBufferType, _currentBufferType)));
        }

        static int uiVolumeHeight = _currentHeight;
        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("Y:"); ImGui::SameLine();
        ImGui::RadioButton("128##Y", &uiVolumeHeight, 128); ImGui::SameLine();
        ImGui::RadioButton("256##Y", &uiVolumeHeight, 256); ImGui::SameLine();
        ImGui::RadioButton("384##Y", &uiVolumeHeight, 384);
        if (uiVolumeHeight != _currentHeight && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newHeight = uiVolumeHeight;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&SparseVolume::CookVolume, this, 
                    _currentWidth, _newHeight, _currentDepth,
                    _currentBufferType, _currentBufferType)));
        }

        static int uiVolumeDepth = _currentDepth;
        ImGui::AlignFirstTextHeightToWidgets();
        ImGui::Text("Z:"); ImGui::SameLine();
        ImGui::RadioButton("128##Z", &uiVolumeDepth, 128); ImGui::SameLine();
        ImGui::RadioButton("256##Z", &uiVolumeDepth, 256); ImGui::SameLine();
        ImGui::RadioButton("384##Z", &uiVolumeDepth, 384);
        if (uiVolumeDepth != _currentDepth && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newDepth = uiVolumeDepth;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&SparseVolume::CookVolume, this, 
                    _currentWidth, _currentHeight, _newDepth,
                    _currentBufferType, _currentBufferType)));
        }
    }
}

void 
SparseVolume::UpdatePerCallData(PerCallDataCB& DataCB, 
    DirectX::XMUINT3 VoxRes, float VoxelSize, 
    DirectX::XMFLOAT2 MinMaxDensity, uint VoxBrickRatio, uint NumOfBalls)
{
    DataCB.u3VoxelReso = VoxRes;
    DataCB.fVoxelSize = VoxelSize;
    DataCB.f3InvVolSize = XMFLOAT3(1.f / (VoxRes.x * VoxelSize), 
        1.f / (VoxRes.y * VoxelSize), 1.f / (VoxRes.z * VoxelSize));
    DataCB.f2MinMaxDensity = MinMaxDensity;
    DataCB.uVoxelBrickRatio = VoxBrickRatio;
    DataCB.f3BoxMin = XMFLOAT3(-0.5f * VoxRes.x * VoxelSize,
        -0.5f * VoxRes.y * VoxelSize, -0.5f * VoxRes.z * VoxelSize);
    DataCB.uNumOfBalls = NumOfBalls;
    DataCB.f3BoxMax = XMFLOAT3(0.5f * VoxRes.x * VoxelSize,
        0.5f * VoxRes.y * VoxelSize, 0.5f * VoxRes.z * VoxelSize);
}

void
SparseVolume::UpdatePerFrameData(DirectX::XMMATRIX wvp, 
    DirectX::XMMATRIX mView, DirectX::XMFLOAT4 eyePos)
{
    _perFrameConstantBufferData.mWorldViewProj = wvp;
    _perFrameConstantBufferData.mView = mView;
    _perFrameConstantBufferData.f4ViewPos = eyePos;
    if (_isAnimated) {
        _animateTime += Core::g_deltaTime;
        for (uint i = 0; i < _perCallConstantBufferData.uNumOfBalls; i++) {
            Ball ball = _ballsData[i];
            _perFrameConstantBufferData.f4Balls[i].x = 
                ball.fOribtRadius * (float)cosf((float)_animateTime * 
                    ball.fOribtSpeed + ball.fOribtStartPhase);
            _perFrameConstantBufferData.f4Balls[i].y = 
                ball.fOribtRadius * (float)sinf((float)_animateTime * 
                    ball.fOribtSpeed + ball.fOribtStartPhase);
            _perFrameConstantBufferData.f4Balls[i].z = 
                0.3f * ball.fOribtRadius * (float)sinf(
                    2.f * (float)_animateTime * ball.fOribtSpeed + 
                    ball.fOribtStartPhase);
            _perFrameConstantBufferData.f4Balls[i].w = ball.fPower;
            _perFrameConstantBufferData.f4BallsCol[i] = ball.f4Color;
        }
    }
}

void
SparseVolume::CookVolume(uint32_t Width, uint32_t Height, uint32_t Depth,
    BufferType BufType, BufferType PreBufType)
{
    uint32_t BufferElmCount = Width * Height * Depth;

    switch (BufType) {
        case kTypedBuffer:
            _typedVolumeBuffer[1 - _onStageIndex].Create(
                L"Typed Volume Buffer", BufferElmCount, 4 * sizeof(uint32_t));
            break;
        case kStructuredBuffer:
            _structVolumeBuffer[1 - _onStageIndex].Create(
                L"Struct Volume Buffer", BufferElmCount, 4 * sizeof(uint32_t));
            break;
        case k3DTexBuffer:
            _volumeTextureBuffer[1 - _onStageIndex].Create(
                L"Texture3D Volume Buffer", Width, Height, Depth,
                DXGI_FORMAT_R32G32B32A32_FLOAT);
            break;
    }

    UpdatePerCallData(_perCallConstantBufferData, 
        XMUINT3(Width, Height, Depth), _currentVoxelSize, 
        XMFLOAT2(_minDensity, _maxDensity), _voxelBrickRatio, _numMetaBalls);

    _newBufferType = BufType;
    _newWidth = Width;
    _newHeight = Height;
    _newDepth = Depth;

    _resourceState.store(kNewBufferReady, memory_order_release);

    while (_resourceState.load(memory_order_acquire) != kOldBufferRetired) {
        this_thread::yield();
    }

    switch (BufType) {
        case kTypedBuffer:
            _typedVolumeBuffer[1 - _onStageIndex].Destroy();
            break;
        case kStructuredBuffer:
            _structVolumeBuffer[1 - _onStageIndex].Destroy();
            break;
        case k3DTexBuffer:
            _volumeTextureBuffer[1 - _onStageIndex].Destroy();
            break;
    }

    _currentBufferType = BufType;
    _currentWidth = _newWidth;
    _currentHeight = _newHeight;
    _currentDepth = _newDepth;

    _resourceState.store(kNormal, memory_order_release);
}

void SparseVolume::AddBall()
{
    Ball ball;
    float r = (0.6f * frand() + 0.7f) * 
        _perCallConstantBufferData.u3VoxelReso.x * 
        _perCallConstantBufferData.fVoxelSize * 0.05f;
    ball.fPower = r * r;
    ball.fOribtRadius = _perCallConstantBufferData.u3VoxelReso.x * 
        _perCallConstantBufferData.fVoxelSize * 
        (0.3f + (frand() - 0.3f) * 0.2f);

    if (ball.fOribtRadius + r > 
        0.45f * _perCallConstantBufferData.u3VoxelReso.x *
        _perCallConstantBufferData.fVoxelSize) {
        r = 0.45f * _perCallConstantBufferData.u3VoxelReso.x * 
            _perCallConstantBufferData.fVoxelSize - ball.fOribtRadius;
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

    XMMATRIX rMatrix = XMMatrixRotationRollPitchYaw(alpha, beta, gamma);
    XMVECTOR colVect = XMVector3TransformNormal(
        XMLoadFloat3(&XMFLOAT3(1, 0, 0)), rMatrix);
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