#pragma once
#include "SparseVolume.inl"

class ManagedBuf
{
public:
    enum Type {
        kStructuredBuffer = 0,
        kTypedBuffer,
        k3DTexBuffer,
        kNumType
    };

    struct BufInterface {
        GpuResource* resource;
        D3D12_CPU_DESCRIPTOR_HANDLE SRV;
        D3D12_CPU_DESCRIPTOR_HANDLE UAV;
    };

    ManagedBuf(DXGI_FORMAT format, DirectX::XMUINT3 reso,
        Type defaultType = kTypedBuffer);
    ~ManagedBuf();
    inline const Type GetType() const {return _currentType;};
    inline const DirectX::XMUINT3 GetReso() const {return _reso;};
    void CreateResource();
    bool ChangeResource(const DirectX::XMUINT3& reso, const Type bufType);
    BufInterface GetResource();

private:
    void _CreateVolume(const DirectX::XMUINT3 reso,
        const Type bufType, uint targetIdx);
    void _CookBuffer(const DirectX::XMUINT3 reso, const Type bufType);

    VolumeTexture _volumeBuffer[2];
    StructuredBuffer _structBuffer[2];
    TypedBuffer _typedBuffer[2];

    Type _currentType = kTypedBuffer;
    Type _newType = kTypedBuffer;
    Type _deprecatedType = kTypedBuffer;

    DirectX::XMUINT3 _reso;
    DirectX::XMUINT3 _newReso;
};