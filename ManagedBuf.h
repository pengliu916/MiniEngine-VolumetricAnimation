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

    enum Bit {
        k16Bit = 0,
        k32Bit,
        kNumBitType
    };

    struct BufInterface {
        Type type;
        GpuResource* resource;
        GpuResource* dummyResource;
        D3D12_CPU_DESCRIPTOR_HANDLE SRV;
        D3D12_CPU_DESCRIPTOR_HANDLE UAV;
        D3D12_CPU_DESCRIPTOR_HANDLE RTV;
    };

    ManagedBuf(DXGI_FORMAT format, DirectX::XMUINT3 reso,
        Type defaultType = kTypedBuffer, Bit defualtBit = k32Bit);
    ~ManagedBuf();
    inline const Type GetType() const { return _currentType; };
    inline const Bit GetBit() const { return _currentBit; };
    inline const DirectX::XMUINT3 GetReso() const { return _reso; };
    void CreateResource();
    bool ChangeResource(const DirectX::XMUINT3& reso, const Type bufType,
        const Bit bufBit);
    BufInterface GetResource();
    void Destory();

private:
    void _CreateVolume(const DirectX::XMUINT3 reso,
        const Type bufType, const Bit bufBit, uint targetIdx);
    void _CookBuffer(const DirectX::XMUINT3 reso, const Type bufType,
        const Bit bufBit);

    VolumeTexture _volumeBuffer[2];
    StructuredBuffer _structBuffer[2];
    TypedBuffer _typedBuffer[2];
    VolumeTexture _dummyBuffer[2];

    Type _currentType;
    Type _newType;
    Type _deprecatedType;

    Bit _currentBit;
    Bit _newBit;

    DirectX::XMUINT3 _reso;
    DirectX::XMUINT3 _newReso;
};