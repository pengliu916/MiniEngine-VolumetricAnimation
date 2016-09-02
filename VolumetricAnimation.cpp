
#include "stdafx.h"
#include "VolumetricAnimation.h"

VolumetricAnimation::VolumetricAnimation(uint32_t width, uint32_t height,
    std::wstring name)
{
    m_width = width;
    m_height = height;
}

VolumetricAnimation::~VolumetricAnimation()
{
}

void VolumetricAnimation::ResetCameraView()
{
    auto center = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    auto radius = m_camOrbitRadius;
    auto maxRadius = m_camMaxOribtRadius;
    auto minRadius = m_camMinOribtRadius;
    auto longAngle = 3.1415926f / 2.f;//4.50f;
    auto latAngle = 3.1415926f / 2.f;//1.45f;
    m_camera.View(center, radius, minRadius, maxRadius, longAngle, latAngle);
}

void VolumetricAnimation::OnConfiguration()
{
    Core::g_config.FXAA = false;
    Core::g_config.swapChainDesc.BufferCount = 5;
    Core::g_config.swapChainDesc.Width = m_width;
    Core::g_config.swapChainDesc.Height = m_height;
}

HRESULT VolumetricAnimation::OnCreateResource()
{
    m_SparseVolume.OnCreateResource();
    m_DenseVolume.OnCreateResource();
    OnSizeChanged();
    ResetCameraView();
    return S_OK;
}

// Load size dependent resource
HRESULT VolumetricAnimation::OnSizeChanged()
{
    uint32_t width = Core::g_config.swapChainDesc.Width;
    uint32_t height = Core::g_config.swapChainDesc.Height;

    float fAspectRatio = width / (FLOAT)height;
    m_camera.Projection(XM_PIDIV2 / 2, fAspectRatio);
    return S_OK;
}

// Update frame-based values.
void VolumetricAnimation::OnUpdate()
{
    m_camera.ProcessInertia();
    //m_camera.OrbitX( Core::g_deltaTime );
    static bool showPenal = true;
    if (ImGui::Begin("VolumetricAnimation", &showPenal)) {
        ImGui::RadioButton("SparseVolume", &m_CurVolType, kSparseVolume);
        ImGui::SameLine();
        ImGui::RadioButton("DenseVolume", &m_CurVolType, kDenseVolume);
        switch (m_CurVolType) {
            case kSparseVolume:
                m_SparseVolume.RenderGui(); break;
            case  kDenseVolume:
                m_DenseVolume.RenderGui(); break;
        }
    }
    ImGui::End();
}

// Render the scene.
void VolumetricAnimation::OnRender(CommandContext& EngineContext)
{
    XMMATRIX view = m_camera.View();
    XMMATRIX proj = m_camera.Projection();

    XMFLOAT4 eyePos;
    XMStoreFloat4(&eyePos, m_camera.Eye());
    switch (m_CurVolType) {
        case kDenseVolume:
            m_DenseVolume.OnRender(EngineContext,
                XMMatrixMultiply(view, proj), eyePos);
            break;
        case kSparseVolume:
            m_SparseVolume.OnRender(EngineContext,
                XMMatrixMultiply(view, proj), view, eyePos);
            break;
    }
}

void VolumetricAnimation::OnDestroy()
{
}

bool VolumetricAnimation::OnEvent(MSG* msg)
{
    switch (msg->message) {
        case WM_MOUSEWHEEL: {
            auto delta = GET_WHEEL_DELTA_WPARAM(msg->wParam);
            m_camera.ZoomRadius(-0.007f*delta);
            return true;
        }
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            auto pointerId = GET_POINTERID_WPARAM(msg->wParam);
            POINTER_INFO pointerInfo;
            if (GetPointerInfo(pointerId, &pointerInfo)) {
                if (msg->message == WM_POINTERDOWN) {
                    // Compute pointer position in render units
                    POINT p = pointerInfo.ptPixelLocation;
                    ScreenToClient(Core::g_hwnd, &p);
                    RECT clientRect;
                    GetClientRect(Core::g_hwnd, &clientRect);
                    p.x = p.x * Core::g_config.swapChainDesc.Width /
                        (clientRect.right - clientRect.left);
                    p.y = p.y * Core::g_config.swapChainDesc.Height /
                        (clientRect.bottom - clientRect.top);
                    // Camera manipulation
                    m_camera.AddPointer(pointerId);
                }
            }

            // Otherwise send it to the camera controls
            m_camera.ProcessPointerFrames(pointerId, &pointerInfo);
            if (msg->message == WM_POINTERUP) m_camera.RemovePointer(pointerId);
            return true;
        }
    }
    return false;
}