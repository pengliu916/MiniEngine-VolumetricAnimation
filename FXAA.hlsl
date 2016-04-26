#include "FXAA_SharedHeader.inl"

#if Pass1
RWStructuredBuffer<uint> HWork : register(u0);
RWBuffer<float3> HColor : register(u1);
RWStructuredBuffer<uint> VWork : register(u2);
RWBuffer<float3> VColor : register(u3);
RWTexture2D<float> Luma : register(u4);
Texture2D<float3> Color : register(t0);
SamplerState LinearSampler : register(s0);

#define BOUNDARY_SIZE 1
#define ROW_WIDTH (8 + BOUNDARY_SIZE * 2)
groupshared float gs_LumaCache[ROW_WIDTH * ROW_WIDTH];

//--------------------------------------------------------------------------------------
// Utility Functions for Compute Shader
//--------------------------------------------------------------------------------------
float RGBToLogLuminance( float3 LinearRGB )
{
	float Luma = dot( LinearRGB, float3(0.212671, 0.715160, 0.072169) );
	return log2( 1 + Luma * 15 ) / 4;
}

//--------------------------------------------------------------------------------------
// Compute Shader
//--------------------------------------------------------------------------------------
[numthreads( 8, 8, 1 )]
void csmain( uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID )
{
	// Each thread read two pixels and store the luma to local data storage
	if (GI < ROW_WIDTH * ROW_WIDTH / 2)
	{
		uint LdsCoord = GI;
		int2 UavCoord = uint2(GI % ROW_WIDTH, GI / ROW_WIDTH) + Gid.xy * 8 - BOUNDARY_SIZE;
		float Luma1 = RGBToLogLuminance( Color[UavCoord] );
		Luma[UavCoord] = Luma1;
		gs_LumaCache[LdsCoord] = Luma1;

		LdsCoord += ROW_WIDTH * ROW_WIDTH / 2;
		UavCoord += int2(0, ROW_WIDTH / 2);
		float Luma2 = RGBToLogLuminance( Color[UavCoord] );
		Luma[UavCoord] = Luma2;
		gs_LumaCache[LdsCoord] = Luma2;
	}

	GroupMemoryBarrierWithGroupSync();

	uint CenterIdx = (GTid.x + BOUNDARY_SIZE) + (GTid.y + BOUNDARY_SIZE) * ROW_WIDTH;

	// Load the ordinal and center luminance
	float lumaN = gs_LumaCache[CenterIdx - ROW_WIDTH];
	float lumaW = gs_LumaCache[CenterIdx - 1];
	float lumaM = gs_LumaCache[CenterIdx];
	float lumaE = gs_LumaCache[CenterIdx + 1];
	float lumaS = gs_LumaCache[CenterIdx + ROW_WIDTH];

	// Contrast threshold test
	float rangeMax = max( max( lumaN, lumaW ), max( lumaE, max( lumaS, lumaM ) ) );
	float rangeMin = min( min( lumaN, lumaW ), min( lumaE, min( lumaS, lumaM ) ) );
	float range = rangeMax - rangeMin;
	if (range < ContrastThreshold)
		return;

	// Load the corner luminance
	float lumaNW = gs_LumaCache[CenterIdx - ROW_WIDTH - 1];
	float lumaNE = gs_LumaCache[CenterIdx - ROW_WIDTH + 1];
	float lumaSW = gs_LumaCache[CenterIdx + ROW_WIDTH - 1];
	float lumaSE = gs_LumaCache[CenterIdx + ROW_WIDTH + 1];

	// Pre-sum a few terms for better reuse
	float lumaNS = lumaN + lumaS;
	float lumaWE = lumaW + lumaE;
	float lumaNWSW = lumaNW + lumaSW;
	float lumaNESE = lumaNE + lumaSE;
	float lumaSWSE = lumaSW + lumaSE;
	float lumaNWNE = lumaNW = lumaNE;

	// Compute horizontal and vertical contrast
	float edgeHorz = abs( lumaNWSW - 2.0 * lumaW ) + abs( lumaNS - 2.0 * lumaM ) * 2.0 + abs( lumaNESE - 2.0 * lumaE );
	float edgeVert = abs( lumaSWSE - 2.0 * lumaS ) + abs( lumaWE - 2.0 * lumaM ) * 2.0 + abs( lumaNWNE - 2.0 * lumaN );

	// Also compute local contrast in the 3x3 region.  This can identify standalone pixels that alias.
	float avgNeighborLuma = ((lumaNS + lumaWE) * 2.0 + lumaNWSW + lumaNESE) / 12.0;
	float subpixelShift = saturate( pow( smoothstep( 0, 1, abs( avgNeighborLuma - lumaM ) / range ), 2 ) * SubpixelRemoval * 2 );

	float NegGrad = (edgeHorz >= edgeVert ? lumaN : lumaW) - lumaM;
	float PosGrad = (edgeHorz >= edgeVert ? lumaS : lumaE) - lumaM;
	uint GradientDir = abs( PosGrad ) >= abs( NegGrad ) ? 1 : 0;
	uint Subpix = uint(subpixelShift * 254.0) & 0xFE;
	uint PixelCoord = DTid.y << 20 | DTid.x << 8;
	//uint PixelCoord =( DTid.y%50) << 20 | (DTid.x%50) << 8;

	// Packet header: [ 12 bits Y | 12 bits X | 7 bits Subpix | 1 bit dir ]
	uint WorkHeader = PixelCoord | Subpix | GradientDir;

	if (edgeHorz >= edgeVert)
	{
		uint WorkIdx = HWork.IncrementCounter();
		HWork[WorkIdx] = WorkHeader;
		HColor[WorkIdx] = Color[DTid.xy + uint2(0, 2 * GradientDir - 1)];
	}
	else
	{
		uint WorkIdx = VWork.IncrementCounter();
		VWork[WorkIdx] = WorkHeader;
		VColor[WorkIdx] = Color[DTid.xy + uint2(2 * GradientDir - 1, 0)];
	}
}
#endif // Pass1

#if Color2Luma
RWTexture2D<float3> Color : register(u0);
Texture2D<float> Luma : register(t0);

[numthreads( 64, 1, 1 )]
void csmain( uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID )
{
	int2 UavCoord = uint2(GI % 8, GI / 8) + Gid.xy * 8;
	Color[UavCoord] = Luma[UavCoord];
}

#endif // Color2Luma

#if ResolveWork
ByteAddressBuffer WorkCounterH : register(t0);
ByteAddressBuffer WorkCounterV : register(t1);
RWByteAddressBuffer IndirectParams : register(u0);
RWStructuredBuffer<uint> WorkQueueH : register(u1);
RWStructuredBuffer<uint> WorkQueueV : register(u2);

[numthreads( 64, 1, 1 )]
void csmain( uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID )
{
	uint PixelCountH = WorkCounterH.Load( 0 );
	uint PixelCountV = WorkCounterV.Load( 0 );

	uint PaddedCountH = (PixelCountH + 63) & ~63;
	uint PaddedCountV = (PixelCountV + 63) & ~63;

	// Write out padding to the buffer
	if (GI + PixelCountH < PaddedCountH)
		WorkQueueH[PixelCountH + GI] = 0xffffffff;

	// Write out padding to the buffer
	if (GI + PixelCountV < PaddedCountV)
		WorkQueueV[PixelCountV + GI] = 0xffffffff;

	if (GI == 0)
	{
		IndirectParams.Store( 0, PaddedCountH >> 6 );
		IndirectParams.Store( 12, PaddedCountV >> 6 );
	}
}
#endif // ResolveWork

#if Pass2
Texture2D<float> Luma : register(t0);
Texture2D<float3> SrcColor : register(t1); // this must alias DstColor
StructuredBuffer<uint> WorkQueue : register(t2);
Buffer<float3> ColorQueue : register(t3);
RWTexture2D<float3> DstColor : register(u0);
SamplerState LinearSampler : register(s0);


// Note that the number of samples in each direction is one less than the number of sample distances.  The last
// is the maximum distance that should be used, but whether that sample is "good" or "bad" doesn't affect the result,
// so we don't need to load it.

#define NUM_SAMPLES 7
static const float s_SampleDistances[8] =	// FXAA_QUALITY__PRESET == 25
{
	1.0, 2.5, 4.5, 6.5, 8.5, 10.5, 14.5, 22.5
};

[numthreads( 64, 1, 1 )]
void csmain( uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID )
{
	uint WorkHeader = WorkQueue[DTid.x];
	uint2 ST = uint2(WorkHeader >> 8, WorkHeader >> 20) & 0xFFF;
	uint GradientDir = WorkHeader & 1; // Determines which side of the pixel has the highest contrast
	float Subpix = (WorkHeader & 0xFE) / 254.0 * 0.5;      // 7-bits to encode [0, 0.5]

#if VERTICAL_ORIENTATION
	float NextLuma = Luma[ST + int2(GradientDir * 2 - 1, 0)];
	float2 StartUV = (ST + float2(GradientDir, 0.5)) * RcpTextureSize;
#else
	float NextLuma = Luma[ST + int2(0, GradientDir * 2 - 1)];
	float2 StartUV = (ST + float2(0.5, GradientDir)) * RcpTextureSize;
#endif
	float ThisLuma = Luma[ST];
	float CenterLuma = (NextLuma + ThisLuma) * 0.5;         // Halfway between this and next; center of the contrasting edge
	float GradientSgn = sign( NextLuma - ThisLuma );          // Going down in brightness or up?
	float GradientMag = abs( NextLuma - ThisLuma ) * 0.25;    // How much contrast?  When can we stop looking?

	float NegDist = s_SampleDistances[NUM_SAMPLES];
	float PosDist = s_SampleDistances[NUM_SAMPLES];
	bool NegGood = false;
	bool PosGood = false;

	for (uint iter = 0; iter < NUM_SAMPLES; ++iter)
	{
		const float Distance = s_SampleDistances[iter];

#if VERTICAL_ORIENTATION
		float2 NegUV = StartUV - float2(0, RcpTextureSize.y) * Distance;
		float2 PosUV = StartUV + float2(0, RcpTextureSize.y) * Distance;
#else
		float2 NegUV = StartUV - float2(RcpTextureSize.x, 0) * Distance;
		float2 PosUV = StartUV + float2(RcpTextureSize.x, 0) * Distance;
#endif

		// Check for a negative endpoint
		float NegGrad = Luma.SampleLevel( LinearSampler, NegUV, 0 ) - CenterLuma;
		if (abs( NegGrad ) >= GradientMag && Distance < NegDist)
		{
			NegDist = Distance;
			NegGood = sign( NegGrad ) == GradientSgn;
		}

		// Check for a positive endpoint
		float PosGrad = Luma.SampleLevel( LinearSampler, PosUV, 0 ) - CenterLuma;
		if (abs( PosGrad ) >= GradientMag && Distance < PosDist)
		{
			PosDist = Distance;
			PosGood = sign( PosGrad ) == GradientSgn;
		}
	}

	// Ranges from 0.0 to 0.5
	float PixelShift = 0.5 - min( NegDist, PosDist ) / (PosDist + NegDist);
	bool GoodSpan = NegDist < PosDist ? NegGood : PosGood;
	PixelShift = max( Subpix, GoodSpan ? PixelShift : 0.0 );

	if (PixelShift > 0.01)
	{
#if DEBUG_OUTPUT
		DstColor[ST] = float3(2.0 * PixelShift, 1.0 - 2.0 * PixelShift, 0);
#else
		DstColor[ST] = lerp( SrcColor[ST], ColorQueue[DTid.x], PixelShift );
#endif
	}
#if DEBUG_OUTPUT
	else
		DstColor[ST] = float3(0, 0, 0.25);
#endif
}
#endif // Pass2