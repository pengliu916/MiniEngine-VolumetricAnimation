#include "SparseVolume.inl"
RWTexture3D<int> tex_uavFlagVol : register(u1);

[numthreads(THREAD_X, THREAD_Y, THREAD_Z)]
void main(uint3 u3DTid : SV_DispatchThreadID)
{
    tex_uavFlagVol[u3DTid] = 0;
}