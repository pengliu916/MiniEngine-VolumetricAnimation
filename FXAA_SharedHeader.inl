// Do not modify below this line
#if __cplusplus
#define CBUFFER_ALIGN __declspec(align(16))
#else
#define CBUFFER_ALIGN
#endif

#if __hlsl
#define REGISTER(x) :register(x)
#define STRUCT(x) x
#else 
typedef DirectX::XMMATRIX	matrix;
typedef DirectX::XMINT4		int4;
typedef DirectX::XMINT3		int3;
typedef DirectX::XMFLOAT4	float4;
typedef DirectX::XMFLOAT3	float3;
typedef DirectX::XMFLOAT2	float2;
typedef UINT				uint;
#define REGISTER(x)
#define STRUCT(x) struct
#endif

CBUFFER_ALIGN STRUCT( cbuffer ) RenderCB REGISTER( b0 )
{
	float2	RcpTextureSize;
	float	ContrastThreshold;		// default = 0.2, lower is more expensive
	float	SubpixelRemoval;		// default = 0.75, lower blurs less

#if __cplusplus
	void* operator new(size_t i) { return _aligned_malloc( i, 16 ); };
	void operator delete(void* p) { _aligned_free( p ); };
#endif
};
#undef CBUFFER_ALIGN