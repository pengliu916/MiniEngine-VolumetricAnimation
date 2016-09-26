#include "SparseVolume.inl"
#include "SparseVolume.hlsli"
Texture3D<int> tex_srvFlagVol : register(t1);

void main(uint uInstanceID : SV_InstanceID, in float4 f4Pos : POSITION,
    out float4 f4ProjPos : SV_POSITION, out float4 f4CamPos : NORMAL0)
{
    uint3 u3Idx = makeU3Idx(uInstanceID,
        vParam.u3VoxelReso / vParam.uVoxelBrickRatio);
    f4ProjPos = float4(0.f, 0.f, 0.f, 1.f);
    f4CamPos = float4(0.f, 0.f, 0.f, 1.f);
    // check whether it is occupied 
    if (tex_srvFlagVol[u3Idx]) {
        float3 f3BrickOffset = 
            u3Idx * vParam.uVoxelBrickRatio * vParam.fVoxelSize -
            (vParam.u3VoxelReso >> 1) * vParam.fVoxelSize;
        f4Pos.xyz = (f4Pos.xyz + 0.5f) * vParam.fVoxelSize *
            vParam.uVoxelBrickRatio + f3BrickOffset;
        f4ProjPos = mul(mWorldViewProj, f4Pos);
        f4CamPos = mul(mView, f4Pos);
    }
}