/* E04-S02 — the display-list decoder test.
 *
 * The criterion asks for range validation to be "tested by injecting
 * deliberately corrupted display lists". That is exactly what this suite does,
 * and it is what makes it possible without the ROM: a corrupt display list is
 * written, a real one is captured.
 *
 * What is established: **every form of invalid input produces a circumscribed,
 * named rejection, and leaves the decoder able to carry on**. A decoder that
 * crashes on bad data is a decoder a modified ROM brings to its knees; a decoder
 * that accepts it in silence addresses host memory.
 */
#include "render/f3ddkr.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "FAIL ", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "FAIL ", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

/* A small test RDRAM: out-of-bounds ranges are then easy to build, and the
   behaviour is the same as with 8 MiB. */
#define RAM_SIZE 4096u
static unsigned char g_ram[RAM_SIZE];

static void put32(unsigned int a, unsigned int v)
{
    g_ram[a + 0] = (unsigned char)(v >> 24);
    g_ram[a + 1] = (unsigned char)(v >> 16);
    g_ram[a + 2] = (unsigned char)(v >>  8);
    g_ram[a + 3] = (unsigned char)(v);
}

/* The last rectangle asked of the backend, for the 2D path test. */
static int      g_rect[4];
static unsigned g_rect_argb;
static int      g_rect_n;

static void note_rect(void *self, int x0, int y0, int x1, int y1, unsigned argb)
{
    (void)self;
    g_rect[0] = x0; g_rect[1] = y0; g_rect[2] = x1; g_rect[3] = y1;
    g_rect_argb = argb;
    g_rect_n++;
}

static void put16(unsigned int a, int v)
{
    g_ram[a] = (unsigned char)((unsigned)v >> 8);
    g_ram[a + 1] = (unsigned char)v;
}

static unsigned int put_cmd(unsigned int a, unsigned int w0, unsigned int w1)
{
    put32(a, w0);
    put32(a + 4, w1);
    return a + 8;
}

/* Trace mode, captured so that it can be checked. */
static char g_trace[64][192];
static int  g_trace_count;

static void trace_sink(void *user, const char *line)
{
    (void)user;
    if (g_trace_count < 64) {
        strncpy(g_trace[g_trace_count], line, sizeof(g_trace[0]) - 1);
        g_trace[g_trace_count][sizeof(g_trace[0]) - 1] = '\0';
        g_trace_count++;
    }
}

static int trace_contains(const char *needle)
{
    int i;
    for (i = 0; i < g_trace_count; i++) {
        if (strstr(g_trace[i], needle)) { return 1; }
    }
    return 0;
}

static void reset(dkr_f3d_context *c, int with_trace)
{
    memset(g_ram, 0, sizeof(g_ram));
    g_trace_count = 0;
    dkr_f3d_init(c, g_ram, RAM_SIZE, NULL);
    if (with_trace) {
        c->trace = trace_sink;
    }
}

int main(void)
{
    dkr_f3d_context c;
    unsigned int a;

    g_out = fopen("D:\\F3DDKR.TXT", "w");

    /* --- A well-formed list -------------------------------------------------- */
    reset(&c, 1);
    a = put_cmd(0, 0xBF000100u, 0x00000200u);          /* DMAOffsets */
    a = put_cmd(a, 0xB8000000u, 0x00000000u);          /* EndDisplayList */
    (void)a;
    check("a well-formed list runs", dkr_f3d_run(&c, 0) == 2);
    check("the DMA bases are kept",
          c.state.matrix_offset == 0x000100u && c.state.vertex_offset == 0x000200u);
    check("trace mode logs the commands",
          trace_contains("DMAOffsets") && trace_contains("EndDisplayList"));

    /* --- Vertices: the three bounding conditions ---------------------------- *
     *
     * The third — a batch that overflows the cache through the sum of the
     * destination and the count — is the one that gets forgotten, and it is the
     * one that allows writing past the cache. */
    reset(&c, 1);
    /* 32 vertices at index 16: the batch overflows by 16. */
    a = put_cmd(0, 0x04F82000u | (16u << 9), 0x00000000u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("a vertex batch that overflows the cache is rejected",
          c.state.rejects[DKR_F3D_REJECT_COUNT] == 1 && c.state.vertices == 0);

    /* A source outside RDRAM. */
    reset(&c, 1);
    a = put_cmd(0, 0xBF000000u, RAM_SIZE - 4u);        /* vertex base at the edge */
    a = put_cmd(a, 0x04080000u, 0x00000000u);          /* 2 vertices => 20 bytes */
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("vertices outside RDRAM are rejected",
          c.state.rejects[DKR_F3D_REJECT_ADDRESS] >= 1 && c.state.vertices == 0);

    /* --- Triangles: an index outside the cache ------------------------------ *
     *
     * And above all: **the whole batch must be rejected**, not only the offending
     * triangle. Validating as we go would let the earlier ones be drawn, which
     * makes the defect content-dependent. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        /* Two triangles: the first valid, the second with an index of 40. */
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        g_ram[table + 17] = 0; g_ram[table + 18] = 40; g_ram[table + 19] = 2;
        a = put_cmd(0, 0x05100000u, table);            /* 2 triangles */
        (void)put_cmd(a, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("a vertex index outside the cache is rejected",
              c.state.rejects[DKR_F3D_REJECT_INDEX] == 1);
        check("and it is **the whole batch** that is rejected, not just the offender",
              c.state.triangles == 0);
    }

    /* Drawing without having loaded any vertex: the indices are within the
       cache's bounds, but the cache is empty. This is not a wrong address — hence
       not a range rejection — and drawing uninitialised vertices would give
       random geometry, which is worse than a missing triangle. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        a = put_cmd(0, 0x05000000u, table);
        (void)put_cmd(a, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("drawing without having loaded any vertex is rejected",
              c.state.rejects[DKR_F3D_REJECT_INDEX] == 1 && c.state.emitted == 0);
    }

    /* A valid batch, vertices loaded first — the sequence of a real display list.
       Without this check, the previous one could pass for the wrong reason: a
       decoder that rejected everything would satisfy it too. */
    reset(&c, 1);
    {
        const unsigned int table = 0x100u;
        const unsigned int verts = 0x200u;
        g_ram[table + 1] = 0; g_ram[table + 2] = 1; g_ram[table + 3] = 2;
        /* Three vertices, at distinct positions so that the triangle has a
           surface. Positive z: in front of the near plane. */
        put16(verts +  0, -10); put16(verts +  2, -10); put16(verts +  4, 100);
        put16(verts + 10,  10); put16(verts + 12, -10); put16(verts + 14, 100);
        put16(verts + 20,   0); put16(verts + 22,  10); put16(verts + 24, 100);
        a = put_cmd(0, 0x04000000u | (2u << 19), verts);   /* 3 vertices */
        a = put_cmd(a, 0x05000000u, table);                /* 1 triangle */
        (void)put_cmd(a, 0xB8000000u, 0u);
        /* A projection where w = z. **Without it, w is 1** and a vertex at
           x = -10 ends up ten half-screens from the centre, hence outside the
           guard band — the triangle is then correctly discarded, and the check
           would fail for a reason unrelated to what it verifies. */
        {
            dkr_matrix proj;
            memset(&proj, 0, sizeof(proj));
            proj.m[0][0] = 1.0f; proj.m[1][1] = 1.0f; proj.m[2][2] = 0.5f;
            proj.m[2][3] = 1.0f;
            dkr_transform_set_projection(&c.transform, &proj);
        }
        dkr_f3d_run(&c, 0);
        check("a valid triangle batch is accepted",
              c.state.triangles == 1 && c.state.rejects[DKR_F3D_REJECT_INDEX] == 0);
        /* And the chain goes all the way: the triangle reaches the backend. */
        check("and the chain does emit it", c.state.emitted == 1);
    }

    /* --- Nesting: the depth is bounded --------------------------------------- */
    reset(&c, 0);
    {
        /* A list that calls itself: without a bound, the stack overflows. */
        unsigned int i;
        put_cmd(0, 0x06000000u, 0x00000000u);          /* call to itself */
        for (i = 0; i < 4; i++) { /* nothing: the loop is in the list */ }
        dkr_f3d_run(&c, 0);
        check("a recursive list does not overflow the stack",
              c.state.rejects[DKR_F3D_REJECT_DEPTH] >= 1);
    }

    /* A call then a return: the stack pops properly. */
    reset(&c, 1);
    a = put_cmd(0, 0x06000000u, 0x00000200u);          /* call to 0x200 */
    (void)put_cmd(a, 0xB8000000u, 0u);                 /* end, after the return */
    put_cmd(0x200u, 0xB8000000u, 0u);                  /* return */
    dkr_f3d_run(&c, 0);
    check("a call followed by a return comes back to the right place",
          trace_contains("return to 0x000008") &&
          c.state.rejects[DKR_F3D_REJECT_DEPTH] == 0);

    /* --- A command straddling the end of RDRAM ------------------------------- */
    reset(&c, 0);
    check("a list starting outside RDRAM is rejected",
          dkr_f3d_run(&c, RAM_SIZE - 4u) == 0 &&
          c.state.rejects[DKR_F3D_REJECT_ADDRESS] == 1);

    /* --- An unknown opcode stops decoding ------------------------------------ *
     *
     * Carrying on after an unknown opcode would invent commands: the stream is
     * probably desynchronised, and every following word would be read at the
     * wrong boundary. */
    reset(&c, 1);
    a = put_cmd(0, 0x99000000u, 0u);                   /* non-existent opcode */
    (void)put_cmd(a, 0x04000000u, 0u);                 /* must not be read */
    dkr_f3d_run(&c, 0);
    check("an unknown opcode is rejected",
          c.state.rejects[DKR_F3D_REJECT_OPCODE] == 1);
    check("and decoding stops there", c.state.vertices == 0);

    /* --- Bounding the log ---------------------------------------------------- *
     *
     * A corrupt display list would produce thousands of lines per frame, which
     * drowns the diagnosis and costs dearly on a 1998 machine. */
    reset(&c, 1);
    {
        unsigned int i, at = 0;
        for (i = 0; i < 200u; i++) {
            at = put_cmd(at, 0x04F82000u | (16u << 9), 0u);   /* always rejected */
        }
        put_cmd(at, 0xB8000000u, 0u);
        dkr_f3d_run(&c, 0);
        check("all 200 rejections are counted",
              c.state.rejects[DKR_F3D_REJECT_COUNT] == 200u);
        check("but the log is bounded", g_trace_count <= 64);
    }

    /* --- MoveWord ------------------------------------------------------------ */
    reset(&c, 1);
    a = put_cmd(0, 0xBC000002u, 0x00000001u);          /* billboard */
    a = put_cmd(a, 0xBC00000Au, 0x00000080u);          /* matrix 2 */
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("MoveWord sets billboard mode", c.state.billboard == 1);
    check("MoveWord selects the matrix", c.state.selected_matrix == 2);

    /* The presentation group is an **extension of the port**, recognised by its
       magic word. Without the magic, it is an ordinary MoveWord. */
    reset(&c, 1);
    a = put_cmd(0, 0xBC0000FEu, 0x444B5202u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("the presentation group is recognised by its magic word",
          trace_contains("PresentationGroup"));
    reset(&c, 1);
    a = put_cmd(0, 0xBC0000FEu, 0x12345678u);
    (void)put_cmd(a, 0xB8000000u, 0u);
    dkr_f3d_run(&c, 0);
    check("and an arbitrary word is not",
          !trace_contains("PresentationGroup"));

    /* --- TextureOffset carries an address, not offsets ----------------------- *
     *
     * This decoder read `w1` as two sixteen-bit texture offsets. The neighbouring
     * port, which runs, makes an **RDRAM addressing base** of it, and resets the
     * shift and the count to zero. The error would not have leapt out: it would
     * have displaced patterns rather than made them vanish, and one would have
     * looked at texture decoding. */
    {
        dkr_f3d_context ctx2;
        unsigned int at2 = 0;
        memset(g_ram, 0, sizeof(g_ram));
        at2 = put_cmd(at2, 0x02000000u, 0x00123456u);   /* TextureOffset */
        (void)put_cmd(at2, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx2, g_ram, RAM_SIZE, NULL);
        ctx2.state.texture_shift = 7;
        ctx2.state.texture_count = 9;
        (void)dkr_f3d_run(&ctx2, 0);
        check("TextureOffset keeps an RDRAM address",
              ctx2.state.texture_offset == 0x123456u);
        check("and resets the shift and the count to zero",
              ctx2.state.texture_shift == 0 && ctx2.state.texture_count == 0);
        /* The 24-bit mask is not decorative: RDRAM is 8 MiB, and a command's high
           bytes carry something else. */
        memset(g_ram, 0, sizeof(g_ram));
        at2 = 0;
        at2 = put_cmd(at2, 0x02000000u, 0xFF123456u);
        (void)put_cmd(at2, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx2, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&ctx2, 0);
        check("the address is bounded to 24 bits, the size of RDRAM",
              ctx2.state.texture_offset == 0x123456u);
    }

    /* --- Both RDRAM layouts give the same decoding --------------------------- *
     *
     * The game does not supply RDRAM in plain big-endian: librecomp stores it
     * **XOR-3 interleaved**, the byte at guest address `a` sitting at `a ^ 3`.
     * The decoder therefore carries `rdram_native`, and the whole point is that
     * both paths return exactly the same result.
     *
     * The test builds a scene in big-endian, produces its XOR-3 permutation, and
     * compares the two decodings field by field. It is the only check that can
     * fail if one of the two paths drifts: a display list read with the wrong
     * convention does not crash, it decodes plausible opcodes at absurd
     * addresses. Without this check, the fault would show up on the machine, as
     * missing scenery, and would be looked for in the rasteriser. */
    {
        dkr_f3d_context plain, twisted;
        static unsigned char interleaved[RAM_SIZE];
        unsigned int i, at3 = 0;

        memset(g_ram, 0, sizeof(g_ram));
        /* A scene that exercises the three read widths: the command (32 bits),
           the vertices (signed 16 bits) and the matrix (bytes). */
        at3 = put_cmd(at3, 0xBF000000u, 0x00000000u);          /* DMAOffsets */
        at3 = put_cmd(at3, 0x01000040u, 0x00000200u);          /* Matrix, 64 B */
        at3 = put_cmd(at3, 0x04000000u | (2u << 19), 0x300u);  /* Vertex x3 */
        at3 = put_cmd(at3, 0x05000000u, 0x00000102u);          /* Triangle */
        (void)put_cmd(at3, 0xB8000000u, 0u);
        /* An identity matrix in fixed point, and three recognisable vertices. */
        for (i = 0; i < 4; i++) {
            put16(0x200u + i * 10u, 1);        /* integer part, diagonal */
        }
        for (i = 0; i < 3; i++) {
            put16(0x300u + i * 16u + 0u, (int)(100 * (i + 1)));
            put16(0x300u + i * 16u + 2u, (int)(-50 * (i + 1)));
            put16(0x300u + i * 16u + 4u, 200);
        }

        /* The permutation. `i ^ 3` is an involution, so the same loop serves
           both directions; that is also what makes it easy to check. */
        for (i = 0; i < RAM_SIZE; i++) { interleaved[i ^ 3u] = g_ram[i]; }

        dkr_f3d_init(&plain, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&plain, 0);

        dkr_f3d_init(&twisted, interleaved, RAM_SIZE, NULL);
        twisted.rdram_native = 1;
        (void)dkr_f3d_run(&twisted, 0);

        check("the interleaved layout decodes the same number of commands",
              plain.state.commands == twisted.state.commands);
        check("the same vertices", plain.state.vertices == twisted.state.vertices);
        check("the same triangles", plain.state.triangles == twisted.state.triangles);
        check("the same emissions", plain.state.emitted == twisted.state.emitted);
        /* The check that stops the previous ones from succeeding vacuously: if
           the scene had decoded nothing, every counter would be zero on both
           sides and the agreement would be meaningless. */
        check("and the scene really did decode something",
              plain.state.vertices == 3 && plain.state.triangles == 1);
        {
            int same_rejects = 1;
            for (i = 0; i < (unsigned)DKR_F3D_REJECT_COUNT_MAX; i++) {
                if (plain.state.rejects[i] != twisted.state.rejects[i]) { same_rejects = 0; }
            }
            check("and the same rejections, category by category", same_rejects);
        }
        /* The matrix goes down a path distinct from the 32-bit reads — it passes
           through a flattened buffer — so it deserves its own check rather than
           being covered by ricochet. */
        {
            int same_matrix = 1;
            for (i = 0; i < 16u; i++) {
                const float a = plain.transform.slot[0].m[i / 4u][i % 4u];
                const float b = twisted.transform.slot[0].m[i / 4u][i % 4u];
                if (a != b) { same_matrix = 0; }
            }
            check("and the loaded matrix is identical under both layouts",
                  same_matrix);
        }
    }

    /* --- The filled rectangle -------------------------------------------------- *
     *
     * Measured on the machine before being written: across the 47,000 commands of
     * DKR's startup sequence, `FILLRECT` is the **only** draw order emitted. This
     * test therefore bears on the path the first pixel the port displays depends
     * on.
     *
     * Three things are checked here, each because it has its own way of going
     * wrong:
     *
     *   - the 5551 to 888 conversion, where 31 must give 255 and not 248;
     *   - the inclusion of the bottom-right corner, which costs a pixel if
     *     forgotten;
     *   - the scale, **read** from SETCOLORIMAGE and not assumed.
     */
    {
        dkr_f3d_context ctx4;
        dkr_render_backend bk;
        unsigned int at4 = 0;

        /* A local backend rather than the empty implementation: the latter
           accepts everything and records nothing, so it cannot say *where* the
           rectangle was asked for. And that is exactly what we want to check —
           corner inclusion and scale are coordinate errors, not counting
           errors. */
        memset(&bk, 0, sizeof(bk));
        bk.name = "test";
        bk.fill_rect = note_rect;
        g_rect_n = 0;

        memset(g_ram, 0, sizeof(g_ram));
        at4 = put_cmd(at4, 0xFF000000u | (320u - 1u), 0x00100000u); /* SetColorImage */
        at4 = put_cmd(at4, 0xF7000000u, 0xFFFFFFFFu);               /* white */
        /* 0,0 .. 9,4 inclusive, hence 10 by 5 pixels at scale 1. */
        at4 = put_cmd(at4, 0xF6000000u | (9u << 14) | (4u << 2), 0u);
        (void)put_cmd(at4, 0xB8000000u, 0u);

        dkr_f3d_init(&ctx4, g_ram, RAM_SIZE, &bk);
        /* A 320x240 viewport: the scale is then exactly one, which makes the
           expected coordinates readable without arithmetic. */
        dkr_transform_set_viewport(&ctx4.transform, 160.0f, -120.0f, 160.0f, 120.0f);
        (void)dkr_f3d_run(&ctx4, 0);

        check("the buffer's width is read from SetColorImage",
              ctx4.state.color_image_width == 320u);
        check("the rectangle reaches the backend", ctx4.state.rects == 1 && g_rect_n == 1);
        /* 0,0 .. 9,4 **inclusive** must become 0,0 .. 10,5 exclusive. Forgetting
           the +1 would leave one line of the background visible at the bottom and
           on the right of a full-screen clear, which would be blamed on the
           rasteriser. */
        check("the bottom-right corner is inclusive on the RDP side, exclusive on the backend side",
              g_rect[0] == 0 && g_rect[1] == 0 && g_rect[2] == 10 && g_rect[3] == 5);
        /* 0xFFFF in 5551 is opaque white. The check bears on 255 and not on
           "non-zero": a shift without high-bit replication would give 248, a
           value close enough to go unnoticed by eye and wrong enough that white
           is never white. */
        check("5551 white becomes 0xFFFFFF and not 0xF8F8F8",
              ctx4.state.fill_color_argb == 0x00FFFFFFu);

        /* And a colour that is neither black nor white, without which a
           conversion that merely saturated would pass the previous check. */
        memset(g_ram, 0, sizeof(g_ram));
        at4 = 0;
        at4 = put_cmd(at4, 0xFF000000u | (320u - 1u), 0x00100000u);
        /* red = 31, green = 0, blue = 0, alpha = 1 -> 0xF801 */
        at4 = put_cmd(at4, 0xF7000000u, 0xF801F801u);
        (void)put_cmd(at4, 0xB8000000u, 0u);
        dkr_f3d_init(&ctx4, g_ram, RAM_SIZE, &bk);
        (void)dkr_f3d_run(&ctx4, 0);
        check("a pure 5551 red becomes 0xFF0000",
              ctx4.state.fill_color_argb == 0x00FF0000u);
    }

    /* --- The return from a counted list ---------------------------------------- *
     *
     * A counted list has no `ENDDL`: its count is what ends it. The decoder
     * pushed the return address and then ignored it, so it fell out of the bottom
     * of the list and carried on into the memory that follows.
     *
     * The symptom on the machine was silent and expensive: 70 commands per list,
     * constant, two fills and **not one triangle**. DKR uploads its textures
     * through a counted list of seven commands, and all the geometry comes after
     * that return. It was lost there, every frame.
     *
     * The test reproduces exactly that trap: garbage is placed right after the
     * counted list, where the decoder used to skid. Without the return it reads
     * the garbage and rejects; with it, it never sees it. That is what makes the
     * check bear on the fix rather than on its wording. */
    {
        dkr_f3d_context ctx5;
        unsigned int at5 = 0, body, k;

        memset(g_ram, 0, sizeof(g_ram));
        /* The main list: calls a counted list of 3 commands, then loads three
           vertices and a triangle, then ends. */
        at5 = put_cmd(at5, 0xBF000000u, 0x00000000u);           /* DMAOffsets */
        at5 = put_cmd(at5, 0x07000000u | (3u << 16), 0x600u);   /* counted list */
        at5 = put_cmd(at5, 0x04000000u | (2u << 19), 0x300u);   /* Vertex x3 */
        at5 = put_cmd(at5, 0x05000000u, 0x00000102u);           /* Triangle */
        (void)put_cmd(at5, 0xB8000000u, 0u);

        /* The counted body: three innocuous RDP commands, **with no ENDDL**, and
           immediately followed by garbage. That is the real layout. */
        body = 0x600u;
        body = put_cmd(body, 0xE7000000u, 0u);                  /* PipeSync */
        body = put_cmd(body, 0xE7000000u, 0u);
        body = put_cmd(body, 0xE7000000u, 0u);
        (void)put_cmd(body, 0x99000000u, 0x99999999u);          /* garbage */

        for (k = 0; k < 3; k++) {
            put16(0x300u + k * 16u + 0u, (int)(10 * (k + 1)));
            put16(0x300u + k * 16u + 2u, (int)(20 * (k + 1)));
            put16(0x300u + k * 16u + 4u, 200);
        }

        dkr_f3d_init(&ctx5, g_ram, RAM_SIZE, NULL);
        (void)dkr_f3d_run(&ctx5, 0);

        check("the counted list hands back at its count, with no ENDDL",
              ctx5.state.rejects[DKR_F3D_REJECT_OPCODE] == 0);
        /* The check that really counts: what follows the return is reached.
           Without the return, the vertices and the triangle sit behind the
           garbage and are never read — exactly what the machine showed. */
        check("and what follows the return is decoded",
              ctx5.state.vertices == 3 && ctx5.state.triangles == 1);
    }

    printf("\n%d failure(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d failure(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
