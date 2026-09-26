#pragma once

// E08-S01: "the on-screen display is legible on the target machine."
//
// `DKR_OSD=1` draws four lines in the top-left corner, refreshed once a second:
//
//     27.3 FPS
//     FRM 36.6 MAX 67.0     the frame period, mean and worst, in ms
//     GFX 12.0 MAX 19.2     one display list's render
//     SND  7.6 MAX 12.9     one audio task
//
// Worst rather than a percentile: a second holds about thirty frames, too few
// for a 99th, and the worst is what a stutter is.
//
// The text is a 3x5 bitmap font drawn as flat rectangles through the backend's
// own `draw_triangles`, with the state the canary uses. No texture: the TMU
// cache and its memos are the game's, and an overlay has no business in them.
// Each glyph is cut into as few rectangles as it takes (greedy, right then
// down), and the layout is rebuilt only when the text changes, once a second.

#include "render/backend.h"

#include <cstring>

namespace dkr::runtime {

class FrameOsd {
public:
    static constexpr int kLines = 4;
    static constexpr int kColumns = 20;

    // Replaces the text and rebuilds the vertices. Characters without a glyph
    // are drawn as blanks.
    void set_text(const char (&lines)[kLines][kColumns + 1]) {
        used_ = 0;
        int widest = 0;
        for (int l = 0; l < kLines; l++) {
            const int n = static_cast<int>(std::strlen(lines[l]));
            if (n > widest) { widest = n; }
        }
        // The background first, so that the text lands on it.
        add_rect(kOrigin - kPad, kOrigin - kPad,
                 kOrigin + widest * kAdvance + kPad - kScale,
                 kOrigin + kLines * kLineHeight + kPad - kScale, 0.0F);
        for (int l = 0; l < kLines; l++) {
            for (int i = 0; lines[l][i] != '\0' && i < kColumns; i++) {
                add_glyph(glyph(lines[l][i]), kOrigin + i * kAdvance,
                          kOrigin + l * kLineHeight);
            }
        }
    }

    void draw(const dkr_render_backend& backend) const {
        if (used_ == 0) { return; }
        dkr_render_state st;
        std::memset(&st, 0, sizeof(st));
        st.combine = DKR_COMBINE_SHADE;
        st.blend = DKR_BLEND_OPAQUE;
        st.depth = DKR_DEPTH_DISABLED;
        st.cull = DKR_CULL_NONE;
        backend.set_state(backend.self, &st);
        backend.draw_triangles(backend.self, vertices_, used_ / 3);
    }

    int triangles() const { return used_ / 3; }

private:
    static constexpr int kScale = 3;                 // screen pixels a font pixel
    static constexpr int kAdvance = 4 * kScale;
    static constexpr int kLineHeight = 6 * kScale;
    static constexpr int kOrigin = 12;
    static constexpr int kPad = 6;
    static constexpr int kMaxVertices = 6 * 320;

    // Five rows of three bits, the leftmost pixel in bit 2.
    static unsigned short glyph(char c) {
        struct Entry { char c; unsigned short rows; };
        #define DKR_OSD_G(a, b, c2, d, e) \
            static_cast<unsigned short>((a << 12) | (b << 9) | (c2 << 6) | (d << 3) | e)
        static const Entry font[] = {
            {'0', DKR_OSD_G(7, 5, 5, 5, 7)}, {'1', DKR_OSD_G(2, 6, 2, 2, 7)},
            {'2', DKR_OSD_G(7, 1, 7, 4, 7)}, {'3', DKR_OSD_G(7, 1, 7, 1, 7)},
            {'4', DKR_OSD_G(5, 5, 7, 1, 1)}, {'5', DKR_OSD_G(7, 4, 7, 1, 7)},
            {'6', DKR_OSD_G(7, 4, 7, 5, 7)}, {'7', DKR_OSD_G(7, 1, 1, 1, 1)},
            {'8', DKR_OSD_G(7, 5, 7, 5, 7)}, {'9', DKR_OSD_G(7, 5, 7, 1, 7)},
            {'.', DKR_OSD_G(0, 0, 0, 0, 2)}, {'A', DKR_OSD_G(2, 5, 7, 5, 5)},
            {'D', DKR_OSD_G(6, 5, 5, 5, 6)}, {'F', DKR_OSD_G(7, 4, 6, 4, 4)},
            {'G', DKR_OSD_G(7, 4, 5, 5, 7)}, {'M', DKR_OSD_G(5, 7, 7, 5, 5)},
            {'N', DKR_OSD_G(6, 5, 5, 5, 5)}, {'P', DKR_OSD_G(7, 5, 7, 4, 4)},
            {'R', DKR_OSD_G(6, 5, 6, 5, 5)}, {'S', DKR_OSD_G(7, 4, 7, 1, 7)},
            {'X', DKR_OSD_G(5, 5, 2, 5, 5)},
        };
        #undef DKR_OSD_G
        for (const Entry& e : font) {
            if (e.c == c) { return e.rows; }
        }
        return 0;
    }

    static bool lit(unsigned short rows, int x, int y) {
        return ((rows >> ((4 - y) * 3 + (2 - x))) & 1u) != 0;
    }

    void add_glyph(unsigned short rows, int x0, int y0) {
        bool used[5][3] = {};
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 3; x++) {
                if (used[y][x] || !lit(rows, x, y)) { continue; }
                int w = 1;
                while (x + w < 3 && !used[y][x + w] && lit(rows, x + w, y)) { w++; }
                int h = 1;
                for (bool grow = true; grow && y + h < 5; ) {
                    for (int i = 0; i < w; i++) {
                        if (used[y + h][x + i] || !lit(rows, x + i, y + h)) { grow = false; }
                    }
                    if (grow) { h++; }
                }
                for (int j = 0; j < h; j++) {
                    for (int i = 0; i < w; i++) { used[y + j][x + i] = true; }
                }
                add_rect(x0 + x * kScale, y0 + y * kScale,
                         x0 + (x + w) * kScale, y0 + (y + h) * kScale, 255.0F);
            }
        }
    }

    void add_rect(int x0, int y0, int x1, int y1, float level) {
        if (used_ + 6 > kMaxVertices) { return; }
        const float xs[6] = {float(x0), float(x1), float(x1), float(x0), float(x1), float(x0)};
        const float ys[6] = {float(y0), float(y0), float(y1), float(y0), float(y1), float(y1)};
        for (int i = 0; i < 6; i++) {
            dkr_render_vertex& v = vertices_[used_ + i];
            std::memset(&v, 0, sizeof(v));
            v.x = xs[i];
            v.y = ys[i];
            v.r = v.g = v.b = level;
            v.a = 255.0F;
            v.oow = 1.0F;
        }
        used_ += 6;
    }

    dkr_render_vertex vertices_[kMaxVertices];
    int used_ = 0;
};

}  // namespace dkr::runtime
