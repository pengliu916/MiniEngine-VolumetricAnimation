#if TEX3D_UAV
#define BUFFER_INDEX(idx) idx
#else
#define BUFFER_INDEX(idx) flatIDX(idx)
#endif

uint flatIDX(uint3 idx)
{
    return idx.x + idx.y * u3VoxelReso.x +
        idx.z * u3VoxelReso.x * u3VoxelReso.y;
}
