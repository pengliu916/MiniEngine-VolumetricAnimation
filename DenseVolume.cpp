#include "stdafx.h"
#include "DenseVolume.h"
#include <ppl.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace std;

DenseVolume::DenseVolume()
    :_backgroundThread(new thread_guard(std::thread()))
{
    for (int i = 0; i < kNumBufferType; ++i) {
        _constantBufferData[i].shiftingColVals[0] = int4(1, 0, 0, 0);
        _constantBufferData[i].shiftingColVals[1] = int4(0, 1, 0, 1);
        _constantBufferData[i].shiftingColVals[2] = int4(0, 0, 1, 2);
        _constantBufferData[i].shiftingColVals[3] = int4(1, 1, 0, 3);
        _constantBufferData[i].shiftingColVals[4] = int4(1, 0, 1, 4);
        _constantBufferData[i].shiftingColVals[5] = int4(0, 1, 1, 5);
        _constantBufferData[i].shiftingColVals[6] = int4(1, 1, 1, 6);
        _constantBufferData[i].voxelSize = _voxelSize;
    }
}

DenseVolume::~DenseVolume()
{
    Graphics::g_cmdListMngr.WaitForFence(Graphics::g_stats.lastFrameEndFence);
    _resourceState.store(kOldBufferRetired, memory_order_release);
}

void DenseVolume::OnCreateResource()
{
    HRESULT hr;
    ASSERT(Graphics::g_device);

    // Feature support checking
    D3D12_FEATURE_DATA_D3D12_OPTIONS FeatureData;
    ZeroMemory(&FeatureData, sizeof(FeatureData));
    V(Graphics::g_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,
        &FeatureData, sizeof(FeatureData)));
    if (SUCCEEDED(hr)) {
        // TypedUAVLoadAdditionalFormats contains a Boolean that tells you 
        // whether the feature is supported or not
        if (FeatureData.TypedUAVLoadAdditionalFormats) {
            // Can assume all-or-nothing?subset is supported
            // (e.g. R32G32B32A32_FLOAT)
            // Cannot assume other formats are supported, so we check:
            D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport = 
                {DXGI_FORMAT_R8G8B8A8_UINT, D3D12_FORMAT_SUPPORT1_NONE,
                D3D12_FORMAT_SUPPORT2_NONE};
            hr = Graphics::g_device->CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_SUPPORT, &FormatSupport,
                sizeof(FormatSupport));
            if (SUCCEEDED(hr) && (FormatSupport.Support2 & 
                D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0) {
                PRINTINFO("DXGI_FORMAT_R8G8B8A8_UINT typed load is supported");
                _typedLoadSupported = true;
            } else {
                PRINTWARN(
                    "DXGI_FORMAT_R8G8B8A8_UINT typed load is not supported");
            }
        } else {
            PRINTWARN("TypedUAVLoadAdditionalFormats load is not supported");
        }
    }

    // Compile Shaders
    ComPtr<ID3DBlob> BoundingCubeVS;
    ComPtr<ID3DBlob> RaycastPS[kNumBufferType];
    ComPtr<ID3DBlob> VolumeUpdateCS[kNumBufferType];

    uint32_t compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    D3D_SHADER_MACRO macro[] =
    {
        {"__hlsl", "1"},    // 0 
        {"VERTEX_SHADER", "0"},    // 1 
        {"PIXEL_SHADER", "0"},    // 2 
        {"COMPUTE_SHADER", "0"},    // 3
        {"TYPED_UAV", "0"},    // 4
        {"TYPED_LOAD_NOT_SUPPORTED", _typedLoadSupported ? "0" : "1"},    // 5
        {nullptr, nullptr}
    };
    macro[1].Definition = "1"; // VERTEX_SHADER
    V(Graphics::CompileShaderFromFile(
        Core::GetAssetFullPath(_T("DenseVolume.hlsl")).c_str(),
        macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs_boundingcube_main",
        "vs_5_1", compileFlags, 0, &BoundingCubeVS));
    macro[1].Definition = "0"; // VERTEX_SHADER
    char temp[8];
    for (int i = 0; i < kNumBufferType; ++i) {
        sprintf(temp, "%d", i);
        macro[4].Definition = temp; // TYPED_UAV
        macro[2].Definition = "1"; // PIXEL_SHADER
        V(Graphics::CompileShaderFromFile(
            Core::GetAssetFullPath(_T("DenseVolume.hlsl")).c_str(),
            macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_raycast_main",
            "ps_5_1", compileFlags, 0, &RaycastPS[i]));
        macro[2].Definition = "0"; // PIXEL_SHADER
        macro[3].Definition = "1"; // COMPUTE_SHADER
        V(Graphics::CompileShaderFromFile(
            Core::GetAssetFullPath(_T("DenseVolume.hlsl")).c_str(),
            macro, D3D_COMPILE_STANDARD_FILE_INCLUDE, "cs_volumeupdate_main",
            "cs_5_1", compileFlags, 0, &VolumeUpdateCS[i]));
        macro[3].Definition = "0"; // COMPUTE_SHADER
    }

    // Create Rootsignature
    _rootsignature.Reset(3);
    _rootsignature[0].InitAsConstantBuffer(0);
    _rootsignature[1].InitAsDescriptorRange(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    _rootsignature[2].InitAsDescriptorRange(
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    _rootsignature.Finalize(L"DenseVolume",
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    _graphicRenderPSO[kStructuredBuffer].SetRootSignature(_rootsignature);
    _computeUpdatePSO[kStructuredBuffer].SetRootSignature(_rootsignature);

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
        D3D12_APPEND_ALIGNED_ELEMENT,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    _graphicRenderPSO[kStructuredBuffer].SetInputLayout(
        _countof(inputElementDescs), inputElementDescs);
    _graphicRenderPSO[kStructuredBuffer].SetRasterizerState(
        Graphics::g_RasterizerDefault);
    _graphicRenderPSO[kStructuredBuffer].SetBlendState(
        Graphics::g_BlendDisable);
    _graphicRenderPSO[kStructuredBuffer].SetDepthStencilState(
        Graphics::g_DepthStateReadWrite);
    _graphicRenderPSO[kStructuredBuffer].SetSampleMask(UINT_MAX);
    _graphicRenderPSO[kStructuredBuffer].SetPrimitiveTopologyType(
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    DXGI_FORMAT ColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
    DXGI_FORMAT DepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();
    _graphicRenderPSO[kStructuredBuffer].SetRenderTargetFormats(1, 
        &ColorFormat, DepthFormat);
    _graphicRenderPSO[kStructuredBuffer].SetVertexShader(
        BoundingCubeVS->GetBufferPointer(), BoundingCubeVS->GetBufferSize());
    _graphicRenderPSO[kTypedBuffer] = _graphicRenderPSO[kStructuredBuffer];
    _graphicRenderPSO[kStructuredBuffer].SetPixelShader(
        RaycastPS[kStructuredBuffer]->GetBufferPointer(),
        RaycastPS[kStructuredBuffer]->GetBufferSize());
    _graphicRenderPSO[kTypedBuffer].SetPixelShader(
        RaycastPS[kTypedBuffer]->GetBufferPointer(),
        RaycastPS[kTypedBuffer]->GetBufferSize());

    _computeUpdatePSO[kTypedBuffer] = _computeUpdatePSO[kStructuredBuffer];
    _computeUpdatePSO[kTypedBuffer].SetComputeShader(
        VolumeUpdateCS[kTypedBuffer]->GetBufferPointer(),
        VolumeUpdateCS[kTypedBuffer]->GetBufferSize());
    _computeUpdatePSO[kStructuredBuffer].SetComputeShader(
        VolumeUpdateCS[kStructuredBuffer]->GetBufferPointer(),
        VolumeUpdateCS[kStructuredBuffer]->GetBufferSize());

    _graphicRenderPSO[kStructuredBuffer].Finalize();
    _graphicRenderPSO[kTypedBuffer].Finalize();
    _computeUpdatePSO[kStructuredBuffer].Finalize();
    _computeUpdatePSO[kTypedBuffer].Finalize();

    // Create Buffer Resources
    uint32_t volumeBufferElementCount =
        _currentDepth*_currentHeight*_currentWidth;
    if (_currentBufferType == kTypedBuffer) {
        _typedVolumeBuffer[_onStageIndex].SetFormat(DXGI_FORMAT_R8G8B8A8_UINT);
        _typedVolumeBuffer[_onStageIndex].Create(L"Typed Volume Buf",
            volumeBufferElementCount, 4 * sizeof(uint8_t));
    } else {
        _structuredVolumeBuffer[_onStageIndex].Create(L"Volume Buffer",
            volumeBufferElementCount, 4 * sizeof(uint8_t));
    }
    // Create the initial volume 
    _resourceState.store(kNewBufferCooking, memory_order_release);
    _backgroundThread = std::unique_ptr<thread_guard>(
        new thread_guard(std::thread(
            &DenseVolume::CookVolume, this, _currentWidth, _currentHeight,
            _currentDepth, _currentBufferType,
            _currentBufferType, _currentVolumeContent)));
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
        0,2,1, 1,2,3,  4,5,6, 5,7,6,  0,1,5, 0,5,4,
        2,6,7, 2,7,3,  0,4,6, 0,6,2,  1,3,7, 1,7,5,
    };

    _indexBuffer.Create(L"Index Buffer", ARRAYSIZE(cubeIndices),
        sizeof(uint16_t), (void*)cubeIndices);
}

void DenseVolume::OnRender(CommandContext& cmdContext,
    DirectX::XMMATRIX wvp, DirectX::XMFLOAT4 eyePos)
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
            if (Graphics::g_cmdListMngr.IsFenceComplete(_fenceValue))
                _resourceState.store(kOldBufferRetired, memory_order_release);
            break;
    }
    _constantBufferData[_onStageIndex].wvp = wvp;
    _constantBufferData[_onStageIndex].viewPos = eyePos;
    GpuBuffer* VolumeBuffer = (_currentBufferType == kStructuredBuffer
        ? (GpuBuffer*)&_structuredVolumeBuffer[_onStageIndex]
        : (GpuBuffer*)&_typedVolumeBuffer[_onStageIndex]);
    ComputeContext& cptContext = cmdContext.GetComputeContext();
    {
        GPU_PROFILE(cptContext, L"Volume Updating");
        cptContext.SetRootSignature(_rootsignature);
        cptContext.SetPipelineState(_computeUpdatePSO[_currentBufferType]);
        cptContext.TransitionResource(*VolumeBuffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cptContext.SetDynamicConstantBufferView(0,
            sizeof(DataCB), (void*)&_constantBufferData[_onStageIndex]);
        cptContext.SetDynamicDescriptors(1, 0, 1, &VolumeBuffer->GetSRV());
        cptContext.SetDynamicDescriptors(2, 0, 1, &VolumeBuffer->GetUAV());
        cptContext.Dispatch(_currentWidth / THREAD_X,
            _currentHeight / THREAD_Y, _currentDepth / THREAD_Z);
    }

    cmdContext.BeginResourceTransition(*VolumeBuffer,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
        gfxContext.SetPipelineState(_graphicRenderPSO[_currentBufferType]);
        gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gfxContext.TransitionResource(*VolumeBuffer,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gfxContext.SetDynamicConstantBufferView(0,
            sizeof(DataCB), (void*)&_constantBufferData[_onStageIndex]);
        gfxContext.SetDynamicDescriptors(1, 0, 1, &VolumeBuffer->GetSRV());
        gfxContext.SetDynamicDescriptors(2, 0, 1, &VolumeBuffer->GetUAV());
        gfxContext.SetRenderTargets(1, &Graphics::g_SceneColorBuffer.GetRTV(),
            Graphics::g_SceneDepthBuffer.GetDSV());
        gfxContext.SetViewport(Graphics::g_DisplayPlaneViewPort);
        gfxContext.SetScisor(Graphics::g_DisplayPlaneScissorRect);
        gfxContext.SetVertexBuffer(0, _vertexBuffer.VertexBufferView());
        gfxContext.SetIndexBuffer(_indexBuffer.IndexBufferView());
        gfxContext.DrawIndexed(36);
    }
    cmdContext.BeginResourceTransition(*VolumeBuffer,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void DenseVolume::RenderGui()
{
    static bool showPenal = true;
    if (ImGui::CollapsingHeader("Dense Volume", 0, true, true)) {
        ImGui::Text("Buffer Settings:");
        static int uBufferChoice = _currentBufferType;
        ImGui::RadioButton("Use Typed Buffer", &uBufferChoice, kTypedBuffer);
        ImGui::RadioButton("Use Structured Buffer",
            &uBufferChoice, kStructuredBuffer);
        if (uBufferChoice != _currentBufferType && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newBufferType = (BufferType)uBufferChoice;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&DenseVolume::CookVolume, this,
                    _currentWidth, _currentHeight,
                    _currentDepth, _newBufferType,
                    _currentBufferType, _currentVolumeContent)));
        }
        ImGui::Separator();

        ImGui::Text("Volume Animation Settings:");
        static int uVolContent = _currentVolumeContent;
        ImGui::RadioButton("Sphere Animation", &uVolContent, kSphere);
        ImGui::RadioButton("Cube Animation", &uVolContent, kDimond);
        if (uVolContent != _currentVolumeContent && 
            _resourceState.load(memory_order_acquire) == kNormal) {
            _resourceState.store(kNewBufferCooking, memory_order_release);
            _newVolumeContent = (VolumeContent)uVolContent;
            _backgroundThread = std::unique_ptr<thread_guard>(
                new thread_guard(std::thread(&DenseVolume::CookVolume, this,
                    _currentWidth, _currentHeight,
                    _currentDepth, _currentBufferType,
                    _currentBufferType, _newVolumeContent)));
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
                new thread_guard(std::thread(&DenseVolume::CookVolume, this, 
                    _newWidth, _currentHeight,
                    _currentDepth, _currentBufferType,
                    _currentBufferType, _currentVolumeContent)));
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
                new thread_guard(std::thread(&DenseVolume::CookVolume, this,
                    _currentWidth, _newHeight,
                    _currentDepth, _currentBufferType,
                    _currentBufferType, _currentVolumeContent)));
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
                new thread_guard(std::thread(&DenseVolume::CookVolume, this,
                    _currentWidth, _currentHeight,
                    _newDepth, _currentBufferType,
                    _currentBufferType, _currentVolumeContent)));
        }
    }
}

void DenseVolume::CookVolume(uint32_t Width, uint32_t Height, uint32_t Depth,
    BufferType BufType, BufferType PreBufType, VolumeContent VolType)
{
    uint32_t BufferElmCount = Width * Height * Depth;
    uint32_t BufferSize = Width * Height * Depth * 4 * sizeof(uint8_t);
    uint8_t* pBufPtr = (uint8_t*)malloc(BufferSize);

    float a = Width / 2.f;
    float b = Height / 2.f;
    float c = Depth / 2.f;

    float radius = VolType == kSphere 
        ? sqrt(a*a + b*b + c*c) 
        : (abs(a) + abs(b) + abs(c));

    uint32_t bgMax = 32;

    Concurrency::parallel_for(uint32_t(0), Depth, [&](uint32_t z)
    {
        for (uint32_t y = 0; y < Height; y++)
            for (uint32_t x = 0; x < Width; x++)
            {
                float _x = x - Width / 2.f;
                float _y = y - Height / 2.f;
                float _z = z - Depth / 2.f;
                float currentRaidus = VolType == kSphere 
                    ? sqrt(_x*_x + _y*_y + _z*_z) 
                    : (abs(_x) + abs(_y) + abs(_z));
                float scale = currentRaidus / radius;
                uint32_t maxColCnt = 4;
                assert(maxColCnt < COLOR_COUNT);
                float currentScale = scale * maxColCnt + 0.1f;
                uint32_t idx = COLOR_COUNT - (uint32_t)(currentScale)-1;
                float intensity = currentScale - (uint32_t)currentScale;
                uint32_t col = (uint32_t)(intensity * (255 - bgMax)) + 1;
                pBufPtr[(x + y*Width + z*Height*Width) * 4 + 0] =
                    32 + col * shiftingColVals[idx].x;
                pBufPtr[(x + y*Width + z*Height*Width) * 4 + 1] =
                    32 + col * shiftingColVals[idx].y;
                pBufPtr[(x + y*Width + z*Height*Width) * 4 + 2] =
                    32 + col * shiftingColVals[idx].z;
                pBufPtr[(x + y*Width + z*Height*Width) * 4 + 3] =
                    shiftingColVals[idx].w;
            }
    });

    if (BufType == kTypedBuffer) {
        _typedVolumeBuffer[1 - _onStageIndex].SetFormat(
            DXGI_FORMAT_R8G8B8A8_UINT);
        _typedVolumeBuffer[1 - _onStageIndex].Create(L"Typed Volume Buffer",
            BufferElmCount, 4 * sizeof(uint8_t), pBufPtr);
    }
    if (BufType == kStructuredBuffer) {
        _structuredVolumeBuffer[1 - _onStageIndex].Create(
            L"Struct Volume Buffer",BufferElmCount,
            4 * sizeof(uint8_t), pBufPtr);
    }

    _constantBufferData[1 - _onStageIndex].bgCol = XMINT4(32, 32, 32, 32);
    _constantBufferData[1 - _onStageIndex].voxelResolution = 
        XMINT3(Width, Height, Depth);
    _constantBufferData[1 - _onStageIndex].boxMin = 
        XMFLOAT3(_voxelSize*-0.5f*Width, _voxelSize*-0.5f*Height,
            _voxelSize*-0.5f*Depth);
    _constantBufferData[1 - _onStageIndex].boxMax = 
        XMFLOAT3(_voxelSize*0.5f*Width, _voxelSize*0.5f*Height,
            _voxelSize*0.5f*Depth);
    _newBufferType = BufType;
    _newWidth = Width;
    _newHeight = Height;
    _newDepth = Depth;

    _resourceState.store(kNewBufferReady, memory_order_release);

    while (_resourceState.load(memory_order_acquire) != kOldBufferRetired) {
        this_thread::yield();
    }

    if (PreBufType == kTypedBuffer) {
        _typedVolumeBuffer[1 - _onStageIndex].Destroy();
    }
    if (PreBufType == kStructuredBuffer) {
        _structuredVolumeBuffer[1 - _onStageIndex].Destroy();
    }

    _currentBufferType = BufType;
    _currentVolumeContent = _newVolumeContent;
    _currentWidth = _newWidth;
    _currentHeight = _newHeight;
    _currentDepth = _newDepth;

    _resourceState.store(kNormal, memory_order_release);
}