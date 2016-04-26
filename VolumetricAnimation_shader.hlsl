#include "D3DX_DXGIFormatConvert.inl"// this file provide utility funcs for format conversion
#include "VolumetricAnimation_SharedHeader.inl"

SamplerState samRaycast : register(s0);
StructuredBuffer<uint> g_bufVolumeSRV : register(t0);
RWStructuredBuffer<uint> g_bufVolumeUAV : register(u0);

static const float density = 0.02;

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct VSOutput {
	float4 ProjPos : SV_POSITION;
	float4 Pos : COLOR;
};

struct Ray
{
	float4 o;
	float4 d;
};

//--------------------------------------------------------------------------------------
// Utility Functions
//--------------------------------------------------------------------------------------
bool IntersectBox( Ray r, float3 boxmin, float3 boxmax, out float tnear, out float tfar )
{
	// compute intersection of ray with all six bbox planes
	float3 invR = 1.0 / r.d.xyz;
	float3 tbot = invR * (boxmin.xyz - r.o.xyz);
	float3 ttop = invR * (boxmax.xyz - r.o.xyz);

	// re-order intersections to find smallest and largest on each axis
	float3 tmin = min( ttop, tbot );
	float3 tmax = max( ttop, tbot );

	// find the largest tmin and the smallest tmax
	float2 t0 = max( tmin.xx, tmin.yz );
	tnear = max( t0.x, t0.y );
	t0 = min( tmax.xx, tmax.yz );
	tfar = min( t0.x, t0.y );

	return tnear <= tfar;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VSOutput vsmain( float4 pos : POSITION )
{
	VSOutput vsout = (VSOutput)0;
	pos.xyz *= voxelResolution;
	vsout.ProjPos = mul( wvp, pos );
	vsout.Pos = pos;
	return vsout;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 psmain( VSOutput input ) : SV_TARGET
{
	float4 output = float4 (0, 0, 0, 0);
	Ray eyeray;
	//world space
	eyeray.o = mul( invWorld, viewPos );
	eyeray.d = input.Pos - eyeray.o;
	eyeray.d = normalize( eyeray.d );
	eyeray.d.x = (eyeray.d.x == 0.f) ? 1e-15 : eyeray.d.x;
	eyeray.d.y = (eyeray.d.y == 0.f) ? 1e-15 : eyeray.d.y;
	eyeray.d.z = (eyeray.d.z == 0.f) ? 1e-15 : eyeray.d.z;

	// calculate ray intersection with bounding box
	float tnear, tfar;
	bool hit = IntersectBox( eyeray, boxMin, boxMax , tnear, tfar );
	if (!hit) return output;

	// calculate intersection points
	float3 Pnear = eyeray.o.xyz + eyeray.d.xyz * tnear;
	float3 Pfar = eyeray.o.xyz + eyeray.d.xyz * tfar;

	float3 P = Pnear;
	float t = tnear;
	float tSmallStep = VOLUME_SIZE_SCALE * 5;
	float3 P_pre = Pnear;
	float3 PsmallStep = eyeray.d.xyz * tSmallStep;

	float3 currentPixPos;
	while (t <= tfar) {
		uint3 idx = P / VOLUME_SIZE_SCALE + voxelResolution * 0.5 - 0.01f; // -0.01f to avoid incorrect float->int jump
		float4 value = D3DX_R8G8B8A8_UINT_to_UINT4( g_bufVolumeSRV[idx.x + idx.y*voxelResolution.x + idx.z*voxelResolution.y * voxelResolution.x] ) / 255.f;
		output += value * density;

		P += PsmallStep;
		t += tSmallStep;
	}
	return output;
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
[numthreads( THREAD_X, THREAD_Y, THREAD_Z )]
void csmain( uint3 DTid: SV_DispatchThreadID, uint Tid : SV_GroupIndex )
{
	uint4 col = D3DX_R8G8B8A8_UINT_to_UINT4( g_bufVolumeUAV[DTid.x + DTid.y*voxelResolution.x + DTid.z*voxelResolution.x*voxelResolution.y] );
	col.xyz -= shiftingColVals[col.w].xyz;

	uint3 delta = col.xyz - bgCol.xyz;
	if (dot( delta, delta ) < 0.8)
	{
		col.w = (col.w + 1) % COLOR_COUNT;
		col.xyz = (255 - bgCol.w) * shiftingColVals[col.w].xyz + bgCol.xyz;
	}
	g_bufVolumeUAV[DTid.x + DTid.y*voxelResolution.x + DTid.z*voxelResolution.x*voxelResolution.y] = D3DX_UINT4_to_R8G8B8A8_UINT( col );
}