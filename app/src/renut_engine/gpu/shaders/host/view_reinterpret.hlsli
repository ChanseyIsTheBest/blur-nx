// blur-nx (2026-11-10): one EDRAM view read through another, the 32-bit pair Blur uses (draw.cpp, SyncColorViewLocked):
// its HDR scene is drawn to a 2_10_10_10_FLOAT surface (EDRAM format 0x3F) and the same EDRAM resolved through an
// 8_8_8_8 surface (0x06), and the other way round. On the console both are the same 32-bit words: R in bits 0-9, G in
// 10-19, B in 20-29 and A in 30-31 for the float view (each colour a 7e3 float, A 2-bit unorm), the four bytes R, G, B,
// A from bit 0 for the 8888 view. Here each view is its own host target (the float one RGBA16F, the 8888 one RGBA8), so
// the words are rebuilt from the view drawn last and read the way the resolving view reads them. halo3-nx's
// view_fp10_encode_ps does the same for the 2_10_10_10 unorm view.
//   g_Push.param1 = 0: the source is the float view; param0 = 2^bias of its colour exponent (the host target holds the
//                      shader's output, without the bias): the output is the word's four bytes, for the 8888 target;
//   g_Push.param1 = 1: the source is the 8888 view; param0 = 2^bias of the float surface being read: the output is the
//                      word's 7e3 floats (divided by that bias) and its 2-bit alpha, for the float target.
// The pixel plus g_Push.translate is the source texel; present_vs draws the fullscreen triangle.
// VIEW_REINTERPRET_MS = 1 (view_reinterpret_ms_ps.hlsl): both views multisampled (Blur's scene is 4x): the shader runs
// per sample (SV_SampleIndex) and reads the same sample of the source, each sample's word being its own.
#if VIEW_REINTERPRET_MS
#define UTILITY_OWN_TEXTURE_TABLE
#endif
#include "utility.hlsli"
#if VIEW_REINTERPRET_MS
Texture2DMS<float4> g_SourceMS[] : register(t0, space0);
#endif

// The 10 bits of a 7e3 float: 3 bits of exponent, 7 of mantissa; 0 to 31.875, denormal below 0.25 (halo3-nx's).
uint Encode7e3(float value)
{
    const float v = clamp(value, 0.0, 31.875);
    if (!(v >= 0.25))
        return uint(floor(v * 512.0 + 0.5));
    const float exponent = floor(log2(v));
    return uint((exponent + 3.0) * 128.0 + floor((v / pow(2.0, exponent) - 1.0) * 128.0 + 0.5));
}

float Decode7e3(uint bits)
{
    const uint e = (bits >> 7) & 7u;
    const uint m = bits & 0x7Fu;
    if (e == 0u)
        return float(m) / 512.0;
    return exp2(float(e) - 3.0) * (1.0 + float(m) / 128.0);
}

#if VIEW_REINTERPRET_MS
float4 main(float4 position : SV_Position, uint sample_index : SV_SampleIndex) : SV_Target0
{
    const int2 texel = int2(position.xy) + int2(g_Push.translate);
    const float4 c = g_SourceMS[NonUniformResourceIndex(g_Push.textureIndex)].Load(texel, sample_index);
#else
float4 main(float4 position : SV_Position) : SV_Target0
{
    const int2 texel = int2(position.xy) + int2(g_Push.translate);
    const float4 c = g_Textures[NonUniformResourceIndex(g_Push.textureIndex)].Load(int3(texel, 0));
#endif
    if (g_Push.param1 < 0.5)
    {
        const uint a = uint(floor(saturate(c.a) * 3.0 + 0.5));
        const uint word = Encode7e3(c.r * g_Push.param0) | (Encode7e3(c.g * g_Push.param0) << 10) |
                          (Encode7e3(c.b * g_Push.param0) << 20) | (a << 30);
        return float4(word & 0xFFu, (word >> 8) & 0xFFu, (word >> 16) & 0xFFu, (word >> 24) & 0xFFu) / 255.0;
    }
    const uint4 b = uint4(floor(saturate(c) * 255.0 + 0.5));
    const uint word = b.r | (b.g << 8) | (b.b << 16) | (b.a << 24);
    const float scale = 1.0 / g_Push.param0;
    return float4(Decode7e3(word & 0x3FFu) * scale, Decode7e3((word >> 10) & 0x3FFu) * scale,
                  Decode7e3((word >> 20) & 0x3FFu) * scale, float(word >> 30) / 3.0);
}
