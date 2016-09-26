#include "SparseVolume.inl"
#include "SparseVolume.hlsli"

#if TYPED_UAV
RWBuffer<float4> tex_uavDataVol : register(u0);
#endif // TYPED_UAV
#if STRUCT_UAV
RWStructuredBuffer<float4> tex_uavDataVol : register(u0);
#endif // STRUCT_UAV
#if TEX3D_UAV
RWTexture3D<float4> tex_uavDataVol : register(u0);
#endif // TEX3D_UAV
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
// Compute Shader
//------------------------------------------------------------------------------
[numthreads(THREAD_X, THREAD_Y, THREAD_Z)]
void main(uint3 u3DTid: SV_DispatchThreadID)
{
    // Current voxel pos in local space
    float3 currentPos =
        (u3DTid - vParam.u3VoxelReso * 0.5f + 0.5f) * vParam.fVoxelSize;
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
    // Write back to voxel 
    tex_uavDataVol[BUFFER_INDEX(u3DTid)] = f4Field;
#if ENABLE_BRICKS
    // Update brick structure
    if (f4Field.x >= vParam.fMinDensity && f4Field.x <= vParam.fMaxDensity) {
        tex_uavFlagVol[u3DTid / vParam.uVoxelBrickRatio] = 1;
    }
#endif // ENABLE_BRICKS
}