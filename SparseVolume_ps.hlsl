#include "SparseVolume.inl"
#include "SparseVolume.ihlsl"

#if TYPED_UAV
Buffer<float4> tex_srvDataVol : register(t0);
#endif // TYPED_UAV
#if STRUCT_UAV
StructuredBuffer<float4> tex_srvDataVol : register(t0);
#endif // STRUCT_UAV
#if TEX3D_UAV
Texture3D<float4> tex_srvDataVol : register(t0);
#endif // TEX3D_UAV
#if ENABLE_BRICKS
Texture3D<int> tex_srvFlagVol : register(t1);
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
    float3 f3uv = f3P * f3InvVolSize + 0.5f;
    f3uv.y = 1.f - f3uv.y;
    return f3uv;
}


/*void isoSurfaceShading( Ray eyeray, float2 f2NearFar, float isoValue,
   inout float4 f4OutColor, inout float fOutDepth )
{
    float3 f3Pnear = eyeray.o.xyz + eyeray.d.xyz * f2NearFar.x;
    float3 f3Pfar = eyeray.o.xyz + eyeray.d.xyz * f2NearFar.y;

    float3 f3P = f3Pnear;
    float t = f2NearFar.x;
    float fStep = 0.99f * voxelSize;
    float3 P_pre = f3Pnear;
    float3 PsmallStep = eyeray.d.xyz * fStep;

    float4 surfacePos;

    float field_pre;
    float field_now = 
            g_srvDataVolume.SampleLevel(sam_Linear, world2uv(f3P), 0).x;

    while (t <= f2NearFar.y) {
        float3 f3TexCoord = world2uv(f3P);
        float4 Field = g_srvDataVolume.SampleLevel(sam_Linear, f3TexCoord, 0);
      //float4 Field = g_srvDataVolume.Load(int4(f3TexCoord*voxelInfo.xyz,0));

        float density = Field.x;
        float4 color = float4(Field.yzw, 0);

        field_pre = field_now;
        field_now = density;

        if (field_now >= isoValue && field_pre < isoValue) {
            // For computing the depth
            surfacePos = float4(P_pre +
                (f3P - P_pre) * (isoValue - field_pre) / 
                (field_now - field_pre), 1.f);

            // For computing the normal

            float3 tCoord = world2uv(surfacePos.xyz);
            float depth_dx = g_srvDataVolume.SampleLevel(
                sam_Linear, tCoord + float3 (1, 0, 0) / voxelInfo.xyz, 0 ).x -
                g_srvDataVolume.SampleLevel( sam_Linear, 
                tCoord + float3 (-1, 0, 0) / voxelInfo.xyz, 0 ).x;
            float depth_dy = g_srvDataVolume.SampleLevel( sam_Linear, tCoord + 
                float3 (0, -1, 0) / voxelInfo.xyz, 0 ).x -
                g_srvDataVolume.SampleLevel( sam_Linear, 
                    tCoord + float3 (0, 1, 0) / voxelInfo.xyz, 0 ).x;
            float depth_dz = g_srvDataVolume.SampleLevel( sam_Linear, tCoord + 
                float3 (0, 0, 1) / voxelInfo.xyz, 0 ).x -
                g_srvDataVolume.SampleLevel( sam_Linear, tCoord + 
                    float3 (0, 0, -1) / voxelInfo.xyz, 0 ).x;

            float3 normal = -normalize( float3 (depth_dx, depth_dy, depth_dz) );


            // shading part
            float3 ambientLight = aLight_col * color;

            float3 directionalLight = dLight_col * color *
                clamp( dot( normal, dLight_dir ), 0, 1 );

            float3 vLight = cb_f4ViewPos.xyz - surfacePos.xyz;
            float3 halfVect = normalize( vLight - eyeray.d.xyz );
            float dist = length( vLight ); vLight /= dist;
            float angleAttn = clamp( dot( normal, vLight ), 0, 1 );
            float distAttn = 1.0f / (dist * dist);
            float specularAttn = 
                pow( clamp( dot( normal, halfVect ), 0, 1 ), 128 );

            float3 pointLight = 
                pLight_col * color * angleAttn + color * specularAttn;

            f4OutColor = 
                float4(ambientLight + directionalLight + pointLight, 1);
            surfacePos = mul( surfacePos, cb_mInvView );
            fOutDepth = surfacePos.z / 10.f;
            return;
            //return float4(normal*0.5+0.5,0);
        }
        P_pre = f3P;
        f3P += PsmallStep;
        t += fStep;
    }
    return;
}*/

float transferFunction(float fDensity)
{
    float fOpacity = (fDensity - f2MinMaxDensity.x) / 
        (f2MinMaxDensity.y - f2MinMaxDensity.x);
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
    return tex_srvDataVol.SampleLevel(samp_Linear, f3Idx / u3VoxelReso, 0);
#elif TEX3D_UAV && FILTER_READ == 3
    return tex_srvDataVol.SampleLevel(samp_Aniso, f3Idx / u3VoxelReso, 0);
#else
    return tex_srvDataVol[BUFFER_INDEX(u3Idx000)];
#endif // !FILTER_READ
}

void accumulatedShading(Ray eyeray, float2 f2NearFar, float2 f2MinMaxDen,
#if DRAW_BRICKGRID
    inout float4 f4OutColor, inout float fOutDepth)
{
    bool bFirstEnter = true;
#else
    inout float4 f4OutColor)
{
#endif // DRAW_BRICKGRID
    float3 f3Pnear = eyeray.f4o.xyz + eyeray.f4d.xyz * f2NearFar.x;
    float3 f3Pfar = eyeray.f4o.xyz + eyeray.f4d.xyz * f2NearFar.y;

    float3 f3P = f3Pnear;
    float t = f2NearFar.x;
    float fStep = 0.8f * fVoxelSize;
    float3 f3PsmallStep = eyeray.f4d.xyz * fStep;

    float4 f4AccuData = 0;
    while (t <= f2NearFar.y) {
        float4 f4CurData = float4(0.001f, 0.001f, 0.001f, 0.005f);
#if DRAW_BRICKGRID
        if (bFirstEnter) {
            float4 fPos = mul(float4(f3P, 1.f), mView);
            fOutDepth = fPos.z / 10.f;
            bFirstEnter = false;
        }
        uint3 idx = f3TexCoord * u3VoxelReso;
#endif // DRAW_BRICKGRID
        float4 f4Field = readVolume(f3P / fVoxelSize + u3VoxelReso * 0.5f);
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
float4 ps_raycast_main( float4 f4Pos : POSITION, 
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
    bool bHit = IntersectBox(eyeray, f3BoxMin, f3BoxMax , fTnear, fTfar);
    if (!bHit) {
        discard;
    }
    if (fTnear <= 0) {
        fTnear = 0;
    }
    float4 f4Col = float4(1.f, 1.f, 1.f, 0.f) * 0.01f;
    float fDepth = 1000.f;
    
    //isoSurfaceShading(eyeray, float2(tnear,tfar),invVolSize.w, col,depth);
    accumulatedShading( eyeray, float2(fTnear,fTfar),f2MinMaxDensity, f4Col);
    
    return f4Col + 0.1f;
}