#include "SparseVolume.inl"

struct PS_INPUT {
    float4 f4Pos : SV_Position;
    uint uSlice : SV_RenderTargetArrayIndex;
    float3 f3Location : POSITION0;
};

[maxvertexcount(3)]
void main(point float vertex[1] : POSITION1, uint uPrimID : SV_PrimitiveID,
    inout TriangleStream<PS_INPUT> triStream)
{
    PS_INPUT output;
    output.f4Pos = float4(-1.f, -1.f, 0.f, 1.f);
    output.uSlice = uPrimID;
    output.f3Location = float3(float2(-0.5f, 0.5f) * vParam.u3VoxelReso.xy,
        uPrimID - vParam.u3VoxelReso.z * 0.5f + 0.5f);
    triStream.Append(output);
    output.f4Pos = float4(3.f, -1.f, 0.f, 1.f);
    output.uSlice = uPrimID;
    output.f3Location = float3(float2(1.5f, 0.5f) * vParam.u3VoxelReso.xy,
        uPrimID - vParam.u3VoxelReso.z * 0.5f + 0.5f);
    triStream.Append(output);
    output.f4Pos = float4(-1.f, 3.f, 0.f, 1.f);
    output.uSlice = uPrimID;
    output.f3Location = float3(float2(-0.5f, -1.5f) * vParam.u3VoxelReso.xy,
        uPrimID - vParam.u3VoxelReso.z * 0.5f + 0.5f);
    triStream.Append(output);
}