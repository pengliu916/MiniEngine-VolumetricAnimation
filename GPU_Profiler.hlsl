struct RectAttr
{
	float2 TL;
	float2 BR;
	float4 Col;
};

StructuredBuffer<RectAttr> RectData : register(t0);

struct VSOutput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

VSOutput vsmain( uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID )
{
	VSOutput Output;
	uint RectIdx = VertexID % 4;
	float2 uv = float2((RectIdx >> 1) & 1, RectIdx & 1);
	float2 Corner = lerp( RectData[InstanceID].TL, RectData[InstanceID].BR, uv );
	Output.pos = float4(Corner, 1.f, 1);
	Output.col = RectData[InstanceID].Col;
	return Output;
}

float4 psmain( VSOutput input ) : SV_TARGET
{
	return input.col;
}