/* E05-S01 — bringing Glide up: context, buffers, presentation, shutdown.
 *
 * Scope. This layer opens the card and hands back control; it does not draw.
 * Textures (E05-S02), the combiner (E05-S03) and rendering proper come later.
 * Nor does it implement the E04-S01 backend interface, which does not exist yet:
 * that is deliberate, and it is written here so nobody believes E05-S01 is
 * finished. The day that interface arrives, it will fit on top without anything
 * below changing.
 *
 * ## Glide is loaded dynamically, and that is not a detail
 *
 * `glide2x.dll` comes from the 3dfx driver, not from the system. Linking against
 * its import library would make it mandatory at load time: on a machine with no
 * 3dfx card, Windows would refuse to start the program while naming a symbol,
 * which teaches the player nothing. Loaded through `LoadLibrary`, its absence
 * becomes a sentence one can write.
 *
 * ## Nothing is hard-coded
 *
 * The number of TMUs and the available memory drive E05-S02 and E05-S04 at run
 * time. They are therefore measured, not assumed — and ADR 0002 mandates two
 * TMUs without forbidding us from finding only one, in which case E05-S04's
 * multipass fallback applies.
 *
 * A measured trap, worth knowing before writing any detection at all:
 * **`grSstQueryHardware` does not tell a Voodoo 1 from a Voodoo 2**. The
 * returned type is 0 for both, and the FBI revision is identical — measured on
 * the test machine, see `docs/research/win95-voodoo2-machine.md`. It is the
 * number of TMUs and the memory per TMU that decide, and those are precisely the
 * only two things the engine needs.
 */
#ifndef DKR_RENDER_GLIDE_H
#define DKR_RENDER_GLIDE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DKR_GLIDE_OK = 0,
    DKR_GLIDE_ERR_NO_LIBRARY,   /* glide2x.dll not found */
    DKR_GLIDE_ERR_NO_SYMBOL,    /* the DLL is there but incomplete */
    DKR_GLIDE_ERR_NO_BOARD,     /* no 3dfx card */
    DKR_GLIDE_ERR_NO_MEMORY,    /* the configuration does not fit in the card */
    DKR_GLIDE_ERR_OPEN          /* grSstWinOpen refused */
} dkr_glide_result;

/* The text is meant for the player, and it names the possible action: "no 3dfx
   card detected" is fixed differently from "glide2x.dll not found". */
const char *dkr_glide_result_text(dkr_glide_result r);

/* What the card says about itself. Filled in by `dkr_glide_detect`. */
typedef struct {
    unsigned  glide_version;    /* 0x254 for Glide 2.54 */
    int       board_count;
    int       tmu_count;
    unsigned  fb_memory_kb;     /* frame-buffer memory */
    unsigned  tmu_memory_kb[3]; /* per TMU, in order */
    int       sli;
} dkr_glide_hardware;

/* Loads the library, resolves the symbols, queries the hardware.
   Opens no context and does not touch the display: it can therefore be called
   to show a diagnostic and then give up. */
dkr_glide_result dkr_glide_detect(dkr_glide_hardware *out);

/* Resolutions, in the order the fallback tries them. */
typedef enum {
    DKR_GLIDE_RES_640x480 = 0,
    DKR_GLIDE_RES_512x384,
    DKR_GLIDE_RES_400x300,
    DKR_GLIDE_RES_320x240,
    DKR_GLIDE_RES_COUNT
} dkr_glide_resolution;

typedef struct {
    dkr_glide_resolution resolution;  /* the one actually obtained */
    int width, height;
    int buffers;                      /* 2 = double buffered */
    int depth_buffer;                 /* 1 if a depth buffer is present */
} dkr_glide_context;

/* Opens the context at `wanted`, or at the first lower resolution that fits in
 * the measured memory. `out` receives what was obtained, which may differ from
 * what was asked for — the caller must read it rather than assume it.
 *
 * The fallback is computed here and not by trial and error on the hardware: when
 * `grSstWinOpen` fails, we do not know *why*, and a message saying "640x480 does
 * not fit in 2 MB" beats a silent refusal. */
dkr_glide_result dkr_glide_open(dkr_glide_resolution wanted,
                                dkr_glide_context *out);

/* Clears the back buffer then swaps it. `argb` is the clear colour. */
void dkr_glide_clear(unsigned argb);
void dkr_glide_swap(void);

/* Closes the context and **restores the display mode**.
 *
 * On a Voodoo 1 or 2, the card takes over full screen through an analogue relay:
 * as long as the context is open, the monitor shows the 3dfx output and not the
 * 2D card's. A missed shutdown therefore leaves the screen black, and only a
 * reboot recovers it — which is very costly at the exact moment when crashes are
 * frequent.
 *
 * That is why `dkr_glide_open` registers this function with E02-S03's abnormal
 * termination registry: the exception filter calls it back before displaying
 * anything at all. Calling it twice has no effect. */
void dkr_glide_shutdown(void);

/* --- Reading back what the card drew ---------------------------------------- *
 *
 * On a passthrough Voodoo, the screen belongs to the card: **no emulator capture
 * shows the 3dfx output**, and everything the project claimed about Glide
 * rendering so far rested on the absence of a crash.
 *
 * `grLfbLock` gives access to the frame buffer. That unlocks three things at
 * once, and it is why this function is worth writing:
 *
 *   - the visual check of E05-S01's triangle, partial until now;
 *   - measuring the off-screen margin Glide tolerates (E04-S05);
 *   - comparison against the reference rasteriser (E09-S02), which is the whole
 *     reason the oracle exists.
 *
 * `out` receives 32-bit ARGB, rows top to bottom — the software rasteriser's
 * format, so that the two images compare with no conversion. The card itself
 * works in 565: the conversion replicates the high bits, without which white
 * would not be white and every comparison would drift.
 *
 * Returns the number of pixels read, or zero. */
int dkr_glide_read_framebuffer(unsigned *out, int max_pixels,
                               int *width, int *height);

/* --- What the backend layer needs ------------------------------------------- *
 *
 * `glide_backend.c` programs the state registers — blending, depth, scissor, fog
 * — which this layer has no business knowing about. Rather than duplicating the
 * DLL loading over there, we expose symbol resolution.
 *
 * Returns NULL if the library is not loaded or if the symbol is missing, which
 * is a normal case: Glide 2.4 does not export everything Glide 2.6 exports, and
 * a state that cannot be programmed must degrade, not crash. */
void *dkr_glide_symbol(const char *decorated_name);

/* Three vertices already in `GrVertex` layout, handed to `grDrawTriangle` as
   they are. The type stays opaque here: `backend.h` is not included by this
   layer, and the layout assertion lives in `backend_layout_check.c`. */
void dkr_glide_draw_raw(const void *a, const void *b, const void *c);

/* A full-screen Gouraud triangle, to prove the chain reaches the pixel. Its
   place here is provisional: it will go away when E04-S01 provides a real
   drawing interface. */
void dkr_glide_draw_test_triangle(void);

#ifdef __cplusplus
}
#endif

#endif /* DKR_RENDER_GLIDE_H */
