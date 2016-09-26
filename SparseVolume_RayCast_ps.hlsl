#include "SparseVolume.inl"
#include "SparseVolume.hlsli"

#if TYPED_UAV
Buffer<float4> tex_srvDataVol : register(t0);
#elif STRUCT_UAV
StructuredBuffer<float4> tex_srvDataVol : register(t0);
#else // TEX3D_UAV
Texture3D<float4> tex_srvDataVol : register(t0);
#endif
#if ENABLE_BRICKS
Texture2D<float2> tex_srvNearFar : register(t1);
//Texture3D<int> tex_srvFlagVol : register(t1);
#endif // ENABLE_BRICKS
SamplerState samp_Linear : register(s0);
SamplerState samp_Aniso : register(s1);


//------------------------------------------------------------------------------
// Structures
//------------------------------------------------------------------------------
struct Ray
{
    float4 f4o;
    float4 f4d;
};

//------------------------------------------------------------------------------
// Utility Funcs
//------------------------------------------------------------------------------
bool IntersectBox(Ray r, float3 boxmin, float3 boxmax,
    out float tnear, out float tfar)
{
    // compute intersection of ray with all six bbox planes
    float3 invR = 1.0 / r.f4d.xyz;
    float3 tbot = invR * (boxmin.xyz - r.f4o.xyz);
    float3 ttop = invR * (boxmax.xyz - r.f4o.xyz);

    // re-order intersections to find smallest and largest on each axis
    float3 tmin = min(ttop, tbot);
    float3 tmax = max(ttop, tbot);

    // find the largest tmin and the smallest tmax
    float2 t0 = max(tmin.xx, tmin.yz);
    tnear = max(t0.x, t0.y);
    t0 = min(tmax.xx, tmax.yz);
    tfar = min(t0.x, t0.y);

    return tnear <= tfar;
}

float3 world2uv(float3 f3P)
{
    float3 f3uv = f3P * vParam.f3InvVolSize + 0.5f;
    f3uv.y = 1.f - f3uv.y;
    return f3uv;
}

float transferFunction(float fDensity)
{
    float fOpacity = (fDensity - vParam.fMinDensity) /
        (vParam.fMaxDensity - vParam.fMinDensity);
    float fp2 = fOpacity * fOpacity + 0.02f;
    float fp4 = fp2 * fp2;
    return fp4 * 0.3f + fp2 * 0.1f + fOpacity * 0.15f;
}

float4 readVolume(float3 f3Idx)
{
    uint3 u3Idx000 = f3Idx;
#if FILTER_READ == 1
    float3 f3d = f3Idx - u3Idx000;
    float4 f4V000 = tex_srvDataVol[BUFFER_INDEX(u3Idx000)];
    float4 f4V001 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(0, 0, 1))];
    float4 f4V010 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(0, 1, 0))];
    float4 f4V011 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(0, 1, 1))];
    float4 f4V100 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(1, 0, 0))];
    float4 f4V101 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(1, 0, 1))];
    float4 f4V110 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(1, 1, 0))];
    float4 f4V111 = tex_srvDataVol[BUFFER_INDEX(u3Idx000 + uint3(1, 1, 1))];
    return f4V000 * (1.f - f3d.x) * (1.f - f3d.y) * (1.f - f3d.z) +
        f4V100 * f3d.x * (1.f - f3d.y) * (1.f - f3d.z) +
        f4V010 * (1.f - f3d.x) * f3d.y * (1.f - f3d.z) +
        f4V001 * (1.f - f3d.x) * (1.f - f3d.y) * f3d.z +
        f4V101 * f3d.x * (1.f - f3d.y) * f3d.z +
        f4V011 * (1.f - f3d.x) * f3d.y * f3d.z +
        f4V110 * f3d.x * f3d.y * (1.f - f3d.z) +
        f4V111 * f3d.x * f3d.y * f3d.z;
#elif TEX3D_UAV && FILTER_READ == 2
    return tex_srvDataVol.SampleLevel(
        samp_Linear, f3Idx / vParam.u3VoxelReso, 0);
#elif TEX3D_UAV && FILTER_READ == 3
    return tex_srvDataVol.SampleLevel(
        samp_Aniso, f3Idx / vParam.u3VoxelReso, 0);
#else
    return tex_srvDataVol[BUFFER_INDEX(u3Idx000)];
#endif // !FILTER_READ
}

void accumulatedShading(Ray eyeray, float2 f2NearFar, float2 f2MinMaxDen,
    inout float4 f4OutColor)
{
    float3 f3Pnear = eyeray.f4o.xyz + eyeray.f4d.xyz * f2NearFar.x;
    float3 f3Pfar = eyeray.f4o.xyz + eyeray.f4d.xyz * f2NearFar.y;

    float3 f3P = f3Pnear;
    float t = f2NearFar.x;
    float fStep = 0.8f * vParam.fVoxelSize;
    float3 f3PsmallStep = eyeray.f4d.xyz * fStep;

    float4 f4AccuData = 0;
    while (t <= f2NearFar.y) {
        float4 f4CurData = float4(0.001f, 0.001f, 0.001f, 0.005f);
        float4 f4Field =
            readVolume(f3P / vParam.fVoxelSize + vParam.u3VoxelReso * 0.5f);
        if (f4Field.x >= f2MinMaxDen.x && f4Field.x <= f2MinMaxDen.y) {
            f4CurData = float4(f4Field.yzw, transferFunction(f4Field.x));
            f4CurData.a *= 0.25f;
            f4CurData.rgb *= f4CurData.a;
            f4AccuData = (1.0f - f4AccuData.a) * f4CurData + f4AccuData;
        }
        if (f4AccuData.a >= 0.95f) {
            break;
        }
        f3P += f3PsmallStep;
        t += fStep;
    }
    f4OutColor = f4AccuData * f4AccuData.a;
    return;
}

//------------------------------------------------------------------------------
// Pixel Shader
//------------------------------------------------------------------------------
float4 main( float4 f4Pos : POSITION, 
    float4 f4ProjPos : SV_POSITION ) : SV_Target
{
    Ray eyeray;
    //world space
    eyeray.f4o = f4ViewPos;
    eyeray.f4d = f4Pos - eyeray.f4o;
    eyeray.f4d = normalize( eyeray.f4d );
    eyeray.f4d.x = (eyeray.f4d.x == 0.f) ? 1e-15 : eyeray.f4d.x;
    eyeray.f4d.y = (eyeray.f4d.y == 0.f) ? 1e-15 : eyeray.f4d.y;
    eyeray.f4d.z = (eyeray.f4d.z == 0.f) ? 1e-15 : eyeray.f4d.z;

    // calculate ray intersection with bounding box
    float fTnear, fTfar; 
#if ENABLE_BRICKS
    int2 uv = f4ProjPos.xy;
    float2 f2NearFar = tex_srvNearFar.Load(int3(uv, 0)).xy / length(eyeray.f4d.xyz);
    fTnear = f2NearFar.x;
    fTfar = -f2NearFar.y;
    bool bHit = (fTfar - fTnear) > 0;
#else
    bool bHit = 
        IntersectBox(eyeray, vParam.f3BoxMin, vParam.f3BoxMax , fTnear, fTfar);
#endif // ENABLE_BRICKS
    if (!bHit) {
        discard;
    }
    if (fTnear <= 0) {
        fTnear = 0;
    }
    float4 f4Col = float4(1.f, 1.f, 1.f, 0.f) * 0.01f;
    float fDepth = 1000.f;

    accumulatedShading( eyeray, float2(fTnear,fTfar),
        float2(vParam.fMinDensity, vParam.fMaxDensity), f4Col);
    return f4Col;
}