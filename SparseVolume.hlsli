#if TEX3D_UAV
#define BUFFER_INDEX(idx) idx
#else
#define BUFFER_INDEX(idx) flatIDX(idx)
#endif

uint flatIDX(uint3 idx)
{
    return idx.x + idx.y * vParam.u3VoxelReso.x +
        idx.z * vParam.u3VoxelReso.x * vParam.u3VoxelReso.y;
}

uint3 makeU3Idx(uint idx, uint3 res)
{
    uint stripCount = res.x * res.y;
    uint stripRemainder = idx % stripCount;
    uint z = idx / stripCount;
    uint y = stripRemainder / res.x;
    uint x = stripRemainder % res.x;
    return uint3(x, y, z);
}