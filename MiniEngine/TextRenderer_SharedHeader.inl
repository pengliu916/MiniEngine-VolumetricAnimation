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
typedef DirectX::XMFLOAT2 float2;
typedef DirectX::XMFLOAT4 float4;
typedef uint32_t uint;
#define REGISTER(x)
#define STRUCT(x) struct
#endif

#if __cplusplus || ( __hlsl && Vertex_Shader )
CBUFFER_ALIGN STRUCT( cbuffer ) VertexShaderParams REGISTER( b0 )
{
	float2      Scale;			// Scale and offset for transforming coordinates
	float2      Offset;
	float2      InvTexDim;		// Normalizes texture coordinates
	float       TextHeight;		// Height of text in destination pixels
	float       TextScale;		// TextSize / FontHeight
	float       DstBorder;		// Extra space around a glyph measured in screen space coordinates
	uint        SrcBorder;		// Extra spacing around glyphs to avoid sampling neighboring glyphs
#if __cplusplus
	void * operator new(size_t i)
	{
		return _aligned_malloc( i, 16 );
	};
	void operator delete(void* p)
	{
		_aligned_free( p );
	};
#endif // __cplusplus
};
#endif // __cplusplus || (__hlsl && Vertex_Shader)

#if __cplusplus || ( __hlsl && Pixel_Shader )
CBUFFER_ALIGN STRUCT( cbuffer ) PixelShaderParams REGISTER( b0 )
{
	float4      Color;
	float2      ShadowOffset;
	float       ShadowHardness;
	float       ShadowOpacity;
	float       HeightRange;	// The range of the signed distance field.
#if __cplusplus
	void * operator new(size_t i)
	{
		return _aligned_malloc( i, 16 );
	};
	void operator delete(void* p)
	{
		_aligned_free( p );
	};
#endif // __cplusplus
};
#endif // __cplusplus || ( __hlsl && Pixel_Shader )
#undef CBUFFER_ALIGN