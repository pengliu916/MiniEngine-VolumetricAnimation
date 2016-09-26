// PS for rendering the stepinfo Texture for raymarching
// [Warning] keep this PS short since during alpha blending, each pixel will be
// shaded multiple times
float2 main(float4 f4ProjPos : SV_POSITION,
    float4 f4CamPos : NORMAL0) : SV_Target
{
    // Alpha blend will store the smallest RG channel, so to keep track of both
    // min/max length, G channel will need to be multiplied by -1
#if DEBUG_VIEW
    return float2(1.f, 0.f);
#else
    float vecLength = length(f4CamPos.xyz);
    return float2(vecLength, -vecLength);
#endif // !DEBUG_VIEW
}