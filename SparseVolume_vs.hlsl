#include "SparseVolume.inl"

void vs_cube_main(inout float4 f4Pos : POSITION,
    out float4 f4ProjPos : SV_POSITION)
{
    f4Pos.xyz *= (u3VoxelReso * fVoxelSize);
    f4ProjPos = mul(mWorldViewProj, f4Pos);
}