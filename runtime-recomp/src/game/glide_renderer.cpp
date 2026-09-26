#include "glide_renderer.hpp"
#include "exclusive_section.hpp"

#include "game_registration.hpp"
#include "diagnostic_log.hpp"

#include "librecomp/game.hpp"

#if defined(DKR_TARGET_WIN95)
// For the frame dump: the read-back lives in the Glide layer, not in the
// backend interface, because it is a property of the card and not of the
// abstraction the decoder drives.
#include "render/glide.h"
// The allocator's own counters, for the report below. `backend.h` hands out a
// pointer to it; reading the struct needs its definition.
#include "render/tmu.h"
#include "render/capture.h"
#include "window.h"
// E00-S03's denominator. The time base is E02-S03's, measured on the machine at
// 1,193,180 Hz -- the 8254 PIT, 4.19 us a tick -- and **not**
// `high_resolution_clock`, which E02-S03 established is the wall clock on this
// toolchain and therefore steps with the system date.
extern "C" {
#include "clock.h"
}
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>

/* **Is the renderer inside a display list right now?**
 *
 * The scheduler wants to know. Its guest threads spend part of every frame with
 * all of them blocked, and the question that time raises is whether they are
 * waiting for this file or for something else. Answering it from the outside
 * needs one bit, sampled at the moment the last guest thread goes to sleep.
 *
 * A weak symbol, like `dkr_clock_now_us` and `dkr_diag_commit`: ultramodern is a
 * dependency and cannot depend back, so it declares this weak and checks for
 * null. A build without a Glide renderer links, and reports the wait as
 * unattributed rather than failing.
 *
 * The flag covers exactly the region `render_us_total_` measures, so the two
 * accounts agree by construction. */
static std::atomic<int> g_renderer_busy{0};

extern "C" int dkr_renderer_busy(void) {
    return g_renderer_busy.load(std::memory_order_relaxed);
}

/* Display lists drawn so far, for the game's `[audio][rate]` line: whether the
 * audio microcode's cost follows the frames drawn or the sound played is read by
 * setting it against both, window by window. */
static std::atomic<unsigned long long> g_display_lists_drawn{0};

extern "C" unsigned long long dkr_display_lists_drawn(void) {
    return g_display_lists_drawn.load(std::memory_order_relaxed);
}

/* The audio tasks' times, for the on-screen display: its only source outside
 * this file. Written by the audio task, read and cleared by the graphics thread
 * once a second; a task landing between the reads is counted in the next
 * second, which the display can afford. */
static std::atomic<unsigned long> g_osd_audio_sum{0};
static std::atomic<unsigned long> g_osd_audio_count{0};
static std::atomic<unsigned long> g_osd_audio_max{0};

extern "C" void dkr_osd_audio_task(unsigned long us) {
    g_osd_audio_sum.fetch_add(us, std::memory_order_relaxed);
    g_osd_audio_count.fetch_add(1, std::memory_order_relaxed);
    if (us > g_osd_audio_max.load(std::memory_order_relaxed)) {
        g_osd_audio_max.store(us, std::memory_order_relaxed);
    }
}

#if defined(DKR_TARGET_WIN95)
/* --- Where the renderer's time goes: zones, E08-S01 ---------------------------
 *
 * The renderer is the largest row of `frame-budget.md` and was one number. It
 * has a clean seam: the decoder calls the Glide backend only through the
 * `dkr_render_backend` table. Under DKR_TRACE_RENDER_ZONES the table is replaced
 * by a proxy that times each entry and forwards to the real one, and the
 * decoder's own time is `dkr_f3d_run` minus what it spent in the backend. Texture
 * conversion, the one piece of decoder work worth its own row, is timed inside
 * f3ddkr.c through `dkr_f3d_zone_clock`.
 *
 * Read under DKR_TRACE_EXCLUSIVE, so that the zones are processor time. */
enum RenderZone {
    kZoneSetState, kZoneScissor, kZoneDraw, kZoneFill, kZoneUpload,
    kZoneRelease, kZoneLookup, kZoneBegin, kZonePresent, kZoneInvalidate,
    kZoneCount
};
static const char* const kZoneNames[kZoneCount] = {
    "state", "scissor", "draw", "fill", "upload",
    "release", "lookup", "begin", "present", "invalidate"};
static const bool g_render_zones_on =
    std::getenv("DKR_TRACE_RENDER_ZONES") != nullptr;
static dkr_render_backend g_zone_inner{};
static unsigned long long g_zone_us[kZoneCount];
static unsigned long long g_zone_n[kZoneCount];
static unsigned long long g_zone_run_us = 0;
static unsigned long long g_zone_run_backend_us = 0;
static int g_zone_in_run = 0;
/* The zones' clock: the cycle counter when it calibrates, which costs a few
   cycles a read, and the 8254 otherwise, which costs 5.8 us and inflates every
   zone by it. All zone totals are in its ticks; `g_zone_hz` converts. */
static unsigned long long zone_cycles(void) { return dkr_cycles_now(); }
static unsigned long long (*g_zone_clock)(void) = dkr_clock_now_us;
static unsigned long long g_zone_hz = 1000000ULL;
static unsigned long long zone_us(unsigned long long ticks) {
    return ticks * 1000ULL / (g_zone_hz / 1000ULL);
}

struct ZoneTimer {
    explicit ZoneTimer(RenderZone z) : zone(z), t0(g_zone_clock()) {}
    ~ZoneTimer() {
        const unsigned long long d = g_zone_clock() - t0;
        g_zone_us[zone] += d;
        g_zone_n[zone]++;
        if (g_zone_in_run) { g_zone_run_backend_us += d; }
    }
    RenderZone zone;
    unsigned long long t0;
};

static void zone_begin_frame(void*, unsigned argb) {
    ZoneTimer t(kZoneBegin); g_zone_inner.begin_frame(g_zone_inner.self, argb);
}
static void zone_present(void*) {
    ZoneTimer t(kZonePresent); g_zone_inner.present(g_zone_inner.self);
}
static void zone_set_state(void*, const dkr_render_state* st) {
    ZoneTimer t(kZoneSetState); g_zone_inner.set_state(g_zone_inner.self, st);
}
static void zone_set_scissor(void*, int x0, int y0, int x1, int y1) {
    ZoneTimer t(kZoneScissor);
    g_zone_inner.set_scissor(g_zone_inner.self, x0, y0, x1, y1);
}
static void zone_invalidate(void*) {
    ZoneTimer t(kZoneInvalidate); g_zone_inner.invalidate(g_zone_inner.self);
}
static void zone_draw_triangles(void*, const dkr_render_vertex* v, int n) {
    ZoneTimer t(kZoneDraw); g_zone_inner.draw_triangles(g_zone_inner.self, v, n);
}
static void zone_fill_rect(void*, int x0, int y0, int x1, int y1, unsigned argb) {
    ZoneTimer t(kZoneFill);
    g_zone_inner.fill_rect(g_zone_inner.self, x0, y0, x1, y1, argb);
}
static dkr_texture_handle zone_texture_upload(void*, const dkr_texture_desc* d) {
    ZoneTimer t(kZoneUpload);
    return g_zone_inner.texture_upload(g_zone_inner.self, d);
}
static void zone_texture_release(void*, dkr_texture_handle h) {
    ZoneTimer t(kZoneRelease); g_zone_inner.texture_release(g_zone_inner.self, h);
}
static dkr_texture_handle zone_texture_lookup(void*, unsigned long long key, int tmu) {
    ZoneTimer t(kZoneLookup);
    return g_zone_inner.texture_lookup(g_zone_inner.self, key, tmu);
}

/* Replaces each non-null entry with its timed twin. `open` and `close` are left
   alone: they run once. */
static void install_render_zones(dkr_render_backend* b) {
    g_zone_inner = *b;
    if (b->begin_frame)     { b->begin_frame = zone_begin_frame; }
    if (b->present)         { b->present = zone_present; }
    if (b->set_state)       { b->set_state = zone_set_state; }
    if (b->set_scissor)     { b->set_scissor = zone_set_scissor; }
    if (b->invalidate)      { b->invalidate = zone_invalidate; }
    if (b->draw_triangles)  { b->draw_triangles = zone_draw_triangles; }
    if (b->fill_rect)       { b->fill_rect = zone_fill_rect; }
    if (b->texture_upload)  { b->texture_upload = zone_texture_upload; }
    if (b->texture_release) { b->texture_release = zone_texture_release; }
    if (b->texture_lookup)  { b->texture_lookup = zone_texture_lookup; }
    if (dkr_cycles_init()) {
        g_zone_clock = zone_cycles;
        g_zone_hz = dkr_cycles_hz();
    }
    dkr_f3d_zone_clock = g_zone_clock;
    std::fprintf(stderr, "[boot][gfx] render zones clock: %s at %llu Hz\n",
                 g_zone_clock == zone_cycles ? "rdtsc" : "8254", g_zone_hz);
}
#endif
#include <iterator>
#include <memory>

namespace {

#if defined(DKR_TARGET_WIN95)
// The size of the snapshot the graphics thread receives. It is fixed in
// `submit_rsp_task` (events.cpp) at 8 MiB, the N64's expanded RDRAM -- **not the
// 20 MiB patch 0017 sizes librecomp's space to**, which covers far more than the
// guest's RDRAM.
//
// Getting this wrong would not crash: the decoder bounds every range against
// this size, so too large a value turns an access past the snapshot into a read
// of neighbouring memory, and too small a value rejects legitimate geometry by
// counting it as an invalid address.
constexpr unsigned kSnapshotBytes = 0x800000u;

// This machine's Voodoo 2. 640x480 is the mode E05 measured, and the only one
// this port opens for now.
constexpr int kWidth = 640;
constexpr int kHeight = 480;

// --- Read the game's state, rather than inferring it from the rendering ------
//
// Every measurement so far described the same steady state -- same command count
// per list, same background colour, no depth sorting at all. They say what the
// game draws, never **where it has got to**. Going on perfecting the rendering of
// a frame the game may have no intention of advancing would misspend the effort.
//
// `gGameMode` is the variable DKR itself consults to decide what to do. Its
// address comes from the decomp's symbol map:
//
//     0x801234ec  gGameMode          -1 INTRO, 0 INGAME, 1 MENU, 5 LOCKUP
//     0x800dd394  gLevelLoadTimer
//     0x80121168  gCurrentLevelHeader
//
// `GAMEMODE_LOCKUP` deserves a mention: the game puts itself there when
// `get_lockup_status()` returns true, and shows a crash screen. If it is there,
// no rendering fix will change anything.
//
// **The read is native, with no byte swapping.** librecomp's RDRAM is XOR-3
// interleaved, and for an aligned 32-bit word the interleave and the host's
// little-endianness cancel exactly. Flipping the bytes "to fix the endianness"
// is the mistake already made once this session, on reading `curRDPTask`.
constexpr unsigned kAddrGameMode = 0x1234ECu;
constexpr unsigned kAddrLevelLoadTimer = 0x0DD394u;
constexpr unsigned kAddrLevelHeader = 0x121168u;

int read_word(const std::uint8_t* rdram, unsigned address) {
    int v = 0;
    std::memcpy(&v, rdram + address, sizeof(v));
    return v;
}

// The decoder trace, bounded -- and **two budgets, not one**.
//
// Without a trace one reads "one reject per list" and cannot tell which: the
// counter says there is a problem, the trace says which one. It is bounded
// because the log goes to an emulated floppy, and because the first few
// occurrences suffice -- a reject repeating six hundred times is diagnosed on
// the first.
//
// A single budget already cost a round trip: the deferred commands, far more
// numerous, exhausted it before a single reject had been written. The log then
// showed "589 opcode rejects" without saying which -- the trace meant to say
// which had been spent on noise.
//
// The reject is what we are after; the rest is context. They cannot draw from
// the same bucket.
int g_probe_x = 0;
int g_probe_y = 0;

unsigned g_trace_rejects = 24;
unsigned g_trace_context = 24;

bool starts_with(const char* line, const char* prefix) {
    while (*prefix != '\0') {
        if (*line != *prefix) { return false; }
        line++;
        prefix++;
    }
    return true;
}

void trace_decoder(void*, const char* line) {
    unsigned* bucket = starts_with(line, "REJECT") ? &g_trace_rejects : &g_trace_context;
    if (*bucket == 0) { return; }
    (*bucket)--;
    std::fprintf(stderr, "[gfx][f3d] %s\n", line);
}
#endif

} // namespace

dkr::runtime::GlideRenderer::GlideRenderer() {
    setup_result = ultramodern::renderer::SetupResult::Success;
    chosen_api = ultramodern::renderer::GraphicsApi::Auto;

#if defined(DKR_TARGET_WIN95)
    dkr_render_backend_glide(&backend_);
    if (g_render_zones_on) {
        install_render_zones(&backend_);
        std::fprintf(stderr, "[boot][gfx] render zones on\n");
    }
    if (backend_.open != nullptr && backend_.open(backend_.self, kWidth, kHeight) != 0) {
        opened_ = true;
        width_ = kWidth;
        height_ = kHeight;
        std::fprintf(stderr, "[boot][gfx] Glide opened at %dx%d\n", width_, height_);
        if (std::getenv("DKR_OSD") != nullptr) {
            osd_.reset(new FrameOsd());
            std::fprintf(stderr, "[boot][gfx] on-screen display on\n");
        }
    } else {
        // **Do not fail the startup for that, though.**
        //
        // `valid()` returning false stops the graphics thread, and the game with
        // it. But an absent or busy Voodoo is exactly the situation where one
        // still wants to be able to read a boot log. The context therefore stays
        // valid and draws nothing, which is `DiagnosticRenderer`'s behaviour - a
        // degradation, not a breakdown.
        std::fprintf(stderr, "[boot][gfx] Glide unavailable: rendering disabled\n");
    }
#endif
}

#if defined(DKR_TARGET_WIN95)
void dkr::runtime::GlideRenderer::dump_frame(const char* path) {
    // 640x480x4 is 1.2 MiB, and this machine has 64. A static buffer rather than
    // a stack one: the graphics thread's stack is nothing like that size.
    static std::uint32_t pixels[640 * 480];
    int w = 0;
    int h = 0;
    const int got = dkr_glide_read_backbuffer(pixels,
                                              static_cast<int>(std::size(pixels)),
                                              &w, &h);
    if (got <= 0 || w <= 0 || h <= 0) {
        std::fprintf(stderr, "[gfx] frame dump: read-back refused\n");
        return;
    }

    std::FILE* out = std::fopen(path, "wb");
    if (out == nullptr) {
        std::fprintf(stderr, "[gfx] frame dump: cannot open %s\n", path);
        return;
    }

    // Rows are padded to four bytes, and stored bottom-up: that is the BMP
    // format, not a choice. Getting either wrong gives a skewed image that reads
    // like a rendering defect.
    const int stride = w * 3;
    const int pad = (4 - (stride % 4)) % 4;
    const std::uint32_t size =
        static_cast<std::uint32_t>(54 + (stride + pad) * h);
    unsigned char head[54] = {0};
    head[0] = 'B'; head[1] = 'M';
    head[2] = static_cast<unsigned char>(size);
    head[3] = static_cast<unsigned char>(size >> 8);
    head[4] = static_cast<unsigned char>(size >> 16);
    head[5] = static_cast<unsigned char>(size >> 24);
    head[10] = 54;
    head[14] = 40;
    head[18] = static_cast<unsigned char>(w);
    head[19] = static_cast<unsigned char>(w >> 8);
    head[22] = static_cast<unsigned char>(h);
    head[23] = static_cast<unsigned char>(h >> 8);
    head[26] = 1;
    head[28] = 24;
    std::fwrite(head, 1, sizeof(head), out);

    // While every pixel is in hand, measure whether there is an image at all.
    //
    // **The count of pixels differing from the corner is not that measure**, and
    // the first version of this dump used it. It reported 299239 of 307200 --
    // "97 % painted" -- on a frame that is six shades of the same grey, because
    // nearly every pixel differs from the corner by one unit in one channel.
    // That is the saturated count `state_probe.c` warns about in its own
    // comments, reproduced here the same day.
    //
    // The honest measure is **how many distinct colours** the frame holds. Six
    // says "uniform" whatever the differing count claims; a real image runs to
    // hundreds. Counted over the card's own 565 space, one bit per possible
    // value: 8 KiB of bitmap, and no allocation on the graphics thread.
    static unsigned char seen[65536 / 8];
    std::memset(seen, 0, sizeof(seen));
    unsigned long distinct = 0;
    unsigned long non_background = 0;
    unsigned long painted = 0;
    const std::uint32_t background = pixels[0] & 0x00FFFFFFu;
    for (int y = h - 1; y >= 0; y--) {
        const std::uint32_t* row = pixels + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; x++) {
            const std::uint32_t p = row[x];
            const unsigned r = (p >> 16) & 0xFFu;
            const unsigned g = (p >> 8) & 0xFFu;
            const unsigned b = p & 0xFFu;
            const unsigned key =
                ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            unsigned char bgr[3] = {
                static_cast<unsigned char>(b),
                static_cast<unsigned char>(g),
                static_cast<unsigned char>(r)};
            std::fwrite(bgr, 1, 3, out);
            if ((seen[key >> 3] & (1u << (key & 7u))) == 0) {
                seen[key >> 3] |= static_cast<unsigned char>(1u << (key & 7u));
                distinct++;
            }
            if ((p & 0x00FFFFFFu) != background) { non_background++; }
            /* --- And what "painted" actually means ------------------------- *
             *
             * `differing` counts pixels unlike the **corner**, which is whatever
             * happens to be at (0,0) -- black on one run and white on the next.
             * Read as a coverage figure it inverts whenever the corner does, and
             * it has now been read that way twice: once across a table where
             * three rows carried `DKR_PAINT_WHITE` and one did not, and once when
             * a white frame's 112,081 "painted" pixels were in fact the 112,081
             * that had *not* been.
             *
             * The frame is cleared to black and the game's first fill is black,
             * so a pixel that is not black is a pixel something drew. That is
             * the same question whatever the corner turns out to be. */
            if ((p & 0x00FFFFFFu) != 0u) { painted++; }
        }
        for (int i = 0; i < pad; i++) { std::fputc(0, out); }
    }
    std::fclose(out);

    std::fprintf(stderr,
                 "[gfx] frame dump: %s %dx%d corner=%06lX distinct=%lu "
                 "differing=%lu painted=%lu/%ld\n",
                 path, w, h, static_cast<unsigned long>(background),
                 distinct, non_background, painted, static_cast<long>(w) * h);

    // What the TMU was pointed at while that frame was drawn. Printed here
    // rather than in the periodic report because it is this frame the image
    // belongs to, and the two have to be read together.
    {
        unsigned long binds = 0;
        unsigned long dead = 0;
        unsigned long changed = 0;
        dkr_glide_backend_bind_stats(&binds, &dead, &changed);
        std::fprintf(stderr,
                     "[gfx] frame dump: binds=%lu skipped-dead=%lu changed=%lu "
                     "submitted=%lu can-draw=%d\n",
                     binds, dead, changed,
                     dkr_glide_backend_triangle_count(),
                     dkr_glide_backend_can_draw());
    }
    {
        // Whether each triangle spans texture space at all. The run-wide s/t
        // extremes cannot answer this: they are wide even when every triangle
        // samples a single point.
        std::fprintf(stderr,
                     "[gfx] frame dump: st-degenerate=%lu st-varying=%lu\n",
                     context_.state.tri_st_degenerate,
                     context_.state.tri_st_varying);
        // **The alternative I had not excluded.** A single large triangle drawn
        // last, wearing a near-uniform texture, produces exactly this frame and
        // is no defect at all. The area histogram for *this* list says whether
        // the screen is one quad or a scene: these are per-list counters, unlike
        // the run-wide ones in the periodic report.
        std::fprintf(stderr,
                     "[gfx] frame dump: areas <1px=%lu <100=%lu <10k=%lu >=10k=%lu\n",
                     context_.state.area[0], context_.state.area[1],
                     context_.state.area[2], context_.state.area[3]);
        std::fprintf(stderr,
                     "[gfx] frame dump: largest triangle=%lu px of %d\n",
                     context_.state.area_max, width_ * height_);
        // **Which of the two painted the flat frame.**
        //
        // The 3D lists come back as a single colour over the whole screen. A
        // full-screen `G_FILLRECT` and one very large triangle both produce
        // that, and every counter this state carries adds them to different
        // totals without saying which one is on top. So both are reported
        // verbatim: the fills with their rectangles and colours, the largest
        // triangle with its corners, its vertex colour and the state it went
        // out under. One run separates them; guessing costs a run each.
        std::fprintf(stderr,
                     "[gfx] frame dump: big-tri (%d,%d) (%d,%d) (%d,%d) "
                     "rgb=%06X combine=%u blend=%u depth=%u\n",
                     context_.state.big_tri[0][0], context_.state.big_tri[0][1],
                     context_.state.big_tri[1][0], context_.state.big_tri[1][1],
                     context_.state.big_tri[2][0], context_.state.big_tri[2][1],
                     context_.state.big_tri_color,
                     static_cast<unsigned>(context_.state.big_tri_state[0]),
                     static_cast<unsigned>(context_.state.big_tri_state[1]),
                     static_cast<unsigned>(context_.state.big_tri_state[2]));
        // **The paint stack of the centre pixel, in submission order.**
        //
        // Everything else here summarises over the frame, and the frame's
        // problem is an order: with the depth test off the screen went black
        // rather than legible, so the scene is painted over rather than hidden
        // behind a comparison. The last sixteen triangles covering (w/2, h/2)
        // say what covers it and what state each went out under.
        {
            const unsigned long hits = context_.state.center_hits;
            const unsigned long shown = hits < 16u ? hits : 16u;
            unsigned long k;
            std::fprintf(stderr,
                     "[gfx] frame dump: triangles on-screen=%lu off-screen=%lu "
                     "area-in-viewport=%lu fogged=%lu\n",
                     context_.state.tri_on_screen, context_.state.tri_off_screen,
                     context_.state.on_screen_area,
                     context_.state.emitted_fogged);
        // **Per frame, not per run.** Which state the triangles of *this* list
        // went out under. Arguing about which field kills the geometry from a
        // run-wide census -- which mixes the title screen's rectangles in with
        // the menu's geometry -- is how three candidates were argued away on
        // figures that did not describe the frame in hand.
        std::fprintf(stderr,
                     "[gfx] frame dump: emitted combine=%lu/%lu/%lu/%lu/%lu "
                     "blend=%lu/%lu/%lu depth=%lu/%lu/%lu alpha-test=%lu ref=%u\n",
                     context_.state.emitted_per_combine[0],
                     context_.state.emitted_per_combine[1],
                     context_.state.emitted_per_combine[2],
                     context_.state.emitted_per_combine[3],
                     context_.state.emitted_per_combine[4],
                     context_.state.emitted_per_blend[0],
                     context_.state.emitted_per_blend[1],
                     context_.state.emitted_per_blend[2],
                     context_.state.emitted_per_depth[0],
                     context_.state.emitted_per_depth[1],
                     context_.state.emitted_per_depth[2],
                     context_.state.emitted_alpha_test,
                     context_.state.alpha_ref_max);
        // The RDP's other way of cutting a texel out, which `alpha-test` above
        // does not see. Counted before anything acts on it.
        std::fprintf(stderr,
                     "[gfx] frame dump: cvg-x-alpha=%lu alpha-cvg-sel=%lu\n",
                     context_.state.states_cvg_x_alpha,
                     context_.state.states_alpha_cvg_sel);
        std::fprintf(stderr, "[gfx] frame dump: centre hits=%lu\n", hits);
            for (k = 0; k < shown; k++) {
                // Oldest of the retained ones first, so the list reads in the
                // order the card received them.
                const unsigned long s = (hits - shown + k) % 16u;
                if (context_.state.center_state[s][0] == 0xFFu) {
                    std::fprintf(stderr,
                                 "[gfx] frame dump: centre%lu FILL after tri=%lu "
                                 "area=%lu rgb=%06X\n",
                                 k, context_.state.center_ordinal[s],
                                 context_.state.center_area[s],
                                 context_.state.center_rgb[s]);
                    continue;
                }
                std::fprintf(stderr,
                             "[gfx] frame dump: centre%lu tri=%lu area=%lu "
                             "rgb=%06X combine=%u blend=%u depth=%u tex=%u\n",
                             k, context_.state.center_ordinal[s],
                             context_.state.center_area[s],
                             context_.state.center_rgb[s],
                             static_cast<unsigned>(context_.state.center_state[s][0]),
                             static_cast<unsigned>(context_.state.center_state[s][1]),
                             static_cast<unsigned>(context_.state.center_state[s][2]),
                             static_cast<unsigned>(context_.state.center_state[s][3]));
                std::fprintf(stderr,
                             "[gfx] frame dump:   tex %dx%d fmt=%u "
                             "st=(%d,%d) (%d,%d) (%d,%d) texel0=%04X "
                             "tile=(%d,%d) dark=%lu/%lu mean=%lu/31\n",
                             context_.state.center_tex_w[s],
                             context_.state.center_tex_h[s],
                             static_cast<unsigned>(context_.state.center_tex_fmt[s]),
                             context_.state.center_s[s][0], context_.state.center_t[s][0],
                             context_.state.center_s[s][1], context_.state.center_t[s][1],
                             context_.state.center_s[s][2], context_.state.center_t[s][2],
                             context_.state.center_texel0[s],
                             context_.state.center_tile_uls[s],
                             context_.state.center_tile_ult[s],
                             static_cast<unsigned long>(context_.state.center_dark[s]),
                             static_cast<unsigned long>(context_.state.center_texels[s]),
                             static_cast<unsigned long>(context_.state.center_mean[s]));
            }
        }
        // **Flat is not black.** The run-wide `texels: black=0 with-content=329`
        // was read as "the textures carry an image", and it says no such thing:
        // a texture uniformly dark grey is not black. Since 413 triangles a list
        // do vary their s and t and the screen still shows one colour, the
        // texture being sampled is the first thing to rule out.
        std::fprintf(stderr,
                     "[gfx] frame dump: tex-shifts=%lu offset-dropped=%lu "
                     "tiles=%lu\n",
                     context_.state.texture_shifts_applied,
                     context_.state.texture_offset_dropped,
                     context_.state.tiles_decoded);
        std::fprintf(stderr,
                     "[gfx] frame dump: textures uniform=%lu varied=%lu "
                     "mostly-black=%lu\n",
                     context_.state.textures_uniform,
                     context_.state.textures_varied,
                     context_.state.textures_mostly_black);
        if (context_.state.uniform_sample_texel != 0u) {
            std::fprintf(stderr,
                         "[gfx] frame dump: uniform sample texel=%04X %dx%d fmt=%u\n",
                         context_.state.uniform_sample_texel & 0xFFFFu,
                         context_.state.uniform_sample_w,
                         context_.state.uniform_sample_h,
                         context_.state.uniform_sample_format);
        }
        std::fprintf(stderr,
                     "[gfx] frame dump: st inside=%lu outside=%lu\n",
                     context_.state.st_inside, context_.state.st_outside);
        std::fprintf(stderr,
                     "[gfx] frame dump: colour-image=0x%08X depth-image=0x%08X "
                     "fills-to-depth=%lu blend-rects=%lu\n",
                     context_.state.color_image_address,
                     context_.state.depth_image_address,
                     context_.state.fills_to_depth,
                     context_.state.blend_rects);
        std::fprintf(stderr, "[gfx] frame dump: fills=%lu\n",
                     context_.state.fill_sample_n);
        {
            unsigned long q;
            const unsigned long shown =
                context_.state.fill_sample_n < 8u ? context_.state.fill_sample_n : 8u;
            for (q = 0; q < shown; q++) {
                std::fprintf(stderr,
                             "[gfx] frame dump: fill%lu %d,%d..%d,%d rgb=%06X "
                             "target=0x%08X after-tri=%lu\n",
                             q,
                             context_.state.fill_sample[q][0],
                             context_.state.fill_sample[q][1],
                             context_.state.fill_sample[q][2],
                             context_.state.fill_sample[q][3],
                             context_.state.fill_sample_color[q],
                             context_.state.fill_sample_target[q],
                             context_.state.fill_sample_after[q]);
            }
        }
        // Where the geometry actually lands. 2457600 px is exactly eight
        // screens, which is the guard band's clamp at +/-2048 - so the areas
        // alone cannot say whether a quad merely overhangs the viewport or the
        // projection scale is wrong. These extremes can.
        std::fprintf(stderr,
                     "[gfx] frame dump: projected x=[%ld..%ld] y=[%ld..%ld] "
                     "screen=%dx%d\n",
                     context_.state.proj_x_min, context_.state.proj_x_max,
                     context_.state.proj_y_min, context_.state.proj_y_max,
                     width_, height_);
        // Before the guard band touches it. On-screen geometry belongs in
        // [-1, 1]; the figure above is the clamp, this one is the geometry.
        //
        // **Integers scaled by a thousand, not `%f`.** The first version used
        // `%.2f` and printed **nothing at all** on this target -- the call is
        // reached, the format string is in the binary, and no line appears.
        // `%f` is unusable here. The `coords:` line above already scales to
        // thousandths for the same reason; that workaround was in the code and
        // not in any comment, so it had to be rediscovered by losing a run.
        //
        // The clamp guards against the seed value: with no triangle processed
        // the extremes still hold +/-1e30, and casting that to int is undefined
        // as well as unreadable.
        {
            const float lo = -1.0e6F;
            const float hi = 1.0e6F;
            float xn = context_.state.ndc_x_min;
            float xx = context_.state.ndc_x_max;
            float yn = context_.state.ndc_y_min;
            float yx = context_.state.ndc_y_max;
            if (xn < lo) { xn = lo; } if (xn > hi) { xn = hi; }
            if (xx < lo) { xx = lo; } if (xx > hi) { xx = hi; }
            if (yn < lo) { yn = lo; } if (yn > hi) { yn = hi; }
            if (yx < lo) { yx = lo; } if (yx > hi) { yx = hi; }
            // **The matrix itself, once.** ndc = x/w, and a uniform scale error
            // in the matrix cancels in that division - so the fault is
            // necessarily non-uniform, and sixteen numbers say which row. The
            // alternative is another run per hypothesis about a matrix nobody
            // has looked at.
            {
                const dkr_matrix* mv = dkr_transform_mvp(&context_.transform);
                int r;
                if (mv != nullptr) {
                    for (r = 0; r < 4; r++) {
                        std::fprintf(stderr,
                                     "[gfx] frame dump: mvp[%d] "
                                     "%d %d %d %d /1000\n", r,
                                     static_cast<int>(mv->m[r][0] * 1000.0F),
                                     static_cast<int>(mv->m[r][1] * 1000.0F),
                                     static_cast<int>(mv->m[r][2] * 1000.0F),
                                     static_cast<int>(mv->m[r][3] * 1000.0F));
                    }
                }
            }
            std::fprintf(stderr,
                         "[gfx] frame dump: ndc inside=%lu outside=%lu\n",
                         context_.state.ndc_inside, context_.state.ndc_outside);
            // Decoded against drawn, which is the pair whose absence hid
            // G_TEXRECT being skipped: the histogram count rises whether or not
            // the command does anything. The half-word opcodes are reported
            // because gbi.h and this decoder disagreed on which they are, and
            // the halves are taken by position precisely so the log can settle
            // it rather than the code assume it.
            std::fprintf(stderr,
                         "[gfx] frame dump: texrect seen=%lu drawn=%lu "
                         "no-texture=%lu halves=%02X,%02X\n",
                         context_.state.texrects_seen,
                         context_.state.texrects_drawn,
                         context_.state.texrects_no_texture,
                         context_.state.texrect_half_opcode[0],
                         context_.state.texrect_half_opcode[1]);
            std::fprintf(stderr,
                         "[gfx] frame dump: rect tex fmt=%u %dx%d padded %dx%d "
                         "s=[%d..%d]/1000 texels\n",
                         context_.state.rect_tex_format,
                         context_.state.rect_tex_w, context_.state.rect_tex_h,
                         context_.state.rect_tex_pw, context_.state.rect_tex_ph,
                         context_.state.rect_s0_1000, context_.state.rect_s1_1000);
            std::fprintf(stderr,
                         "[gfx] frame dump: rect state combine=%u blend=%u "
                         "alpha-test=%u ref=%u\n",
                         context_.state.rect_state[0], context_.state.rect_state[1],
                         context_.state.rect_state[2], context_.state.rect_state[3]);
            for (unsigned long q = 0; q < context_.state.rect_sample_n; q++) {
                std::fprintf(stderr,
                             "[gfx] frame dump: rect%lu %d,%d..%d,%d\n", q,
                             context_.state.rect_sample[q][0],
                             context_.state.rect_sample[q][1],
                             context_.state.rect_sample[q][2],
                             context_.state.rect_sample[q][3]);
            }
            std::fprintf(stderr,
                         "[gfx] frame dump: ndc x=[%d..%d]/1000 y=[%d..%d]/1000\n",
                         static_cast<int>(xn * 1000.0F),
                         static_cast<int>(xx * 1000.0F),
                         static_cast<int>(yn * 1000.0F),
                         static_cast<int>(yx * 1000.0F));
        }
    }
}
#endif

// Once a second: the text of the on-screen display, from what the second held.
// Integer formatting, tenths of a millisecond.
void dkr::runtime::GlideRenderer::osd_refresh(unsigned long long now_us) {
    const unsigned long long span = osd_t0_ != 0ULL ? now_us - osd_t0_ : 0ULL;
    const unsigned long audio_sum = g_osd_audio_sum.exchange(0, std::memory_order_relaxed);
    const unsigned long audio_n = g_osd_audio_count.exchange(0, std::memory_order_relaxed);
    const unsigned long audio_max = g_osd_audio_max.exchange(0, std::memory_order_relaxed);
    const auto tenths = [] (unsigned long long us) -> unsigned long {
        return static_cast<unsigned long>((us + 50ULL) / 100ULL);
    };
    const unsigned long fps10 = (span != 0ULL)
        ? static_cast<unsigned long>((static_cast<unsigned long long>(osd_periods_) *
                                      10000000ULL) / span)
        : 0UL;
    const unsigned long frm = tenths(osd_periods_ ? osd_period_sum_ / osd_periods_ : 0ULL);
    const unsigned long frm_max = tenths(osd_period_max_);
    const unsigned long gfx = tenths(osd_renders_ ? osd_render_sum_ / osd_renders_ : 0ULL);
    const unsigned long gfx_max = tenths(osd_render_max_);
    const unsigned long snd = tenths(audio_n ? audio_sum / audio_n : 0UL);
    const unsigned long snd_max = tenths(audio_max);

    char lines[FrameOsd::kLines][FrameOsd::kColumns + 1];
    std::snprintf(lines[0], sizeof(lines[0]), "%2lu.%lu FPS", fps10 / 10, fps10 % 10);
    std::snprintf(lines[1], sizeof(lines[1]), "FRM %3lu.%lu MAX %3lu.%lu",
                  frm / 10, frm % 10, frm_max / 10, frm_max % 10);
    std::snprintf(lines[2], sizeof(lines[2]), "GFX %3lu.%lu MAX %3lu.%lu",
                  gfx / 10, gfx % 10, gfx_max / 10, gfx_max % 10);
    std::snprintf(lines[3], sizeof(lines[3]), "SND %3lu.%lu MAX %3lu.%lu",
                  snd / 10, snd % 10, snd_max / 10, snd_max % 10);
    osd_->set_text(lines);
    // Its own cost, every twentieth second: what the display adds to `render`.
    if (++osd_refreshes_ % 20UL == 0UL && osd_draws_ != 0UL) {
        std::fprintf(stderr, "[gfx] osd: triangles=%d draw=%lu us a list\n",
                     osd_->triangles(),
                     static_cast<unsigned long>(osd_draw_us_ / osd_draws_));
        osd_draw_us_ = 0;
        osd_draws_ = 0;
    }

    osd_t0_ = now_us;
    osd_period_sum_ = osd_period_max_ = 0;
    osd_periods_ = 0;
    osd_render_sum_ = osd_render_max_ = 0;
    osd_renders_ = 0;
}

bool dkr::runtime::GlideRenderer::valid() {
    return true;
}

bool dkr::runtime::GlideRenderer::update_config(
    const ultramodern::renderer::GraphicsConfig&,
    const ultramodern::renderer::GraphicsConfig&) {
    return false;
}

void dkr::runtime::GlideRenderer::enable_instant_present() {}

void dkr::runtime::GlideRenderer::send_dl(const OSTask* task,
                                          [[maybe_unused]] std::uint8_t* rdram_snapshot) {
    const auto index = ++display_list_count_;

#if defined(DKR_TARGET_WIN95)
    if (!opened_ || rdram_snapshot == nullptr) {
        return;
    }

    /* --- E00-S03's missing denominator -------------------------------------- *
     *
     * The CPU budget has had both its *factors* since 11 August -- 2.16x for the
     * 64 to 32 bit move, 17.7x for the normalisation to a 400 MHz Pentium II,
     * hence 38x from this development machine to the target. What it has never
     * had is the quantity those factors multiply: **how long a frame actually
     * takes on the machine**. The ticket says so in as many words -- "it is no
     * longer a factor, it is a denominator" -- and names E02-S06 as what was
     * blocking it. E02-S06 is passed.
     *
     * Two clocks, and the pair is the point:
     *
     *   - `period` is the wall time between one graphics task arriving and the
     *     next. That is the frame rate, whatever produces it.
     *   - `render` is the time spent inside this function: decode, transform,
     *     clip, convert, hand to Glide, present.
     *
     * Their difference is everything else -- the recompiled MIPS code, the
     * scheduler, the audio -- which is the half nobody can time directly and the
     * half the go/no-go is about. Reporting only one of the two would say which
     * number is large without saying whose it is, and this project has already
     * spent a week on a ratio with no denominator.
     *
     * The mean is kept alongside the worst case because they answer different
     * questions: the mean is the frame rate, the worst case is whether the
     * scheduler's watchdog fires. `[trace][dl]` in `events.cpp` already watches
     * the worst case and nothing watched the mean. */
    /* **The clock does not initialise itself, and returns zero until it does.**
     *
     * `dkr_clock_now` answers 0 while `clock_source` is NONE, so an uninitialised
     * base does not fail -- it reports every frame as instantaneous, which is a
     * plausible-looking wrong answer of exactly the kind this project has spent
     * days on. Nothing in the game called `dkr_clock_init`: the witnesses did,
     * and E02-S03's measurements were all made by them. Called here, once, and
     * the source it settled on is logged so that a fallback to a coarser clock
     * cannot pass unnoticed -- `GetTickCount` at 9 ms would quantise a 33 ms
     * frame into four steps. */
    /* `DkrMain` initialises the clock before any thread exists; this call is the
       idempotent second one, kept so that a renderer driven by a witness -- which
       has no `DkrMain` -- still gets a time base. `dkr_clock_init` returns 1
       immediately when a source is already chosen. */
    static const bool clock_ready = dkr_clock_init() != 0;
    // Under DKR_TRACE_EXCLUSIVE, the display list runs at time-critical
    // priority so that `render=` is processor time; see exclusive_section.hpp.
    const ExclusiveSection exclusive;
    const unsigned long long t_entry = clock_ready ? dkr_clock_now_us() : 0ULL;
    g_renderer_busy.store(1, std::memory_order_relaxed);
    if (clock_ready && last_task_us_ != 0ULL) {
        const unsigned long long d = t_entry - last_task_us_;
        /* A first period after a pause is not a frame -- the ROM load and the
           dumps both produce one -- and averaging it in would move the mean by
           more than the thing being measured. Anything past a second is dropped
           and counted, so the drop is visible rather than silent. */
        if (d < 1000000ULL) {
            period_us_total_ += d;
            period_n_++;
            {
                unsigned long n = static_cast<unsigned long>((d + 8333ULL) / 16667ULL);
                period_retraces_[n > 8UL ? 8UL : n]++;
            }
            {
                const unsigned long bin = static_cast<unsigned long>(d / 2000ULL);
                period_bins_[bin > 50UL ? 50UL : bin]++;
                period_hist_.add(d);
                last_period_us_ = d;
                osd_period_sum_ += d;
                osd_periods_++;
                if (d > osd_period_max_) { osd_period_max_ = d; }
            }
            if (d > period_us_worst_) { period_us_worst_ = d; }
        } else {
            period_dropped_++;
        }
    }
    last_task_us_ = t_entry;

    backend_.begin_frame(backend_.self, 0x000000);

    // Reset for every display list, not carried across frames. That is the
    // microcode's contract: each graphics task arrives with its own DMAOffsets
    // and its own matrix. Keeping state would make one frame depend on the
    // previous, and the first symptom would be geometry correct at boot then
    // drifting -- the worst case to diagnose.
    dkr_f3d_init(&context_, rdram_snapshot, kSnapshotBytes, &backend_);
    // librecomp's XOR-3 interleaved layout. Without this flag the decoder would
    // read plausible opcodes at absurd addresses.
    context_.rdram_native = 1;
    // E05-S04. The decoder must know how many units it can aim at, and it reads
    // it rather than calling: `dkr_glide_backend_tmu_count` accounts for both
    // E05-S01's detection and the single-TMU test override, and the software
    // oracle -- which has one -- answers for itself.
    context_.tmu_count =
        static_cast<unsigned char>(dkr_glide_backend_tmu_count());
    // `trace_decoder` prints the first 24 context lines and then discards the
    // rest, but the decoder formats every line before handing it over: 16.7 ms
    // of a 31 ms display list, measured by the render zones. Once the context
    // lines are spent, only the rejections keep a route to the log.
    context_.trace = (g_trace_context != 0) ? trace_decoder : nullptr;
    context_.reject_trace = trace_decoder;
    // `DKR_GFX_NO_STATS=1` skips the decoder's per-triangle statistics. The
    // counter lines they feed then read zero. Off by default.
    {
        static const bool no_stats = std::getenv("DKR_GFX_NO_STATS") != nullptr;
        context_.no_statistics = no_stats ? 1 : 0;
    }
    // `DKR_NO_DEPTH=1` turns depth sorting off. A diagnostic switch: it answers
    // in one run a question that reading the code does not settle.
    {
        static const bool no_depth = (std::getenv("DKR_NO_DEPTH") != nullptr);
        context_.no_depth = no_depth ? 1 : 0;
        // `DKR_NO_TEXCACHE=1` converts every texture even when the card holds
        // it. It exists so that the frame-dump probe's texel figures come back
        // exact: the residency path has nothing converted to describe and
        // reports `dark=0/0` rather than the previous texture's numbers.
        static const bool no_texcache =
            (std::getenv("DKR_NO_TEXCACHE") != nullptr);
        context_.no_texture_cache = no_texcache ? 1 : 0;
        // `DKR_NO_ALPHA_TEST=1` draws every texel whatever its alpha. It answers
        // in one run whether the speckle eaten out of this game's best-time
        // digits is the `CVG_X_ALPHA` cutout meeting a dithered coverage — the
        // approximation `rdp_state.c` already names — or something else.
        static const bool no_alpha_test =
            (std::getenv("DKR_NO_ALPHA_TEST") != nullptr);
        context_.no_alpha_test = no_alpha_test ? 1 : 0;
        // `DKR_NO_MULTIPASS=1` draws every triangle once, turning off the
        // pre-pass and the second pass together. The cutout was excluded for the
        // shredded digits by exactly this method — one switch, one run — and the
        // extra passes are the next hypothesis with a shape: they are the newest
        // thing in the pixel path, and one of them lays an opaque constant over
        // the whole triangle before putting the texel back.
        static const bool no_multipass =
            (std::getenv("DKR_NO_MULTIPASS") != nullptr);
        if (no_multipass) {
            dkr_glide_backend_extra_passes(0);
        }
        // `DKR_FORCE_COMBINE=shade|texel|texel_shade|texel_shade_a` forces every
        // draw to one combine mode. Named rather than numbered: a run costs four
        // minutes, and `DKR_FORCE_COMBINE=2` in a batch file three weeks from now
        // says nothing about what was measured.
        static const unsigned char forced = [] () -> unsigned char {
            const char* v = std::getenv("DKR_FORCE_COMBINE");
            if (v == nullptr) { return 0; }
            if (std::strcmp(v, "shade") == 0) { return DKR_COMBINE_SHADE + 1; }
            if (std::strcmp(v, "texel") == 0) { return DKR_COMBINE_TEXTURE + 1; }
            if (std::strcmp(v, "texel_shade") == 0) {
                return DKR_COMBINE_TEXTURE_SHADE + 1;
            }
            if (std::strcmp(v, "texel_shade_a") == 0) {
                return DKR_COMBINE_TEXTURE_SHADE_ALPHA + 1;
            }
            std::fprintf(stderr,
                         "[boot][gfx] DKR_FORCE_COMBINE=%s unrecognised, ignored\n",
                         v);
            return 0;
        } ();
        context_.force_combine = forced;
        // `DKR_SCISSOR=1` lets `G_SETSCISSOR` reach the card. Off by default:
        // the command is decoded and counted regardless, and switching it on
        // costs 230 distinct colours in the measured frames for a reason not yet
        // named. See `cmd` for `OP_SETSCISSOR` in `f3ddkr.c`.
        static const bool scissor = (std::getenv("DKR_SCISSOR") != nullptr);
        context_.scissor_enabled = scissor ? 1 : 0;
        // `DKR_FLATTEN_W=1` makes every triangle carry what a textured rectangle
        // carries in oow/z/ooz. See the note at the emission in `f3ddkr.c`.
        static const bool flatten = (std::getenv("DKR_FLATTEN_W") != nullptr);
        context_.flatten_w = flatten ? 1 : 0;
        // `DKR_PAINT_WHITE=1`: every emitted vertex opaque white. See the note at
        // the emission in `f3ddkr.c` -- it separates "does not rasterise" from
        // "rasterises black on a black clear", which every counter conflates.
        static const bool white = (std::getenv("DKR_PAINT_WHITE") != nullptr);
        context_.paint_white = white ? 1 : 0;
        // `DKR_FOG=1` puts fog back. It is off by default: Glide takes its
        // factor from the vertex alpha, which here carries opacity, and
        // G_SETFOGCOLOR is never decoded -- so fog on paints the 3D layer black.
        static const bool fog = (std::getenv("DKR_FOG") != nullptr);
        context_.fog_enabled_override = fog ? 1 : 0;
        // `DKR_FORCE_STATE=1`: every triangle under the canary's state block.
        static const unsigned char fstate = [] () -> unsigned char {
            const char* v = std::getenv("DKR_FORCE_STATE");
            if (v == nullptr) { return 0; }
            const long n = std::strtol(v, nullptr, 10);
            return static_cast<unsigned char>(n < 1 ? 1 : (n > 3 ? 3 : n));
        } ();
        context_.force_state = fstate;
        // `DKR_NEUTRAL=<mask>`: neutralise fields of the render state one bit at
        // a time. 1 texture, 2 filter, 4 blend, 8 fog, 16 constant colour.
        // `DKR_PROBE=x,y` moves the paint-stack probe off the screen centre.
        static const bool probe_set = [] () -> bool {
            const char* v = std::getenv("DKR_PROBE");
            if (v == nullptr) { return false; }
            int px = 0, py = 0;
            if (std::sscanf(v, "%d,%d", &px, &py) == 2) {
                g_probe_x = px;
                g_probe_y = py;
                return true;
            }
            return false;
        } ();
        (void)probe_set;
        context_.probe_x = g_probe_x;
        context_.probe_y = g_probe_y;
        static const unsigned char neutral = [] () -> unsigned char {
            const char* v = std::getenv("DKR_NEUTRAL");
            if (v == nullptr) { return 0; }
            return static_cast<unsigned char>(std::strtol(v, nullptr, 10) & 31);
        } ();
        context_.neutral_mask = neutral;
        // **Once, not per list.** The first version announced the forced mode
        // from inside this block, which runs for every display list: on a target
        // whose stderr is unbuffered and committed to disk per line, that is one
        // floppy write per frame. The run managed 39 lists where its neighbours
        // reached 400 -- a diagnostic that slowed by ten times the thing it was
        // measuring, and produced no frame because the dump never came up.
        static bool announced = false;
        if (forced != 0 && !announced) {
            announced = true;
            std::fprintf(stderr, "[boot][gfx] combine forced to mode %u\n",
                         static_cast<unsigned>(forced - 1u));
        }
    }
    // The resolution actually opened: the decoder needs it to carry the game's
    // buffer (320 wide) onto the screen, for the 2D rectangles as well as for
    // the 3D viewport.
    context_.screen_width = static_cast<unsigned>(width_);
    context_.screen_height = static_cast<unsigned>(height_);

    // **One whole list, dumped once.**
    //
    // The counters led this far then went quiet: 70 commands per list, constant
    // from list 300 on, two fills and nothing else. A stable number says nothing
    // more about what the game is building; the list has to be seen.
    //
    // The 300th is not an arbitrary choice: that is where the rate settles, so
    // it is the first list describing the state the game stays in. Dumping the
    // first would give the initialisation sequence instead.
    //
    // `DKR_TRACE_LIST=<n>` overrides it. The list worth reading is no longer a
    // fixed one: the dump is anchored on `gGameMode`, so the interesting lists
    // are the first few after the menu, and 300 is far past them. It is asked
    // for by number rather than guessed at, because the question a full trace
    // answers -- what the list actually contains, in order -- is the one no
    // counter has been able to answer about the trailing full-buffer fill.
    {
        static const long traced = [] () -> long {
            const char* v = std::getenv("DKR_TRACE_LIST");
            return v == nullptr ? 300L : std::strtol(v, nullptr, 10);
        } ();
        if (static_cast<long>(index) == traced) {
            g_trace_context = 900;
            std::fprintf(stderr, "[gfx] --- list %ld, full contents ---\n", traced);
        }
    }
    dkr_transform_set_viewport(&context_.transform,
                               static_cast<float>(width_) * 0.5F,
                               -static_cast<float>(height_) * 0.5F,
                               static_cast<float>(width_) * 0.5F,
                               static_cast<float>(height_) * 0.5F);

    // The address is guest-virtual (0x80xxxxxx); the snapshot is indexed
    // physically.
    /* --- E09-S02: freeze one list, so that the next comparison means something -
     *
     * `DKR_CAPTURE_LIST=<n>[,<n>...]` writes each list to `D:\CAPnnnn.BIN` -- the
     * display list's start address and the whole RDRAM image it reads from.
     * Replayed, the same bytes give the same image whatever else changed, which
     * is the only way an image difference becomes attributable to the renderer
     * rather than to the moment of an animation.
     *
     * Written **before** the decode, not after: the decoder writes into the
     * snapshot -- the vertex scratch window at 0x7FE000 among others -- so a
     * capture taken afterwards would replay a memory the game never had.
     *
     * At or after, once each, like the frame dump and for the same reason: a
     * trigger on an exact count one cannot predict fails silently when the run
     * stops short of it. */
    {
        // `DKR_CAPTURE_LIST=400` or `DKR_CAPTURE_LIST=100,400,900`. Each index
        // writes `D:\CAPnnnn.BIN`, the file naming the list it holds.
        //
        // **Several per run, and that is the point of the comma.** A capture
        // needs a boot, a launch and a wait; the corpus E09-S02 asks for is
        // title, menus, a lap of each level, cutscenes, split screen and
        // results, and taking them one boot at a time is a day. Firing on
        // several indices in one run costs nothing but disk.
        //
        // Eight at most: the transfer disk holds about fifty captures, and a
        // bound one can read here beats a run that fills the volume and reports
        // it as a write failure at the least useful moment.
        static const int kMaxCaptures = 8;
        static unsigned long cap_at[kMaxCaptures];
        static bool cap_done[kMaxCaptures];
        static int cap_count = -1;
        // **The list number is not a stable landmark, `gGameMode` is** -- the same
        // reasoning the frame dump below already carries, and the capture had not
        // been given it. How far the game has got by its four-hundredth display
        // list depends on load times and on how long a cutscene took; "twenty
        // lists after entering the menu" is the same moment in every run.
        //
        // That matters more for a capture than for a dump. A dump is looked at; a
        // capture is *replayed and compared*, and E09-S02's corpus is only worth
        // keeping if each of its scenes is a moment one can return to. It also
        // decides what the corpus can cover at all: since a capture ends the run
        // that takes it, each run buys exactly one scene, and naming that scene
        // by the game's own state is how the six the ticket asks for get chosen
        // rather than hunted.
        //
        // `DKR_CAPTURE_MODE=<n>` anchors: -1 INTRO, 0 INGAME, 1 MENU, 5 LOCKUP.
        // Without it the indices are absolute, as before.
        // **`DKR_CAPTURE_KEY=1`: capture what is on the screen now.**
        //
        // Anchoring on `gGameMode` names a moment the game defines, which is what
        // the corpus wants for scenes the game reaches on its own. It cannot name
        // a moment *inside* a mode: the vehicle-select screen and the race are
        // both preceded by MENU, and the screen one wants to look at is usually
        // one the game passes through on the way somewhere.
        //
        // The occasion for this: the timers on the vehicle-select screen render
        // shredded — the digits speckled through, where the same font in the race
        // HUD is clean — and there was no way to freeze that screen. A defect one
        // can see and cannot capture is a defect one cannot attribute, which is
        // the whole disease E09-S02 exists to cure.
        //
        // `F9` because it is bound to nothing: `runtime_platform.cpp` maps space,
        // shift, Z, return, the arrows, Q/E, IJKL and WASD, and a capture key that
        // also steers would fire while the player was driving.
        static const bool cap_on_key =
            (std::getenv("DKR_CAPTURE_KEY") != nullptr);
        static const char* const cap_mode_env = std::getenv("DKR_CAPTURE_MODE");
        static const bool cap_have_mode = (cap_mode_env != nullptr);
        static const int cap_wanted_mode =
            cap_mode_env ? static_cast<int>(std::strtol(cap_mode_env, nullptr, 10))
                         : 0;
        static unsigned long cap_anchor = 0UL;
        char path_key[32];
        if (cap_count < 0) {
            const char* const env = std::getenv("DKR_CAPTURE_LIST");
            cap_count = 0;
            for (const char* p = env; p != nullptr && *p != '\0';) {
                char* end = nullptr;
                const unsigned long v = std::strtoul(p, &end, 10);
                if (end == p) { break; }          // not a number: stop, do not spin
                if (v != 0UL && cap_count < kMaxCaptures) {
                    cap_at[cap_count++] = v;
                }
                p = (*end == ',') ? end + 1 : end;
                if (*end != ',' && *end != '\0') { break; }
            }
            if (cap_count > 0) {
                std::fprintf(stderr, "[gfx] capture: %d list(s) armed\n", cap_count);
            }
        }
        if (cap_on_key) {
            static bool key_shot = false;
            // **Say that it is armed.** The first run with this feature pressed
            // F9 on the screen it was built for, wrote nothing, and left three
            // candidate causes indistinguishable: the variable never reached the
            // program, the key never reached the window, or the write failed.
            // One line at arming and one line per key seen separate all three,
            // and the run that would have needed them had already been spent.
            static bool armed_said = false;
            if (!armed_said) {
                armed_said = true;
                std::fprintf(stderr, "[gfx] capture: F9 armed\n");
                dkr_diag_commit();
            }
            if (!key_shot && dkr_window_take_capture_request()) {
                key_shot = true;
                std::fprintf(stderr, "[gfx] capture: F9 at list %llu\n",
                             static_cast<unsigned long long>(index));
                /* Committed here too. The line above was added this morning so a
                   run could say whether the key arrived, and the first run to use
                   it wrote a capture and never printed it: the program is killed
                   rather than closed, and an uncommitted tail is a lost tail.
                   A diagnostic whose own output does not survive the run it
                   diagnoses is the thing it was written against. */
                dkr_diag_commit();
                std::sprintf(path_key, "D:\\CKEY%04lu.BIN",
                             static_cast<unsigned long>(index % 10000u));
                dkr_capture_write(path_key,
                                  task->t.data_ptr & 0x00FFFFFFu,
                                  rdram_snapshot, kSnapshotBytes,
                                  context_.rdram_native,
                                  static_cast<unsigned>(index),
                                  kWidth, kHeight);
            }
        }
        if (cap_have_mode && cap_anchor == 0UL && cap_count > 0) {
            if (read_word(rdram_snapshot, kAddrGameMode) == cap_wanted_mode) {
                cap_anchor = index;
                std::fprintf(stderr,
                             "[gfx] capture: gGameMode=%d reached at list %llu\n",
                             cap_wanted_mode,
                             static_cast<unsigned long long>(index));
            }
        }
        const bool cap_armed = cap_have_mode ? (cap_anchor != 0UL) : true;
        for (int c = 0; cap_armed && c < cap_count; ++c) {
            // At or after, once, like the frame dump: a trigger on an exact
            // count one cannot predict fails silently when the run stops short.
            if (!cap_done[c] && index >= cap_anchor + cap_at[c]) {
                char path[32];
                cap_done[c] = true;
                // Named by the moment, not by a counter, when one is given: a
                // letter for the mode and the offset within it. Two runs anchored
                // on different modes then cannot overwrite each other's scene,
                // which a bare `CAP0020.BIN` would.
                if (cap_have_mode) {
                    const char m = (cap_wanted_mode == -1) ? 'I'
                                 : (cap_wanted_mode ==  0) ? 'G'
                                 : (cap_wanted_mode ==  1) ? 'M'
                                 : (cap_wanted_mode ==  5) ? 'L' : 'X';
                    std::sprintf(path, "D:\\C%c%04lu.BIN", m, cap_at[c]);
                } else {
                    std::sprintf(path, "D:\\CAP%04lu.BIN", cap_at[c]);
                }
                dkr_capture_write(path,
                                  task->t.data_ptr & 0x00FFFFFFu,
                                  rdram_snapshot, kSnapshotBytes,
                                  context_.rdram_native,
                                  static_cast<unsigned>(index),
                                  kWidth, kHeight);
            }
        }

#if defined(DKR_TARGET_WIN95)
    if (g_render_zones_on) {
        const unsigned long long run_t0 = g_zone_clock();
        g_zone_in_run = 1;
        (void)dkr_f3d_run(&context_, task->t.data_ptr & 0x00FFFFFFu);
        g_zone_in_run = 0;
        g_zone_run_us += g_zone_clock() - run_t0;
    } else
#endif
    (void)dkr_f3d_run(&context_, task->t.data_ptr & 0x00FFFFFFu);

    // --- The canary -----------------------------------------------------------
    //
    // `DKR_CANARY=1` draws two hard-coded triangles into the game's own frame,
    // just before it is read back: identical in every respect except `oow`, one
    // at 1 and one at 0.00625, which is what the menu's orthographic lists give
    // every vertex.
    //
    // This is the ground truth no counter can supply. The chain has been
    // measured end to end -- 243 triangles submitted to `grDrawTriangle` against
    // 243 emitted -- and 429,306 pixels of on-screen surface come back as 2,857.
    // Every candidate has been eliminated by a switch. What has never been asked
    // is whether the card, in the state this list leaves it in, will draw a
    // triangle that the port knows to be correct.
    //
    // If both squares appear, the state is fine and the game's vertices carry
    // something bad. If neither does, the list leaves the card unable to draw.
    // If only one does, `oow` is the whole story -- and `DKR_FLATTEN_W=1`,
    // which sets it to 1 and paints nothing at all, says which way round.
    {
        static const int canary_mode = [] () -> int {
            const char* v = std::getenv("DKR_CANARY");
            if (v == nullptr) { return 0; }
            const long n = std::strtol(v, nullptr, 10);
            return static_cast<int>(n < 1 ? 1 : (n > 2 ? 2 : n));
        } ();
        if (canary_mode != 0 && opened_) {
            dkr_render_state st;
            dkr_render_vertex v[6];
            std::memset(&st, 0, sizeof(st));
            st.combine = DKR_COMBINE_SHADE;
            st.blend   = DKR_BLEND_OPAQUE;
            st.depth   = DKR_DEPTH_DISABLED;
            st.cull    = DKR_CULL_NONE;
            std::memset(v, 0, sizeof(v));
            {
                const float xs[6] = { 400.0F, 500.0F, 400.0F,
                                      400.0F, 500.0F, 400.0F };
                const float ys[6] = { 100.0F, 100.0F, 200.0F,
                                      250.0F, 250.0F, 350.0F };
                for (int i = 0; i < 6; i++) {
                    v[i].x = xs[i];
                    v[i].y = ys[i];
                    v[i].r = v[i].g = v[i].b = v[i].a = 255.0F;
                    v[i].z = 0.0F;
                    v[i].ooz = 0.0F;
                    // The upper triangle as a rectangle carries it, the lower as
                    // the game's geometry does.
                    v[i].oow = (i < 3) ? 1.0F : 0.00625F;
                    v[i].tmu[0][DKR_TMU_OOW] = v[i].oow;
                }
            }
            // `DKR_CANARY=2` draws without pushing a state at all, inheriting
            // whatever the list left on the card.
            //
            // The two facts to separate: the textured combiner never paints, and
            // the untextured one paints only if re-pushed before every triangle
            // -- a factor of forty for writing the same registers again. Both
            // point at the state the card actually holds between draws, which is
            // the one thing here that has only ever been inferred from what was
            // written to it.
            //
            // If this canary paints, the state a list leaves behind is drawable
            // and the decay reading is wrong. If it does not, the list leaves the
            // card unable to draw, and the next question is at what point.
            if (canary_mode < 2) {
                backend_.set_state(backend_.self, &st);
            }
            backend_.draw_triangles(backend_.self, v, 2);
        }
    }

    // --- The on-screen display (DKR_OSD) --------------------------------------
    //
    // Drawn before the frame dumps, so that a dump shows it: the passthrough
    // Voodoo's output reaches no capture, and a dump is how its legibility is
    // checked. The decoder does not push its state again at the start of a
    // list, so it is told its state is stale -- otherwise the next list's first
    // triangles would inherit the display's.
    if (osd_ && opened_) {
        if (clock_ready && t_entry - osd_t0_ >= 1000000ULL) { osd_refresh(t_entry); }
        const unsigned long long osd_start = clock_ready ? dkr_clock_now_us() : 0ULL;
        osd_->draw(backend_);
        context_.state_dirty = 1;
        if (clock_ready) {
            osd_draw_us_ += dkr_clock_now_us() - osd_start;
            osd_draws_++;
        }
    }

    // **One frame brought back, before it is presented.**
    //
    // A passthrough Voodoo drives the monitor through an analogue relay, so its
    // output appears in no capture the emulator can take. Every measurement of
    // this port's rendering has therefore been a counter -- triangles emitted,
    // combiners catalogued -- and counters say what was sent, never what came
    // out. `DKR_DUMP_FRAME=<n>` writes display list `n` to `D:\FRAME.BMP`.
    //
    // Read from the **back** buffer, before the swap: that is where this list
    // was just rasterised, and no flip timing enters into it. Reading the front
    // buffer after presenting returns whatever the retrace-scheduled swap has
    // got round to, which on 17 August 2026 cost a run and a wrong conclusion.
    //
    // It is E09-S02's missing half as much as a diagnostic: comparing the card's
    // output against the reference rasteriser needs the card's output in a file.
    //
    // **At or after, once** -- not on equality. The first version tested
    // `index == dump_at` with 600 asked for, and the run reached list 480: no
    // file, no log line, nothing to distinguish "the dump failed" from "the dump
    // never came up". A trigger that depends on reaching an exact count one
    // cannot predict is a trigger that fails silently.
    //
    // **Several lists, not one.** A single dump cannot tell "the game draws one
    // huge quad at this instant" from "the game draws one huge quad, full stop",
    // and that distinction is now the whole question: at list 200 the largest
    // triangle covered 734,776 pixels of a 307,200-pixel screen. `DKR_DUMP_EVERY`
    // repeats the dump, up to six files, so one run answers it.
    //
    // **The list number is not a stable landmark, `gGameMode` is.**
    //
    // Dumping at list 420 gives a different screen every run: how far the game
    // has got by its four-hundred-and-twentieth display list depends on load
    // times, on how long a cutscene took, on the machine's mood. Two runs of the
    // same build produced a menu one time and a black transition the next, which
    // makes a before-and-after comparison worthless -- and that is exactly what
    // was being attempted on the doubled glyphs.
    //
    // `DKR_DUMP_MODE=<n>` anchors on the game's own state variable instead:
    // -1 INTRO, 0 INGAME, 1 MENU, 5 LOCKUP. The counting starts when the mode is
    // first reached, so list 20 after entering the menu is list 20 after entering
    // the menu in every run. `DKR_DUMP_FRAME` keeps its old meaning when no mode
    // is given, since some questions really are about a list number.
    //
    // It is also what E09-S02's corpus needs: captures that can be replayed and
    // compared require a reproducible moment, not a reproducible counter.
    }

    {
        static const char* const dump_env = std::getenv("DKR_DUMP_FRAME");
        static const char* const every_env = std::getenv("DKR_DUMP_EVERY");
        static const char* const mode_env = std::getenv("DKR_DUMP_MODE");
        static const unsigned long dump_at =
            dump_env ? std::strtoul(dump_env, nullptr, 10) : 0UL;
        static const unsigned long dump_every =
            every_env ? std::strtoul(every_env, nullptr, 10) : 0UL;
        static const bool have_mode = (mode_env != nullptr);
        static const int wanted_mode =
            mode_env ? static_cast<int>(std::strtol(mode_env, nullptr, 10)) : 0;
        static const int kMaxDumps = 6;
        static int dumps_done = 0;
        static unsigned long next_dump = 0UL;
        static unsigned long mode_anchor = 0UL;   // list at which the mode arrived

        if (have_mode && mode_anchor == 0UL) {
            const int mode = read_word(rdram_snapshot, kAddrGameMode);
            if (mode == wanted_mode) {
                mode_anchor = index;
                std::fprintf(stderr,
                             "[gfx] frame dump: gGameMode=%d reached at list %llu\n",
                             mode, static_cast<unsigned long long>(index));
            }
        }
        // Without a mode the anchor is the start of the run, which reproduces
        // the old behaviour exactly.
        const bool armed = have_mode ? (mode_anchor != 0UL) : true;
        const unsigned long base = have_mode ? mode_anchor : 0UL;

        if (armed && dump_at != 0UL && dumps_done < kMaxDumps) {
            if (next_dump == 0UL) { next_dump = base + dump_at; }
            if (index >= next_dump) {
                char path[24];
                std::sprintf(path, "D:\\FRAME%d.BMP", dumps_done);
                std::fprintf(stderr, "[gfx] frame dump %d: list %llu\n",
                             dumps_done,
                             static_cast<unsigned long long>(index));
                dump_frame(path);
                dumps_done++;
                // A zero interval keeps the old single-shot behaviour rather
                // than dumping every list from here on, which would fill the
                // disk and slow the run - the mistake the per-list fprintf
                // already made once.
                next_dump = (dump_every != 0UL) ? index + dump_every : 0UL;
                if (next_dump == 0UL) { dumps_done = kMaxDumps; }
            }
        }
    }

    backend_.present(backend_.self);

    if (clock_ready) {
        const unsigned long long d = dkr_clock_now_us() - t_entry;
        render_us_total_ += d;
        render_hist_.add(d);
        osd_render_sum_ += d;
        osd_renders_++;
        if (d > osd_render_max_) { osd_render_max_ = d; }
        // FRAMES.BIN: period (0 for the first list after a pause), render, and
        // the display list's triangles emitted.
        timing_export_.add(static_cast<std::uint32_t>(t_entry / 1000ULL),
                           static_cast<std::uint32_t>(last_period_us_),
                           static_cast<std::uint32_t>(d),
                           static_cast<std::uint32_t>(context_.state.triangles));
        last_period_us_ = 0;
        render_n_++;
        g_display_lists_drawn.fetch_add(1, std::memory_order_relaxed);
        if (d > render_us_worst_) { render_us_worst_ = d; }
    }
    g_renderer_busy.store(0, std::memory_order_relaxed);

    for (int i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
        rejects_by_kind_[i] += context_.state.rejects[i];
    }
    for (int i = 0; i < 256; i++) { opcodes_[i] += context_.state.opcodes[i]; }
    total_rects_ += context_.state.rects;
    total_viewports_ += context_.state.viewports;
    total_tex_loaded_ += context_.state.textures_loaded;
    total_tex_reused_ += context_.state.textures_reused;
    total_tex_resident_ += context_.state.textures_resident;
    for (int i = 0; i < 4; i++) {
        emitted_per_cc_[i] += context_.state.emitted_per_cc[i];
    }
    total_emitted_uncatalogued_ += context_.state.emitted_uncatalogued;
    for (int i = 0; i < 8; i++) {
        tilesize_per_tile_[i] += context_.state.tilesize_per_tile[i];
    }
    tile_image_changes_[0] += context_.state.tile_image_changes[0];
    tile_image_changes_[1] += context_.state.tile_image_changes[1];
    total_tile1_distinct_ += context_.state.tile1_distinct;
    total_two_layer_ += context_.state.emitted_two_layer;
    total_tile1_unserved_ += context_.state.tile1_unserved;
    t1_entered_  += context_.state.tile1_entered;
    t1_cached_   += context_.state.tile1_cached;
    t1_resident_ += context_.state.tile1_resident;
    t1_uploaded_ += context_.state.tile1_uploaded;
    tt_with_     += context_.state.two_texel_with_layer;
    tt_without_  += context_.state.two_texel_without;
    total_tex_refused_ += context_.state.textures_refused;
    total_tex_padded_ += context_.state.textures_padded;
    total_emitted_textured_ += context_.state.emitted_textured;
    // **Accumulate every frame, not inside the reporting block.**
    //
    // The first version of this counter added inside the `if` that only prints
    // one list in sixty: it therefore saw one sixtieth of the frames, and the
    // total was sixty times too low. It was too low *consistently*, which is the
    // worst case -- 5,731 against 380,000 emitted reads as "blending is almost
    // never set" rather than as a sampling error.
    for (int m = 0; m < 8; m++) { blend_[m] += context_.state.emitted_per_blend[m]; }
    total_alpha_test_ += context_.state.emitted_alpha_test;
    for (int i = 0; i < 4; i++) {
        area_[i] += context_.state.area[i];
        depth_[i] += context_.state.emitted_per_depth[i];
    }
    for (int i = 0; i < DKR_COMBINE_COUNT; i++) {
        emitted_per_combine_[i] += context_.state.emitted_per_combine[i];
    }
    total_tex_aspect_ += context_.state.textures_bad_aspect;
    total_tex_unsupported_ += context_.state.textures.unsupported;
    total_tex_out_of_rdram_ += context_.state.textures.out_of_rdram;
    total_states_ += context_.state.states_applied;
    total_combiners_known_ += context_.state.combiners_known;
    total_combiners_unknown_ += context_.state.combiners_unknown;
    total_approximate_ += context_.state.states_approximate;
    total_fill_wrong_cycle_ += context_.state.fills_wrong_cycle;
    total_deferred_ += context_.state.deferred;
    total_commands_ += context_.state.commands;
    total_triangles_ += context_.state.triangles;
    total_emitted_ += context_.state.emitted;
    {
        int i;
        for (i = 0; i < DKR_F3D_REJECT_COUNT_MAX; i++) {
            total_rejects_ += context_.state.rejects[i];
        }
    }

    // A periodic report rather than a line per frame: the log goes to an
    // emulated floppy, and sixty lines a second saturate it.
    //
    // The three numbers are read together, and it is their gap that informs.
    // Many triangles and zero emitted points at clipping or culling; zero
    // triangles with commands points at the decoder; rising rejects point at
    // addressing, hence at the memory layout.
    if (index <= 3 || index % 60 == 0) {
        std::fprintf(stderr,
                     "[gfx] list=%llu cmd=%lu tri=%lu emitted=%lu rejects=%lu\n",
                     static_cast<unsigned long long>(index), total_commands_,
                     total_triangles_, total_emitted_, total_rejects_);
        // The reject total does not say what to fix: an address outside RDRAM
        // accuses the addressing, an unknown opcode the decoding, a vertex index
        // a command missed upstream. Counting them apart is what turns "it
        // rejects" into a lead.
        std::fprintf(stderr,
                     "[gfx]   deferred=%lu | address=%lu count=%lu index=%lu "
                     "depth=%lu opcode=%lu\n",
                     total_deferred_,
                     rejects_by_kind_[DKR_F3D_REJECT_ADDRESS],
                     rejects_by_kind_[DKR_F3D_REJECT_COUNT],
                     rejects_by_kind_[DKR_F3D_REJECT_INDEX],
                     rejects_by_kind_[DKR_F3D_REJECT_DEPTH],
                     rejects_by_kind_[DKR_F3D_REJECT_OPCODE]);
        // The eight most frequent opcodes, descending. Eight is enough: the
        // distribution is very uneven, and what we are after is what dominates
        // the frame, not the tail.
        {
            char line[160];
            int taken[8] = {0};
            int n = 0;
            std::size_t written = 0;
            line[0] = '\0';
            for (n = 0; n < 8; n++) {
                int best = -1;
                for (int op = 0; op < 256; op++) {
                    bool already = false;
                    for (int k = 0; k < n; k++) { already = already || (taken[k] == op); }
                    if (already || opcodes_[op] == 0) { continue; }
                    if (best < 0 || opcodes_[op] > opcodes_[best]) { best = op; }
                }
                if (best < 0) { break; }
                taken[n] = best;
                written += static_cast<std::size_t>(std::snprintf(
                    line + written, sizeof(line) - written, " %02X:%lu",
                    static_cast<unsigned>(best), opcodes_[best]));
                if (written >= sizeof(line) - 12) { break; }
            }
            std::fprintf(stderr, "[gfx]   opcodes%s\n", line);
        }
        // The drawing commands, named and looked for explicitly.
        //
        // The histogram's top eight are all RDP state, which leaves a question
        // the ranking does not settle: is there *any* drawing command in these
        // frames, or none? An absent opcode appears in no ranking, and "absent
        // from the top eight" reads far too easily as "rare" when it may be
        // zero.
        //
        // Zero everywhere would say the boot sequence draws nothing at all.
        // Rectangles without triangles would name the 2D path as the only work
        // left.
        std::fprintf(stderr,
                     "[gfx]   draw: vertices=%lu triangles=%lu texrect=%lu "
                     "texrectflip=%lu fillrect=%lu | handed-over=%lu colour=0x%06X "
                     "buffer=%u\n",
                     opcodes_[0x04], opcodes_[0x05], opcodes_[0xE4],
                     opcodes_[0xE5], opcodes_[0xF6], total_rects_,
                     context_.state.fill_color_argb,
                     context_.state.color_image_width);
        // The RDP state. `approximate` is the number to watch: an approximate
        // translation that does not announce itself produces a plausible, wrong
        // image, which is worse than a clean failure. `wrong-cycle` is a check
        // that fires on its own - the RDP only fills in FILL mode, so any other
        // value accuses the decoding of the mode word.
        std::fprintf(stderr,
                     "[gfx]   state: applied=%lu approximate=%lu "
                     "fills-wrong-cycle=%lu cycle=%u viewports=%lu\n",
                     total_states_, total_approximate_, total_fill_wrong_cycle_,
                     static_cast<unsigned>(context_.state.current_cycle),
                     total_viewports_);
        // The textures. `uploaded` against `reused` says whether the cache holds -
        // without it we would reconvert the same texture thousands of times per
        // frame, which alone would make the port unplayable. `unknown-format`
        // counts the indexed formats, refused for want of a palette: they come out
        // as untextured surfaces rather than in arbitrary colours.
        // --- The frame budget, and where it goes ----------------------------
        //
        // `period` is the frame; `render` is this file's share of it; the rest is
        // the recompiled game code, the scheduler and the audio. Announced as
        // `elsewhere` rather than left to a subtraction the reader has to do --
        // it is the number the go/no-go turns on, so it gets a name.
        if (period_n_ > 0 && render_n_ > 0) {
            const unsigned long long per = period_us_total_ / period_n_;
            const unsigned long long ren = render_us_total_ / render_n_;
            std::fprintf(stderr,
                         "[gfx]   frame: period=%lu us (%lu.%02lu fps) "
                         "render=%lu us elsewhere=%lu us\n",
                         static_cast<unsigned long>(per),
                         static_cast<unsigned long>(per ? 1000000ULL / per : 0ULL),
                         static_cast<unsigned long>(
                             per ? (100000000ULL / per) % 100ULL : 0ULL),
                         static_cast<unsigned long>(ren),
                         static_cast<unsigned long>(per > ren ? per - ren : 0ULL));
            std::fprintf(stderr, "[gfx]   frame-retraces:");
            for (int n = 0; n < 9; n++) {
                std::fprintf(stderr, " %d%s=%lu", n, n == 8 ? "+" : "", period_retraces_[n]);
            }
            std::fprintf(stderr, "\n");
            // Over a window of 600 display lists (about 20 s), not from boot.
            if (render_hist_.count() >= 600) {
                std::fprintf(stderr,
                             "[gfx]   frame-percentiles: window=%lu period p50=%llu p99=%llu us, "
                             "render p50=%llu p99=%llu us\n",
                             render_hist_.count(),
                             period_hist_.percentile(500), period_hist_.percentile(990),
                             render_hist_.percentile(500), render_hist_.percentile(990));
                period_hist_.reset();
                render_hist_.reset();
            }
            std::fprintf(stderr, "[gfx]   frame-bins-2ms:");
            for (int b = 0; b < 51; b++) { std::fprintf(stderr, " %lu", period_bins_[b]); }
            std::fprintf(stderr, "\n");
            std::fprintf(stderr,
                         "[gfx]   frame-worst: period=%lu us render=%lu us "
                         "samples=%lu dropped=%lu\n",
                         static_cast<unsigned long>(period_us_worst_),
                         static_cast<unsigned long>(render_us_worst_),
                         period_n_, period_dropped_);
        }
#if defined(DKR_TARGET_WIN95)
        if (g_render_zones_on) {
            /* Totals, not means: the reader divides by `lists`, and the zones
               add up to the renderer's own time only when read together. */
            std::fprintf(stderr, "[gfx]   zones: lists=%lu run=%llu decoder=%llu "
                                 "convert=%llu/%llu",
                         render_n_, zone_us(g_zone_run_us),
                         zone_us(g_zone_run_us - g_zone_run_backend_us -
                                 dkr_f3d_convert_ticks),
                         zone_us(dkr_f3d_convert_ticks), dkr_f3d_convert_n);
            for (int z = 0; z < kZoneCount; z++) {
                if (g_zone_n[z] != 0ULL) {
                    std::fprintf(stderr, " %s=%llu/%llu", kZoneNames[z],
                                 zone_us(g_zone_us[z]), g_zone_n[z]);
                }
            }
            std::fprintf(stderr, " us\n");
            /* Inclusive of the backend calls each command makes. Opcode, total
               microseconds, and the command count the decoder already keeps. */
            {
                static const char* const kTriangleZones[DKR_F3D_TRIANGLE_ZONES] = {
                    "batch", "corners", "clip", "project", "state",
                    "diagnostics", "draw", "cull-and-tail", "fetch", "trace-args",
                    "st-stats", "ndc-stats"};
                std::fprintf(stderr, "[gfx]   zones-triangle:");
                for (int z = 0; z < DKR_F3D_TRIANGLE_ZONES; z++) {
                    std::fprintf(stderr, " %s=%llu", kTriangleZones[z],
                                 zone_us(dkr_f3d_triangle_ticks[z]));
                }
                std::fprintf(stderr, " us\n");
            }
            std::fprintf(stderr, "[gfx]   zones-by-opcode:");
            for (int op = 0; op < 256; op++) {
                if (dkr_f3d_opcode_ticks[op] != 0ULL) {
                    std::fprintf(stderr, " %02X=%llu/%lu", op,
                                 zone_us(dkr_f3d_opcode_ticks[op]), opcodes_[op]);
                }
            }
            std::fprintf(stderr, " us\n");
        }
#endif
        std::fprintf(stderr,
                     "[gfx]   textures: uploaded=%lu reused=%lu resident=%lu "
                     "refused-tmu=%lu unknown-format=%lu outside-rdram=%lu\n",
                     total_tex_loaded_, total_tex_reused_, total_tex_resident_,
                     total_tex_refused_, total_tex_unsupported_, total_tex_out_of_rdram_);
        // What the residency query answered, and above all whether it ever
        // answered **stale** -- a descriptor naming a key the allocator had
        // evicted. That one would draw another texture's pattern, so it is
        // printed separately from the misses and a non-zero value is a defect
        // report rather than a cache statistic.
        {
            unsigned long lh = 0, lm = 0, ls = 0;
            dkr_glide_backend_lookup_stats(&lh, &lm, &ls);
            std::fprintf(stderr,
                         "[gfx]   lookup: hits=%lu misses=%lu stale=%lu\n",
                         lh, lm, ls);
        }
        // What the conversions cost this list, and how much of it repeats. The
        // hit rate of a one-entry cache says nothing about how big a real one
        // would have to be; the distinct count does.
        std::fprintf(stderr,
                     "[gfx]   conversions: texels=%lu distinct-keys=%u"
                     " overflow=%lu repeats=%lu\n",
                     context_.state.conversion_texels,
                     context_.state.distinct_keys,
                     context_.state.distinct_overflow,
                     context_.state.distinct_repeats);
        // --- What the card was actually asked to swallow --------------------
        //
        // Everything above counts what the *decoder* did: `uploaded` is the
        // number of times a tile was converted and handed down, `reused` the
        // hits on the one-entry cache in front of it. Neither says whether the
        // texture then had to cross the PCI bus, and that is the quantity a
        // Voodoo 2 is limited by -- 64 KiB moved inside a 16 ms frame is a
        // hitch felt on the controller, not a line in a log.
        //
        // The allocator has kept `hits`, `misses`, `downloads` and
        // `download_bytes` since E05-S02, `tmu.h` says in as many words that the
        // ticket wants them readable in-game, and until now the only thing that
        // ever read them was a witness. So the claim this port has been resting
        // on since 25 August -- "the allocator keeps the texture resident, so a
        // key that comes back is a hit and costs no download" -- had never been
        // measured on the game.
        {
            int u;
            for (u = 0; u < dkr_glide_backend_tmu_count(); u++) {
                const dkr_tmu* t = dkr_glide_backend_tmu(u);
                if (t == nullptr) { continue; }
                std::fprintf(stderr,
                             "[gfx]   tmu%d: hits=%lu/%lu downloads=%lu "
                             "bytes=%lu evict=%lu fail=%lu peak=%luK\n",
                             u, t->stats.hits, t->stats.hits + t->stats.misses,
                             t->stats.downloads, t->stats.download_bytes,
                             t->stats.evictions, t->stats.failures,
                             t->stats.peak_bytes / 1024u);
                // **The shape of what is free, beside how much of it there is.**
                //
                // `peak` is a total and a total cannot tell a full unit from a
                // broken one. The race measured on 17 September evicted 1,040
                // times at 57 % occupancy with zero memory refusals, which is
                // what a buddy allocator does when it holds plenty of room in
                // pieces smaller than the order being asked for. `largest` far
                // below `free` says that outright; `largest` close to `free`
                // says the fragmentation theory is wrong and the search must
                // move on.
                {
                    unsigned int total = 0u, largest = 0u, blocks = 0u;
                    dkr_tmu_free_shape(t, &total, &largest, &blocks);
                    std::fprintf(stderr,
                                 "[gfx]   tmu%d: free=%uK largest=%uK "
                                 "blocks=%u\n",
                                 u, total / 1024u, largest / 1024u, blocks);
                }
            }
        }
        std::fprintf(stderr,
                     "[gfx]   refusal-detail: aspect=%lu size=%lu "
                     "slots=%lu tmu-memory=%lu reclaimed=%lu\n",
                     dkr_glide_backend_upload_failure(0),
                     dkr_glide_backend_upload_failure(1),
                     dkr_glide_backend_upload_failure(2),
                     dkr_glide_backend_upload_failure(3),
                     dkr_glide_backend_slots_reclaimed());
        // **Does a key ever come back?** Cumulative across frames, which the
        // decoder's own distinct-key figures cannot be: `dkr_f3d_init` memsets
        // the context once per display list, and reading those per-frame numbers
        // against the backend's cumulative ones is what made an earlier entry in
        // E05-S02 wrong. If `matches` stays near zero while `uploads` climbs,
        // keys never recur and the residency cache reporting no hits is being
        // asked for something impossible rather than failing at something
        // possible.
        {
            unsigned long matches = 0, uploads = 0;
            dkr_glide_backend_key_recurrence(&matches, &uploads);
            std::fprintf(stderr,
                         "[gfx]   key recurrence: matches=%lu of %lu uploads\n",
                         matches, uploads);
            unsigned long long kf = 0, kl = 0;
            dkr_glide_backend_key_samples(&kf, &kl);
            std::fprintf(stderr,
                         "[gfx]   upload keys: first=%08lX%08lX last=%08lX%08lX\n",
                         (unsigned long)(kf >> 32), (unsigned long)(kf & 0xFFFFFFFFull),
                         (unsigned long)(kl >> 32), (unsigned long)(kl & 0xFFFFFFFFull));
        }
        // Where this frame's textures sat in RDRAM. Two reports from the same
        // screen say whether the address the key is built on holds still.
        std::fprintf(stderr,
                     "[gfx]   timg: first=0x%06X lo=0x%06X hi=0x%06X"
                     " key=%08lX%08lX\n",
                     context_.state.timg_first, context_.state.timg_lo,
                     context_.state.timg_hi,
                     (unsigned long)(context_.state.timg_first_key >> 32),
                     (unsigned long)(context_.state.timg_first_key & 0xFFFFFFFFull));
        std::fprintf(stderr,
                     "[gfx]   padded-to-power-of-2=%lu "
                     "refused-aspect=%lu\n",
                     total_tex_padded_, total_tex_aspect_);
        // Which shapes the 8:1 rule reaches, and what padding them costs. The
        // rule used to refuse them on a stated cost of "eight times memory",
        // and a factor is not a quantity.
        if (context_.state.aspect_padded_n != 0) {
            char shapes[128];
            unsigned pos = 0;
            for (unsigned i = 0; i < context_.state.aspect_padded_n; i++) {
                const int n = std::snprintf(
                    shapes + pos, sizeof(shapes) - pos, "%s%ux%u",
                    (i == 0) ? "" : " ",
                    static_cast<unsigned>(context_.state.aspect_padded_dims[i][0]),
                    static_cast<unsigned>(context_.state.aspect_padded_dims[i][1]));
                if (n <= 0 || pos + static_cast<unsigned>(n) >= sizeof(shapes)) {
                    break;
                }
                pos += static_cast<unsigned>(n);
            }
            std::fprintf(stderr,
                         "[gfx]   aspect-padded shapes: %s | count=%lu "
                         "texels=%lu\n",
                         shapes, context_.state.textures_aspect_padded,
                         context_.state.aspect_padded_texels);
        }
        // The normalised coordinates. A neighbourhood of [0,1] confirms the 10.5
        // format and the width in use; thousands refute it.
        std::fprintf(stderr,
                     "[gfx]   emitted: textured=%lu | shade=%lu texel=%lu "
                     "texel*shade=%lu texel*shade+a=%lu\n",
                     total_emitted_textured_, emitted_per_combine_[0],
                     emitted_per_combine_[1], emitted_per_combine_[2],
                     emitted_per_combine_[3]);
        // Which catalogue category paints the frame -- the number that decides
        // whether the second TMU (E05-S04) is worth building, and the only one
        // that separates "exact" from "the table says this needs two texels".
        // E05-S04's precondition, before its implementation: does the game ever
        // supply a second texel? A tile-1 sizing naming a different texture
        // image than tile 0 holds is one; zero of them would mean the two-texel
        // configurations have nothing to read from.
        std::fprintf(stderr,
                     "[gfx]   tiles: sized=[%lu %lu %lu %lu %lu %lu %lu %lu] "
                     "img-changes=%lu/%lu tile1-distinct=%lu\n",
                     tilesize_per_tile_[0], tilesize_per_tile_[1],
                     tilesize_per_tile_[2], tilesize_per_tile_[3],
                     tilesize_per_tile_[4], tilesize_per_tile_[5],
                     tilesize_per_tile_[6], tilesize_per_tile_[7],
                     tile_image_changes_[0], tile_image_changes_[1],
                     total_tile1_distinct_);
        // E05-S04, from both ends. `states` is what the backend programmed,
        // `triangles` what the decoder handed over with coordinates for the
        // second unit; one being zero while the other is not is a defect with an
        // address rather than a puzzle. `unserved` counts the tile-1 sizings
        // dropped -- a refused upload, or a card with one unit.
        std::fprintf(stderr,
                     "[gfx]   two-layer: states=%lu triangles=%lu unserved=%lu | "
                     "tile1 in=%lu cached=%lu resident=%lu uploaded=%lu | "
                     "two-texel with-layer=%lu without=%lu\n",
                     dkr_glide_backend_two_layer_states(),
                     total_two_layer_, total_tile1_unserved_,
                     t1_entered_, t1_cached_, t1_resident_, t1_uploaded_,
                     tt_with_, tt_without_);
        std::fprintf(stderr,
                     "[gfx]   painted-by: exact=%lu multipass=%lu "
                     "approximate=%lu two-texel=%lu uncatalogued=%lu\n",
                     emitted_per_cc_[0], emitted_per_cc_[1],
                     emitted_per_cc_[2], emitted_per_cc_[3],
                     total_emitted_uncatalogued_);
        std::fprintf(stderr,
                     "[gfx]   areas: <1px=%lu <100px=%lu <10000px=%lu "
                     ">=10000px=%lu\n",
                     area_[0], area_[1], area_[2], area_[3]);
        std::fprintf(stderr,
                     "[gfx]   depth: mode0=%lu mode1=%lu mode2=%lu mode3=%lu\n",
                     depth_[0], depth_[1], depth_[2],
                     depth_[3]);
        {
            char l2[128];
            std::size_t e2 = 0;
            int m;
            l2[0] = '\0';
            for (m = 0; m < 8; m++) {
                if (blend_[m] != 0) {
                    e2 += static_cast<std::size_t>(std::snprintf(
                        l2 + e2, sizeof(l2) - e2, " %d:%lu", m, blend_[m]));
                }
            }
            std::fprintf(stderr,
                         "[gfx]   blend:%s | alpha-test=%lu ref-max=%u\n",
                         l2, total_alpha_test_, context_.state.alpha_ref_max);
        }
        if (context_.state.oow_max > context_.state.oow_min) {
            total_tex_black_ += context_.state.textures_black;
            total_tex_with_content_ += context_.state.textures_with_content;
            std::fprintf(stderr, "[gfx]   texels: black=%lu with-content=%lu\n",
                         total_tex_black_, total_tex_with_content_);
            std::fprintf(stderr, "[gfx]   shade-max=%d alpha-max=%d\n",
                         static_cast<int>(context_.state.shade_max),
                         static_cast<int>(context_.state.alpha_max));
            std::fprintf(stderr, "[gfx]   oow=[%d..%d]/1000000\n",
                         static_cast<int>(context_.state.oow_min * 1000000.0F),
                         static_cast<int>(context_.state.oow_max * 1000000.0F));
        }
        {
            char line[128];
            std::size_t written = 0;
            unsigned i;
            line[0] = '\0';
            for (i = 0; i < context_.state.unknown_keys_n && written < 100; i++) {
                written += static_cast<std::size_t>(std::snprintf(
                    line + written, sizeof(line) - written, " %08X",
                    static_cast<unsigned>(context_.state.unknown_keys[i])));
            }
            std::fprintf(stderr,
                         "[gfx]   combiners: catalogued=%lu unknown=%lu%s%s\n",
                         total_combiners_known_, total_combiners_unknown_,
                         (line[0] != '\0') ? " keys:" : "", line);
            // The composition, in the (a,b,c,d) form `gDPSetCombineLERP` takes -
            // that is the G_CC_* macros' form, hence the one that allows the
            // configuration to be named and added to the table.
            for (i = 0; i < context_.state.unknown_keys_n; i++) {
                const dkr_combiner& k = context_.state.unknown_combiners[i];
                std::fprintf(stderr,
                             "[gfx]     %08X cycle=%u rgb0=(%u,%u,%u,%u) "
                             "a0=(%u,%u,%u,%u) rgb1=(%u,%u,%u,%u) "
                             "a1=(%u,%u,%u,%u)\n",
                             static_cast<unsigned>(context_.state.unknown_keys[i]),
                             context_.state.unknown_cycle[i],
                             k.rgb[0].a, k.rgb[0].b, k.rgb[0].c, k.rgb[0].d,
                             k.alpha[0].a, k.alpha[0].b, k.alpha[0].c, k.alpha[0].d,
                             k.rgb[1].a, k.rgb[1].b, k.rgb[1].c, k.rgb[1].d,
                             k.alpha[1].a, k.alpha[1].b, k.alpha[1].c, k.alpha[1].d);
            }
        }
        // The game's state, read where it lives. It is the only measurement in
        // this series that does not speak of the rendering.
        {
            const int mode = read_word(rdram_snapshot, kAddrGameMode);
            static const char* names[] = { "INGAME", "MENU", "UNUSED2",
                                           "UNUSED3", "UNUSED4", "LOCKUP" };
            const char* name = (mode == -1) ? "INTRO"
                             : (mode >= 0 && mode <= 5) ? names[mode] : "?";
            std::fprintf(stderr,
                         "[game] gGameMode=%d (%s) loading=%d level=0x%08X\n",
                         mode, name, read_word(rdram_snapshot, kAddrLevelLoadTimer),
                         static_cast<unsigned>(
                             read_word(rdram_snapshot, kAddrLevelHeader)));
        }
        if (context_.state.s_max > context_.state.s_min) {
            std::fprintf(stderr,
                         "[gfx]   coords: s=[%d..%d]/1000 t=[%d..%d]/1000\n",
                         static_cast<int>(context_.state.s_min * 1000.0F),
                         static_cast<int>(context_.state.s_max * 1000.0F),
                         static_cast<int>(context_.state.t_min * 1000.0F),
                         static_cast<int>(context_.state.t_max * 1000.0F));
        }
        // **And commit what has just been written.** Everything above reaches
        // the write-behind cache and nothing reaches the directory entry, which
        // Windows 95 updates only at close -- so a run stopped from the outside
        // leaves `DKRR.LOG` at zero bytes and every counter in it unreadable.
        //
        // Once per report, that is once per sixty lists, is the right rate: it
        // is the rate at which there is anything new to read, and a commit per
        // line would put a `FlushFileBuffers` between two `fprintf`s on a floppy
        // controller's emulated timing.
        dkr_diag_commit();
    }
#else
    (void)task;
    (void)index;
#endif
}

void dkr::runtime::GlideRenderer::update_screen() {
    /* Counted as the renderer being busy, like `send_dl`.
     *
     * The scheduler's idle accounting charges a wait to "the renderer" or to
     * "something else" by asking this flag, and on 21 September 2026 more than
     * half the wait came back as "something else". Part of that bucket is this
     * function: it runs on the graphics thread, outside the display-list path
     * that `render_us_total_` measures, and was therefore invisible to both
     * accounts at once. */
    struct BusyWhileHere {
        BusyWhileHere() { g_renderer_busy.store(1, std::memory_order_relaxed); }
        ~BusyWhileHere() { g_renderer_busy.store(0, std::memory_order_relaxed); }
    } busy_while_here;
    const ExclusiveSection exclusive;

    const auto index = ++present_count_;
    if (index == 1) {
        // The first refresh is only queued once ultramodern's VI thread has
        // installed its dummy mode. Starting DKR here keeps the game thread from
        // racing that setup.
        std::fprintf(stderr, "[boot] VI initialized; starting recompiled DKR entrypoint\n");
        recomp::start_game(kGameId);
    }
    if (index <= 10 || index % 300 == 0) {
        std::fprintf(stderr, "[boot][vi] present=%llu\n",
                     static_cast<unsigned long long>(index));
        // Committed here as well as in the display-list report, and the reason is
        // a question this line is meant to answer: the game stops submitting
        // lists around 300 and nothing says whether the *rest* of the runtime
        // stops with it. `dkr_diag_commit` ran only from the list report, so once
        // the lists ceased the log ceased being written to disk, and a VI thread
        // still presenting looked exactly like a VI thread that had stopped.
        //
        // Windows 95 updates a file's directory entry at close, and this program
        // is normally killed rather than closed, so an uncommitted tail is a lost
        // tail. See `diagnostic_log.hpp`.
        dkr_diag_commit();
    }
}

void dkr::runtime::GlideRenderer::shutdown() {
#if defined(DKR_TARGET_WIN95)
    if (opened_ && backend_.close != nullptr) {
        backend_.close(backend_.self);
        opened_ = false;
        // The card holds the screen through an analogue relay: not closing the
        // context leaves the monitor on the 3dfx output, black screen, with no
        // error message visible at all.
        std::fprintf(stderr, "[gfx] Glide closed\n");
    }
#endif
}

std::uint32_t dkr::runtime::GlideRenderer::get_display_framerate() const {
    return 60;
}

float dkr::runtime::GlideRenderer::get_resolution_scale() const {
    return 1.0F;
}

std::unique_ptr<ultramodern::renderer::RendererContext> dkr::runtime::CreateGlideRenderer(
    std::uint8_t*, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<GlideRenderer>();
}
