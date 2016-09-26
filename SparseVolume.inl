#if !__hlsl
#pragma once
#endif // !__hlsl


// Params
#define MAX_BALLS 128
#define THREAD_X 8
#define THREAD_Y 8
#define THREAD_Z 8
#define MAX_DEPTH 10000
// Do not modify below this line

// The length of cube triangles-strip vertices
#define CUBE_TRIANGLESTRIP_LENGTH 14
// The length of cube line-strip vertices
#define CUBE_LINESTRIP_LENGTH 19

#if __hlsl
#define CBUFFER_ALIGN
#define REGISTER(x) :register(x)
#define STRUCT(x) x
#else
#define CBUFFER_ALIGN __declspec( \
    align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
#define REGISTER(x)
#define STRUCT(x) struct
typedef DirectX::XMMATRIX matrix;
typedef DirectX::XMFLOAT4 float4;
typedef DirectX::XMFLOAT3 float3;
typedef DirectX::XMFLOAT2 float2;
typedef DirectX::XMUINT3 uint3;
typedef uint32_t uint;
#endif

// will be put into constant buffer, pay attention to alignment
struct VolumeParam {
    uint3 u3VoxelReso;
    uint uVoxelBrickRatio;
    float3 f3InvVolSize;
    float fVoxelSize;
    float3 f3BoxMin;
    float fMaxDensity;
    float3 f3BoxMax;
    float fMinDensity;
};

CBUFFER_ALIGN STRUCT(cbuffer) PerFrameDataCB REGISTER(b0)
{
    matrix mWorldViewProj;
    matrix mView;
    float4 f4ViewPos;
    float4 f4Balls[MAX_BALLS];
    float4 f4BallsCol[MAX_BALLS];
#if !__hlsl
    void* operator new(size_t i) {
        return _aligned_malloc(i, 
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    };
    void operator delete(void* p) {
        _aligned_free(p);
    }
#endif
};

CBUFFER_ALIGN STRUCT(cbuffer) PerCallDataCB REGISTER(b1)
{
    VolumeParam vParam;
    uint uNumOfBalls;
    uint NIU[3];
#if !__hlsl
    void* operator new(size_t i) {
        return _aligned_malloc(i, 
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    };
    void operator delete(void* p) {
        _aligned_free(p);
    }
#endif
};
#undef CBUFFER_ALIGN