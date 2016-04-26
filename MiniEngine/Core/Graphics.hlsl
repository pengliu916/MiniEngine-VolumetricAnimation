#if QuadVS
void vsmain( in uint VertID : SV_VertexID, out float4 Pos : SV_Position, out float2 Tex : TexCoord0 )
{
	// Texture coordinates range [0, 2], but only [0, 1] appears on screen.
	Tex = float2(uint2(VertID, VertID << 1) & 2);
	Pos = float4(lerp( float2(-1, 1), float2(1, -1), Tex ), 0, 1);
}
#endif

#if CopyPS
Texture2D<float4>ColorTex : register(t0);

float4 psmain(float4 position : SV_Position) : SV_Target0
{
	return ColorTex[(int2)position.xy];
}
#endif

#if CopyLumaPS
Texture2D<float>LumaTex : register(t0);

float4 psmain( float4 position : SV_Position ) : SV_Target0
{
	return LumaTex[(int2)position.xy].xxxx;
}
#endif