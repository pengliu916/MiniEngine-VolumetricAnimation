#include "SparseVolume.inl"
#include "SparseVolume.hlsli"

void main(inout float4 f4Pos : POSITION,
    out float4 f4ProjPos : SV_POSITION)
{
    f4Pos.xyz *= (vParam.u3VoxelReso * vParam.fVoxelSize);
    f4ProjPos = mul(mWorldViewProj, f4Pos);
}