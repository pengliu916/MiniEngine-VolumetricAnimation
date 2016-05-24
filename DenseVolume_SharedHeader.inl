
// Feature flag

// Set this to 1 will lose perf due to nvidia hw specific architecture
// See the following link for more detail
// http://www.gamedev.net/topic/673103-using-small-static-arrays-in-shader-cause-heavily-perf-drop/
//
#define STATIC_ARRAY 0

// Toggle between using octahedron animation or sphere animation
#define SPHERE_VOLUME_ANIMATION 0


// Params
#define THREAD_X 8
#define THREAD_Y 8
#define THREAD_Z 8

#define VOLUME_SIZE_SCALE 0.01f
#define COLOR_COUNT 7

// Do not modify below this line
#if __cplusplus
#define CBUFFER_ALIGN __declspec(align(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
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
#define REGISTER(x)
#define STRUCT(x) struct
#endif

#if __cplusplus || (__hlsl && STATIC_ARRAY)
static const int4 shiftingColVals[] =
{
	int4( 1, 0, 0, 0 ),
	int4( 0, 1, 0, 1 ),
	int4( 0, 0, 1, 2 ),
	int4( 1, 1, 0, 3 ),
	int4( 1, 0, 1, 4 ),
	int4( 0, 1, 1, 5 ),
	int4( 1, 1, 1, 6 ),
};
#endif

CBUFFER_ALIGN STRUCT( cbuffer ) DataCB REGISTER( b0 )
{
	matrix wvp;
	float4 viewPos;
	int4 bgCol;
	int3 voxelResolution;
	int dummy0;
	float3 boxMin;
	int dummy1;
	float3 boxMax;
	int dummy2;

#if !STATIC_ARRAY
	int4 shiftingColVals[COLOR_COUNT];
#endif // !STATIC_ARRAY
#if __cplusplus
	void * operator new(size_t i)
	{
		return _aligned_malloc( i, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT );
	};
	void operator delete(void* p)
	{
		_aligned_free( p );
	};
#endif
};
#undef CBUFFER_ALIGN