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
    Type defaultType, Bit defaultBit)
    :_reso(reso),
    _currentType(defaultType),
    _newType(defaultType),
    _deprecatedType(defaultType),
    _currentBit(defaultBit),
    _newBit(defaultBit)
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
    _CreateVolume(_reso, _currentType, _currentBit, _activeIndex);
}

bool
ManagedBuf::ChangeResource(const DirectX::XMUINT3& reso,
    const Type bufType, const Bit bufBit)
{
    if ((reso.x == _reso.x && reso.y == _reso.y && reso.z == _reso.z &&
        bufType == _currentType && bufBit == _currentBit) ||
        _bufState.load(std::memory_order_acquire) != kNormal) {
        return false;
    }
    _bufState.store(kNewBufferCooking, std::memory_order_release);
    _newReso = reso;
    _newType = bufType;
    _newBit = bufBit;
    _backgroundThread = std::unique_ptr<thread_guard>(
        new thread_guard(std::thread(&ManagedBuf::_CookBuffer, this,
            reso, bufType, bufBit)));
    return true;
}

ManagedBuf::BufInterface
ManagedBuf::GetResource()
{
    switch (_bufState.load(std::memory_order_acquire)) {
    case kNewBufferReady:
        _activeIndex = 1 - _activeIndex;
        _currentType = _newType;
        _currentBit = _newBit;
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
    result.type = _currentType;
    result.dummyResource = &_dummyBuffer[_activeIndex];
    switch (result.type) {
    case kStructuredBuffer:
        result.resource = &_structBuffer[_activeIndex];
        result.SRV = _structBuffer[_activeIndex].GetSRV();
        result.UAV = _structBuffer[_activeIndex].GetUAV();
        result.RTV = _dummyBuffer[_activeIndex].GetRTV();
        break;
    case kTypedBuffer:
        result.resource = &_typedBuffer[_activeIndex];
        result.SRV = _typedBuffer[_activeIndex].GetSRV();
        result.UAV = _typedBuffer[_activeIndex].GetUAV();
        result.RTV = _dummyBuffer[_activeIndex].GetRTV();
        break;
    case k3DTexBuffer:
        result.resource = &_volumeBuffer[_activeIndex];
        result.SRV = _volumeBuffer[_activeIndex].GetSRV();
        result.UAV = _volumeBuffer[_activeIndex].GetUAV();
        result.RTV = _volumeBuffer[_activeIndex].GetRTV();
        break;
    }
    return result;
}

void
ManagedBuf::_CreateVolume(const DirectX::XMUINT3 reso,
    const Type bufType, const Bit bufBit, uint targetIdx)
{
    uint32_t volumeBufferElementCount = reso.x * reso.y * reso.z;
    uint32_t elementSize;
    DXGI_FORMAT format;
    switch (bufBit) {
    case k16Bit:
        elementSize = 4 * sizeof(uint16_t);
        format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    case k32Bit:
        elementSize = 4 * sizeof(uint32_t);
        format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;
    }
    switch (bufType) {
    case kStructuredBuffer:
        _structBuffer[targetIdx].Create(L"Struct Volume Buffer",
            volumeBufferElementCount, elementSize);
        break;
    case kTypedBuffer:
        _typedBuffer[targetIdx].SetFormat(format);
        _typedBuffer[targetIdx].Create(L"Typed Volume Buffer",
            volumeBufferElementCount, elementSize);
        break;
    case k3DTexBuffer:
        _volumeBuffer[targetIdx].Create(L"Texture3D Volume Buffer",
            reso.x, reso.y, reso.z, 1, format);
        break;
    }
    _dummyBuffer[targetIdx].Create(L"Dummy Texture3D Buffer",
        reso.x, reso.y, 1, 1, format);
}

void
ManagedBuf::_CookBuffer(const DirectX::XMUINT3 reso,
	const Type bufType, const Bit bufBit)
{
	_deprecatedType = _currentType;
	_CreateVolume(reso, bufType, bufBit, 1 - _activeIndex);
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
	_dummyBuffer[1 - _activeIndex].Destroy();
	_bufState.store(kNormal, std::memory_order_release);
}