#include "stdafx.h"
#include "ManagedBuf.h"

namespace {
    enum ResourceState {
        kNormal = 0,
        kNewBufferCooking,
        kNewBufferReady,
        kRetiringOldBuffer,
        kOldBufferRetired,
        kNumStates
    };

    std::atomic<ResourceState> _bufState(kNormal);
    uint8_t _activeIndex = 0;
    uint64_t _fenceValue = 0;
    std::unique_ptr<thread_guard> _backgroundThread;
};

ManagedBuf::ManagedBuf(DXGI_FORMAT format, DirectX::XMUINT3 reso,
    Type defaultType)
    : _typedBuffer{format, format},
    _reso(reso),
    _currentType(defaultType)
{
}

ManagedBuf::~ManagedBuf()
{
    if (_bufState.load(std::memory_order_acquire) != kNormal) {
        Graphics::g_cmdListMngr.WaitForFence(
            Graphics::g_stats.lastFrameEndFence);
        _bufState.store(kOldBufferRetired, std::memory_order_release);
    }
}

void
ManagedBuf::CreateResource()
{
    _CreateVolume(_reso, _currentType, _activeIndex);
}

bool
ManagedBuf::ChangeResource(const DirectX::XMUINT3& reso,
    const Type bufType)
{
    if ((reso.x == _reso.x && reso.y == _reso.y &&
        reso.z == _reso.z && bufType == _currentType) ||
        _bufState.load(std::memory_order_acquire) != kNormal) {
        return false;
    }
    _bufState.store(kNewBufferCooking, std::memory_order_release);
    _newReso = reso;
    _newType = bufType;
    _backgroundThread = std::unique_ptr<thread_guard>(
        new thread_guard(std::thread(&ManagedBuf::_CookBuffer, this,
            reso, bufType)));
    return true;
}

ManagedBuf::BufInterface
ManagedBuf::GetResource()
{
    switch (_bufState.load(std::memory_order_acquire)) {
    case kNewBufferReady:
        _activeIndex = 1 - _activeIndex;
        _currentType = _newType;
        _reso = _newReso;
        _fenceValue = Graphics::g_stats.lastFrameEndFence;
        _bufState.store(kRetiringOldBuffer, std::memory_order_release);
        break;
    case kRetiringOldBuffer:
        if (Graphics::g_cmdListMngr.IsFenceComplete(_fenceValue)) {
            _bufState.store(kOldBufferRetired, std::memory_order_release);
        }
        break;
    }
    BufInterface result;
    switch (_currentType) {
    case kStructuredBuffer:
        result.resource = &_structBuffer[_activeIndex];
        result.SRV = _structBuffer[_activeIndex].GetSRV();
        result.UAV = _structBuffer[_activeIndex].GetUAV();
        break;
    case kTypedBuffer:
        result.resource = &_typedBuffer[_activeIndex];
        result.SRV = _typedBuffer[_activeIndex].GetSRV();
        result.UAV = _typedBuffer[_activeIndex].GetUAV();
        break;
    case k3DTexBuffer:
        result.resource = &_volumeBuffer[_activeIndex];
        result.SRV = _volumeBuffer[_activeIndex].GetSRV();
        result.UAV = _volumeBuffer[_activeIndex].GetUAV();
        break;
    }
    return result;
}

void
ManagedBuf::_CreateVolume(const DirectX::XMUINT3 reso,
    const Type bufType, uint targetIdx)
{
    uint32_t volumeBufferElementCount = reso.x * reso.y * reso.z;
    switch (bufType) {
    case kStructuredBuffer:
        _structBuffer[targetIdx].Create(
            L"Struct Volume Buffer", volumeBufferElementCount,
            4 * sizeof(uint32_t));
        break;
    case kTypedBuffer:
        _typedBuffer[targetIdx].Create(
            L"Typed Volume Buffer", volumeBufferElementCount,
            4 * sizeof(uint32_t));
        break;
    case k3DTexBuffer:
        _volumeBuffer[targetIdx].Create(
            L"Texture3D Volume Buffer", reso.x, reso.y, reso.z,
            DXGI_FORMAT_R32G32B32A32_FLOAT);
        break;
    }
}

void
ManagedBuf::_CookBuffer(const DirectX::XMUINT3 reso,
    const Type bufType)
{
    _deprecatedType = _currentType;
    _CreateVolume(reso, bufType, 1 - _activeIndex);
    _bufState.store(kNewBufferReady, std::memory_order_release);
    while (_bufState.load(std::memory_order_acquire) != kOldBufferRetired) {
        std::this_thread::yield();
    }
    switch (_deprecatedType) {
    case kTypedBuffer:
        _typedBuffer[1 - _activeIndex].Destroy();
        break;
    case kStructuredBuffer:
        _structBuffer[1 - _activeIndex].Destroy();
        break;
    case k3DTexBuffer:
        _volumeBuffer[1 - _activeIndex].Destroy();
        break;
    }
    _bufState.store(kNormal, std::memory_order_release);
}