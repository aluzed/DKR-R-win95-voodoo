/* E01-S05 - the recompiled code's arithmetic, compared against the 64-bit
 * oracle.
 *
 * The C that N64Recomp produces manipulates the VR4300's registers as 64-bit
 * integers. On the host these are machine registers; in 32-bit, every operation
 * becomes a pair, and divisions and shifts go through libgcc's helpers -
 * `__divdi3`, `__moddi3`, `__ashrdi3`. The compiler takes care of it, but "the
 * compiler takes care of it" is not a verification.
 *
 * The ticket therefore asks for better than a code review: "a targeted test on a
 * few of the game's arithmetic functions, compared against the 64-bit oracle's
 * output". That is what this program does.
 *
 * ## How
 *
 * Each chosen function is run on an **entirely determined** state: an RDRAM
 * filled by a fixed-seed generator, and a context whose every register receives
 * a value drawn from the same generator. The final state - the whole context
 * plus RDRAM - is then summarised by a 64-bit FNV-1a digest.
 *
 * The same program, compiled for the 64-bit host and for the 32-bit target, must
 * produce **the same digests**. Any divergence is a porting defect, and the
 * digest says which function produced it.
 *
 * ## The registers point into RDRAM, by design
 *
 * The generated code's memory accesses are of the form
 * `rdram + (register + offset) - 0xFFFFFFFF80000000`. For them to land inside
 * the allocated region, the registers therefore receive a value near
 * `0xFFFFFFFF80000000`, offset by a few kilobytes. Without that the program
 * would read anywhere and the comparison would bear on nothing.
 *
 * Some functions nonetheless compute an address outside the region. They are not
 * discarded: the fault is **caught**, and "this function faults" becomes an
 * observation like any other, which must also agree between the two targets.
 *
 * ## A known limit, and where it bites
 *
 * The catching rests on `signal(SIGSEGV)`. On the host that is reliable, an
 * alternate stack even settling stack overflow. **Under Windows 95 it is not**:
 * `obj_animate`'s fault is duly delivered, `func_8001CD28`'s is not and the
 * process dies. It is not a stack overflow - a 64 MiB reserve changes nothing -
 * but a fault mingw's CRT does not translate into a signal on this target.
 *
 * The comparison therefore stops at the first function of that kind. On the
 * functions it reaches, it **agrees exactly**, faults included.
 *
 * To go all the way a resuming driver will be needed: the program notes in a
 * file the function it is about to run, and a relaunch restarts after it. That
 * works on both targets and demands no exception acrobatics - which beats making
 * the harness depend on what Windows 95 is willing to deliver.
 */
/* `sigsetjmp` is POSIX and not ISO: under strict `-std=c17` the header hides it.
   The harness compiles to the same standard as the recompiled code, so we ask
   for the extension explicitly rather than relax the standard. */
#if !defined(_WIN32)
#  define _GNU_SOURCE 1
#endif

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

#include "recomp.h"

/* --- The ground ---------------------------------------------------------- *
 *
 * 8 MiB, the RDRAM of an N64 with the Expansion Pak - the same amount E00-S01
 * verified allocates under Windows 95.
 */
#define RDRAM_SIZE   (8u * 1024u * 1024u)
#define RDRAM_BASE   0xFFFFFFFF80000000ULL
/* The registers aim at the middle of the region: a negative offset from the
   generated code then stays within bounds. */
#define REG_POINT    (RDRAM_BASE + (RDRAM_SIZE / 2))

static uint8_t *rdram;

/* --- An RDRAM framed by forbidden pages ------------------------------------ *
 *
 * Not every function stays inside the region: some compute an address from a
 * register which, in a real session, would point elsewhere. With a plain
 * allocation, those writes damage the heap and the program dies much later, in
 * an unrelated place - which is what happened, and led to the belief that the
 * fourth function was at fault.
 *
 * So we reserve generously, make only the 8 MiB window accessible, and anything
 * that overruns lands on a forbidden page. The fault becomes immediate,
 * catchable, and attributed to the right function. It is also the scheme
 * `librecomp` uses for real - reserve, commit a window.
 */
#define GUARD_BYTES  (64u * 1024u * 1024u)

static uint8_t *reserve_rdram(void)
{
#if defined(_WIN32)
    uint8_t *base = (uint8_t *)VirtualAlloc(NULL, GUARD_BYTES * 2 + RDRAM_SIZE,
                                            MEM_RESERVE, PAGE_NOACCESS);
    if (!base) {
        return NULL;
    }
    if (!VirtualAlloc(base + GUARD_BYTES, RDRAM_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
        return NULL;
    }
    return base + GUARD_BYTES;
#else
    uint8_t *base = (uint8_t *)mmap(NULL, GUARD_BYTES * 2 + RDRAM_SIZE,
                                    PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return NULL;
    }
    if (mprotect(base + GUARD_BYTES, RDRAM_SIZE, PROT_READ | PROT_WRITE) != 0) {
        return NULL;
    }
    return base + GUARD_BYTES;
#endif
}

/* A fixed-seed generator. Deliberately trivial and written here rather than
   borrowed from the library: `rand` differs from one implementation to another,
   and one of the two sides of the comparison is not Linux. */
static uint64_t rng_state;

static void rng_seed(uint64_t seed) { rng_state = seed; }

static uint64_t rng_next(void)
{
    /* xorshift64. Identical everywhere, with no dependency on the CRT. */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* --- The digest ----------------------------------------------------------- */

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv1a(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; i++) {
        hash ^= (uint64_t)p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/* --- Catching faults ------------------------------------------------------ *
 *
 * `signal` rather than SEH: mingw offers no `__try` in C on i686, and the CRT
 * already translates a faulting access into SIGSEGV. The same code therefore
 * serves both targets, which is exactly what one wants from a comparison
 * harness.
 */
/* Under POSIX, `longjmp` from a handler leaves the signal **blocked**: the second
   fault then kills the process, and one would wrongly believe the first faulting
   function is the last. `sigsetjmp` with mask saving is the only correct form.
   Windows has no signal mask, and `setjmp` suffices there - hence the two
   spellings. */
#if defined(_WIN32)
#  define FAULT_SETJMP(buf)   setjmp(buf)
#  define FAULT_LONGJMP(buf)  longjmp((buf), 1)
typedef jmp_buf fault_buf;
#else
#  define FAULT_SETJMP(buf)   sigsetjmp((buf), 1)
#  define FAULT_LONGJMP(buf)  siglongjmp((buf), 1)
typedef sigjmp_buf fault_buf;
#endif

static fault_buf fault_return;
static volatile int fault_armed;

static void on_fault(int sig)
{
    (void)sig;
    if (fault_armed) {
        fault_armed = 0;
        FAULT_LONGJMP(fault_return);
    }
    _exit(3);
}

/* --- The context, laid down reproducibly ---------------------------------- */

static void prepare(recomp_context *ctx, uint64_t seed)
{
    size_t i;
    uint64_t *slot;

    rng_seed(seed);

    /* RDRAM first: it is what the functions will read. */
    for (i = 0; i < RDRAM_SIZE; i += 8) {
        uint64_t v = rng_next();
        memcpy(rdram + i, &v, 8);
    }

    memset(ctx, 0, sizeof(*ctx));

    /* The 32 general registers. r0 stays zero - it is MIPS's hard-wired zero
       register, and giving it a value would produce results the hardware never
       produces. */
    slot = &ctx->r1;
    for (i = 0; i < 31; i++) {
        /* A value near the aiming point, so that memory accesses land inside
           the region, with enough entropy in the low bits for the arithmetic to
           have something to get its teeth into. */
        slot[i] = REG_POINT + (int64_t)(int16_t)(rng_next() & 0x1FFF);
    }

    /* The floating-point registers, as bit patterns: it is the integer <->
       float conversions that diverge most readily between x87 and SSE, so they
       must be given something to diverge on.

       We avoid patterns that make a NaN or an infinity: their propagation is a
       question of floating-point behaviour and not of 32-bit porting, and it
       would drown the signal we are after. The exponents are therefore
       bounded. */
    {
        fpr *f = &ctx->f0;
        for (i = 0; i < 32; i++) {
            uint64_t v = rng_next();
            v &= 0x3FFFFFFFFFFFFFFFULL;      /* a modest exponent */
            v |= 0x3F00000000000000ULL;      /* and not denormal */
            f[i].u64 = v;
        }
    }

    /* `f_odd` is not an array but a **pointer** to the high half of f0: it is
       through it that the generated code reaches the odd half-registers in MIPS
       32-bit mode. Leaving it null, as the first version of this harness did,
       blows the program up before its first line. librecomp sets it the same way
       (`recomp.cpp:471`). */
    ctx->f_odd = &ctx->f0.u32h;
}

/* The digest bears on the **architectural** state, field by field, and never on
 * the raw structure. Two reasons, and either would suffice:
 *
 *  - `recomp_context` contains `f_odd`, a **pointer**. Its value changes from one
 *    run to the next; summarising the whole structure therefore gave a different
 *    digest on every attempt, including on the same machine.
 *  - Its size and layout **differ between 32 and 64 bits**, because of that same
 *    pointer and of padding. A digest of the raw structure could never have been
 *    compared between the two targets, which is the whole point of this program.
 *
 * Every quantity is therefore summarised at a fixed width and in a fixed order.
 */
static uint64_t digest(const recomp_context *ctx)
{
    uint64_t h = FNV_OFFSET;
    const gpr *r = &ctx->r0;
    const fpr *f = &ctx->f0;
    size_t i;

    for (i = 0; i < 32; i++) {
        uint64_t v = (uint64_t)r[i];
        h = fnv1a(h, &v, sizeof(v));
    }
    for (i = 0; i < 32; i++) {
        uint64_t v = f[i].u64;
        h = fnv1a(h, &v, sizeof(v));
    }
    {
        uint64_t hi = ctx->hi, lo = ctx->lo;
        uint32_t status = ctx->status_reg;
        uint8_t  mode = ctx->mips3_float_mode;
        h = fnv1a(h, &hi, sizeof(hi));
        h = fnv1a(h, &lo, sizeof(lo));
        h = fnv1a(h, &status, sizeof(status));
        h = fnv1a(h, &mode, sizeof(mode));
    }
    /* The whole of RDRAM, to catch a write in the wrong place as much as a
       wrong computation. */
    h = fnv1a(h, rdram, RDRAM_SIZE);
    return h;
}

/* --- The functions under test --------------------------------------------- */

#define DKR_ORACLE_FN(name) void name(uint8_t *rdram, recomp_context *ctx);
#include "functions.inc"
#undef DKR_ORACLE_FN

typedef void (*oracle_fn)(uint8_t *, recomp_context *);

static const struct {
    const char *name;
    oracle_fn   fn;
} entries[] = {
#define DKR_ORACLE_FN(name) { #name, name },
#include "functions.inc"
#undef DKR_ORACLE_FN
};

#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

int main(void)
{
    recomp_context ctx;
    size_t i;
    unsigned faults = 0;
    FILE *report;

    rdram = reserve_rdram();
    if (!rdram) {
        printf("FAILED: cannot reserve RDRAM\n");
        return 2;
    }

    /* An alternate stack, without which a stack overflow is uncatchable: the
       handler would need the very stack that has just run out. That is what
       killed the harness on `func_8001CD28`, and made it look like a defect of
       the recompiled code when it is a property of it - that function goes deep,
       and the harness's random state pushes it there. */
#if !defined(_WIN32)
    {
        static char alt[262144];
        stack_t ss;
        struct sigaction sa;

        ss.ss_sp    = alt;
        ss.ss_size  = sizeof(alt);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_fault;
        sa.sa_flags   = SA_ONSTACK | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL);
        sigaction(SIGFPE,  &sa, NULL);
    }
#else
    signal(SIGSEGV, on_fault);
    signal(SIGFPE, on_fault);
#endif

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("# digests of the recompiled code - %u functions\n",
           (unsigned)ENTRY_COUNT);
    printf("# %u-bit\n", (unsigned)(sizeof(void *) * 8));

    for (i = 0; i < ENTRY_COUNT; i++) {
        uint64_t h;

        /* One seed per function, derived from its index: two runs of the program
           lay down the same state, and two functions do not share one. */
        prepare(&ctx, 0x9E3779B97F4A7C15ULL ^ (uint64_t)(i + 1));

        /* The name is announced **before** the call: if the function leaves the
           frame without the fault being caught, the report's last line names it
           all the same. Without that, one searches for a long time. */
        fprintf(stderr, "  -> %s\n", entries[i].name);
        fault_armed = 1;
        if (FAULT_SETJMP(fault_return) == 0) {
            entries[i].fn(rdram, &ctx);
            fault_armed = 0;
            h = digest(&ctx);
            printf("%-32s %016llX\n", entries[i].name, (unsigned long long)h);
        } else {
            /* A fault is an observation, not a failure: it simply has to happen
               on both sides. */
            faults++;
            printf("%-32s FAULT\n", entries[i].name);
        }
        fflush(stdout);
    }

    printf("# %u fault(s) out of %u\n", faults, (unsigned)ENTRY_COUNT);

#if defined(_WIN32)
    report = fopen("D:\\ORACLE.TXT", "w");
#else
    report = fopen("oracle-host.txt", "w");
#endif
    if (report) {
        /* The report is rewritten into the file, to be compared byte for byte
           between the two targets. */
        for (i = 0; i < ENTRY_COUNT; i++) {
            uint64_t h;
            prepare(&ctx, 0x9E3779B97F4A7C15ULL ^ (uint64_t)(i + 1));
            fault_armed = 1;
            if (FAULT_SETJMP(fault_return) == 0) {
                entries[i].fn(rdram, &ctx);
                fault_armed = 0;
                h = digest(&ctx);
                fprintf(report, "%-32s %016llX\n", entries[i].name,
                        (unsigned long long)h);
            } else {
                fprintf(report, "%-32s FAULT\n", entries[i].name);
            }
        }
        fclose(report);
    }

    return 0;
}
