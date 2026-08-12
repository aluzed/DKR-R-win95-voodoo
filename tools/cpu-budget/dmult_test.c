/* Equivalence de DMULT/DMULTU portable contre __int128, sur x86-64. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Reference : la version __int128 de recomp.h */
static void DMULT_ref(int64_t a, int64_t b, int64_t* lo, int64_t* hi) {
    __int128 f = ((__int128)a) * ((__int128)b);
    *hi = (int64_t)(f >> 64); *lo = (int64_t)(f >> 0);
}
static void DMULTU_ref(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi) {
    unsigned __int128 f = ((unsigned __int128)a) * ((unsigned __int128)b);
    *hi = (uint64_t)(f >> 64); *lo = (uint64_t)(f >> 0);
}

/* Candidat : la version portable du patch 0002 */
static void DMULTU_new(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    const uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
    const uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
    const uint64_t p_ll = a_lo * b_lo, p_lh = a_lo * b_hi;
    const uint64_t p_hl = a_hi * b_lo, p_hh = a_hi * b_hi;
    const uint64_t middle = (p_ll >> 32) + (uint32_t)p_lh + (uint32_t)p_hl;
    *lo64 = (middle << 32) | (uint32_t)p_ll;
    *hi64 = p_hh + (p_lh >> 32) + (p_hl >> 32) + (middle >> 32);
}
static void DMULT_new(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    uint64_t u_lo = 0, u_hi = 0;
    DMULTU_new((uint64_t)a, (uint64_t)b, &u_lo, &u_hi);
    if (a < 0) { u_hi -= (uint64_t)b; }
    if (b < 0) { u_hi -= (uint64_t)a; }
    *lo64 = (int64_t)u_lo; *hi64 = (int64_t)u_hi;
}

static uint64_t rng_state = 0x123456789ABCDEFull;
static uint64_t rnd(void) { /* xorshift64* */
    rng_state ^= rng_state >> 12; rng_state ^= rng_state << 25; rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

int main(void) {
    long fails = 0, n = 0;
    const uint64_t edges[] = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
        0x100000000ull, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull};
    const int ne = sizeof(edges)/sizeof(edges[0]);

    /* frontieres : produit cartesien complet */
    for (int i = 0; i < ne; i++) for (int j = 0; j < ne; j++) {
        uint64_t ul1,uh1,ul2,uh2; int64_t sl1,sh1,sl2,sh2;
        DMULTU_ref(edges[i],edges[j],&ul1,&uh1); DMULTU_new(edges[i],edges[j],&ul2,&uh2);
        DMULT_ref((int64_t)edges[i],(int64_t)edges[j],&sl1,&sh1);
        DMULT_new((int64_t)edges[i],(int64_t)edges[j],&sl2,&sh2);
        n += 2;
        if (ul1!=ul2||uh1!=uh2) { fails++; printf("DMULTU frontiere %llx*%llx\n",(unsigned long long)edges[i],(unsigned long long)edges[j]); }
        if (sl1!=sl2||sh1!=sh2) { fails++; printf("DMULT frontiere %llx*%llx\n",(unsigned long long)edges[i],(unsigned long long)edges[j]); }
    }

    /* aleatoire */
    for (long k = 0; k < 10000000L; k++) {
        uint64_t a = rnd(), b = rnd();
        uint64_t ul1,uh1,ul2,uh2; int64_t sl1,sh1,sl2,sh2;
        DMULTU_ref(a,b,&ul1,&uh1); DMULTU_new(a,b,&ul2,&uh2);
        DMULT_ref((int64_t)a,(int64_t)b,&sl1,&sh1); DMULT_new((int64_t)a,(int64_t)b,&sl2,&sh2);
        n += 2;
        if (ul1!=ul2||uh1!=uh2) { if (++fails < 5) printf("DMULTU alea %llx*%llx\n",(unsigned long long)a,(unsigned long long)b); }
        if (sl1!=sl2||sh1!=sh2) { if (++fails < 5) printf("DMULT alea %llx*%llx\n",(unsigned long long)a,(unsigned long long)b); }
    }
    printf("comparaisons: %ld, echecs: %ld -> %s\n", n, fails, fails ? "ECHEC" : "IDENTIQUE AU BIT PRES");
    return fails != 0;
}
