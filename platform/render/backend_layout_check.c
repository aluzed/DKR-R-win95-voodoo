/* E04-S01 — the interface's two promises, checked at compile time.
 *
 * `backend.h` asserts two things everything else depends on, and that a comment
 * does not protect:
 *
 *   1. `dkr_render_vertex` has the layout of Glide 2.x's `GrVertex`, so that the
 *      backend can hand the vertex address to `grDrawTriangle` with no
 *      conversion at all;
 *   2. `dkr_render_state` is comparable with `memcmp`, so that the backend only
 *      emits the differences.
 *
 * The first breaks silently: reordering two fields compiles perfectly and
 * renders permuted colours, Glide reading the floats at the wrong offsets —
 * measured in E09-S01, where a red vertex came out green. The second breaks by
 * adding a misaligned field, which introduces padding whose contents are
 * indeterminate: `memcmp` then reports differences that are not differences, and
 * state tracking re-emits on every call.
 *
 * Both are therefore checked here, at compile time. This file produces no code.
 */
#include "backend.h"

#include <stddef.h>

#if defined(__cplusplus)
#define DKR_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define DKR_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define DKR_STATIC_ASSERT(cond, msg) \
    typedef char dkr_assert_##__LINE__[(cond) ? 1 : -1]
#endif

/* --- 1. The `GrVertex` layout ---------------------------------------------- *
 *
 * Copied from 3dfx's `glide.h`. The order is not intuitive — `ooz` and `a` sit
 * between the colours and `oow` — and that is precisely why it has to be checked
 * rather than assumed.
 */
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, x)   ==  0, "GrVertex.x");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, y)   ==  4, "GrVertex.y");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, z)   ==  8, "GrVertex.z");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, r)   == 12, "GrVertex.r");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, g)   == 16, "GrVertex.g");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, b)   == 20, "GrVertex.b");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, ooz) == 24, "GrVertex.ooz");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, a)   == 28, "GrVertex.a");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, oow) == 32, "GrVertex.oow");
DKR_STATIC_ASSERT(offsetof(dkr_render_vertex, tmu) == 36, "GrVertex.tmuvtx");
DKR_STATIC_ASSERT(sizeof(dkr_render_vertex) == 36 + 3 * 4 * 4,
                  "GrVertex is 84 bytes; any difference breaks grDrawTriangle");

/* --- 2. The state block, free of padding ----------------------------------- *
 *
 * The sum of the fields must equal the size of the structure. If the compiler
 * inserts a padding byte the equality fails and the check fails with it — which
 * is the point: it is exactly that padding, never initialised, that would make
 * `memcmp` lie.
 */
/* Every field is counted **by its name**, and that is what makes the check
 * useful. A first version added up types — "four `unsigned char`" — and the
 * self-test caught it out: removing `pad_` left the sum unchanged, the compiler
 * putting back exactly the byte that had just been taken away. A check that
 * counts types also counts the padding it is looking for. */
#define DKR_FIELD_SIZE(f) sizeof(((dkr_render_state *)0)->f)

DKR_STATIC_ASSERT(
    sizeof(dkr_render_state) ==
        DKR_FIELD_SIZE(combine)   + DKR_FIELD_SIZE(blend) +
        DKR_FIELD_SIZE(constant_color) +
    DKR_FIELD_SIZE(recipe) +
    DKR_FIELD_SIZE(recipe_pad) +
        DKR_FIELD_SIZE(depth)     + DKR_FIELD_SIZE(cull) +
        DKR_FIELD_SIZE(filter)    + DKR_FIELD_SIZE(wrap_s) +
        DKR_FIELD_SIZE(wrap_t)    + DKR_FIELD_SIZE(alpha_test) +
        DKR_FIELD_SIZE(alpha_reference) + DKR_FIELD_SIZE(fog_enabled) +
        DKR_FIELD_SIZE(pad_)      + DKR_FIELD_SIZE(fog_color) +
        DKR_FIELD_SIZE(texture)  + DKR_FIELD_SIZE(texture1),
    "dkr_render_state carries padding: memcmp would compare indeterminate "
    "bytes and state tracking would re-emit on every call");
