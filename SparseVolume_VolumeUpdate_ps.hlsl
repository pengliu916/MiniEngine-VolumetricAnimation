#include "SparseVolume.inl"
#include "SparseVolume.hlsli"

#if TYPED_UAV
RWBuffer<float4> tex_uavDataVol : register(u0);
#endif // TYPED_UAV
#if STRUCT_UAV
RWStructuredBuffer<float4> tex_uavDataVol : register(u0);
#endif // STRUCT_UAV
#if ENABLE_BRICKS
RWTexture3D<int> tex_uavFlagVol : register(u1);
#endif // ENABLE_BRICKS

//------------------------------------------------------------------------------
// Utility Funcs
//------------------------------------------------------------------------------
float Ball(float3 f3Pos, float3 f3Center, float fRadiusSq)
{
    float3 f3d = f3Pos - f3Center;
    float fDistSq = dot(f3d, f3d);
    float fInvDistSq = 1.f / fDistSq;
    return fRadiusSq * fInvDistSq;
}

//------------------------------------------------------------------------------
// Pixel Shader
//------------------------------------------------------------------------------
float4 main(float4 f4Pos : SV_Position,
    uint uSlice : SV_RenderTargetArrayIndex,
    float3 f3Location : POSITION0)
    : SV_Target0
{
    // Current voxel pos in local space
    float3 currentPos = f3Location * vParam.fVoxelSize;
    uint3 u3DTid = f3Location + vParam.u3VoxelReso * 0.5f - 0.5f;
    // Voxel content: x-density, yzw-color
    float4 f4Field = float4(0.f, 1.f, 1.f, 1.f);
    // Update voxel based on its position
    for (uint i = 0; i < uNumOfBalls; i++) {
        float fDensity = Ball(currentPos, f4Balls[i].xyz, f4Balls[i].w);
        f4Field.x += fDensity;
        f4Field.yzw += f4BallsCol[i].xyz * pow(fDensity, 3) * 1000.f;
    }
    // Make color vivid
    f4Field.yzw = normalize(f4Field.yzw);
#if ENABLE_BRICKS
    // Update brick structure
    if (f4Field.x >= vParam.fMinDensity && f4Field.x <= vParam.fMaxDensity) {
        tex_uavFlagVol[u3DTid / vParam.uVoxelBrickRatio] = 1;
    }
#endif // ENABLE_BRICKS
#if !TEX3D_UAV
    // Write back to voxel 
    tex_uavDataVol[BUFFER_INDEX(u3DTid)] = f4Field;
    discard;
#endif // !TEX3D_UAV
    return f4Field;
}