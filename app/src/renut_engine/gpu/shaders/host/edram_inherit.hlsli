// blur-nx (2026-11-11, blur_renut_edram_inherit, A/B 220): one EDRAM view's samples written into another view of the same
// tiles, through the console's layout (draw.cpp, InheritEdramBeforeDrawLocked). On the console all views of the EDRAM
// are one memory: a view drawn with a write mask that leaves channels out (or blending) keeps what any view wrote there
// before. Here each view is its own host target, so before such a draw the target takes the tiles other views wrote
// since its own last write.
//
// The EDRAM is 2048 tiles of 80x16 32-bit samples (40x16 for the 64-bit formats). A surface of base tile B whose rows
// are C tiles across holds sample (X, Y) in tile (B + (Y / 16) * C + X / W) mod 2048, at (X mod W, Y mod 16) in it
// (W: the tile's width in samples). A pixel's samples: 1x (x, y); 2x (x, 2y + s), its two samples one above the other;
// 4x (2x + (s & 1), 2y + (s >> 1)). For each destination sample this finds the tile and place, the source view's sample
// at the same place, and copies it; one outside the source's tiles or its size keeps the destination's own (discard).
// Both views have the same EDRAM format and tile width (the caller checks), so a sample's word is the same value.
//
//   g_Push.scale.x (bits): the destination's span: base (bits 0-11), tiles across (12-23), 2 samples across (24),
//                          2 samples down (25), 40-sample tiles (26)
//   g_Push.scale.y (bits): the source's span, the same fields
//   g_Push.translate (bits): the source's width and height in pixels
//   g_Push.param0 (bits): the source's rows of tiles
//   g_Push.textureIndex: the source's slot (EDRAM_INHERIT_SRC_MS: in the multisampled table)
// present_vs draws the fullscreen triangle; EDRAM_INHERIT_DST_MS runs per sample (SV_SampleIndex).
#if EDRAM_INHERIT_SRC_MS
#define UTILITY_OWN_TEXTURE_TABLE
#endif
#include "utility.hlsli"
#if EDRAM_INHERIT_SRC_MS
Texture2DMS<float4> g_SourceMS[] : register(t0, space0);
#endif

struct EdramSpanFields
{
    uint base;
    uint columns;
    uint samples_x;
    uint samples_y;
    uint tile_width;
};

EdramSpanFields UnpackSpan(uint bits)
{
    EdramSpanFields s;
    s.base = bits & 0xFFFu;
    s.columns = (bits >> 12) & 0xFFFu;
    s.samples_x = ((bits >> 24) & 1u) + 1u;
    s.samples_y = ((bits >> 25) & 1u) + 1u;
    s.tile_width = ((bits >> 26) & 1u) != 0u ? 40u : 80u;
    return s;
}

#if EDRAM_INHERIT_DST_MS
float4 main(float4 position : SV_Position, uint sample_index : SV_SampleIndex) : SV_Target0
#else
float4 main(float4 position : SV_Position) : SV_Target0
#endif
{
#if !EDRAM_INHERIT_DST_MS
    const uint sample_index = 0u;
#endif
    const EdramSpanFields dst = UnpackSpan(asuint(g_Push.scale.x));
    const EdramSpanFields src = UnpackSpan(asuint(g_Push.scale.y));
    const uint2 pixel = uint2(position.xy);
    // The destination sample's place in its own sample grid.
    uint2 sample_xy = pixel * uint2(dst.samples_x, dst.samples_y);
    if (dst.samples_x == 2u)
        sample_xy += uint2(sample_index & 1u, sample_index >> 1);
    else if (dst.samples_y == 2u)
        sample_xy.y += sample_index & 1u;
    const uint w = dst.tile_width;
    const uint tile = (dst.base + (sample_xy.y / 16u) * dst.columns + sample_xy.x / w) & 2047u;
    const uint2 in_tile = uint2(sample_xy.x % w, sample_xy.y % 16u);
    // The same tile and place in the source view.
    const uint rel = (tile - src.base) & 2047u;
    const uint src_rows = asuint(g_Push.param0);
    if (src.columns == 0u || rel >= src.columns * src_rows)
        discard;
    const uint2 src_sample = uint2((rel % src.columns) * w + in_tile.x, (rel / src.columns) * 16u + in_tile.y);
    const uint2 src_pixel = src_sample / uint2(src.samples_x, src.samples_y);
    const uint2 src_size = uint2(asuint(g_Push.translate.x), asuint(g_Push.translate.y));
    if (src_pixel.x >= src_size.x || src_pixel.y >= src_size.y)
        discard;
#if EDRAM_INHERIT_SRC_MS
    uint s = 0u;
    if (src.samples_x == 2u)
        s = (src_sample.x & 1u) | ((src_sample.y & 1u) << 1);
    else if (src.samples_y == 2u)
        s = src_sample.y & 1u;
    return g_SourceMS[NonUniformResourceIndex(g_Push.textureIndex)].Load(int2(src_pixel), int(s));
#else
    return g_Textures[NonUniformResourceIndex(g_Push.textureIndex)].Load(int3(int2(src_pixel), 0));
#endif
}
