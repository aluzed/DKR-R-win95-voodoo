/* E05-S02 — mise en œuvre. Le contrat et les mesures sont dans `tmu.h`. */
#include "tmu.h"

#include <stdio.h>
#include <string.h>

/* --- L'arbre buddy ----------------------------------------------------------- *
 *
 * Rangé comme un tas binaire : le nœud 0 couvre tout l'espace, le nœud `i` a
 * pour fils `2i+1` et `2i+2`. Aucun pointeur, aucune liste chaînée — la
 * structure entière tient dans un tableau d'octets de 32 Kio, et une adresse se
 * déduit de l'indice par arithmétique.
 *
 * C'est ce qui la rend éprouvable : il n'y a pas d'état caché à corrompre, et un
 * parcours complet de l'arbre vérifie l'invariant en quelques lignes. */

static unsigned int node_size(unsigned int index)
{
    /* Le nœud 0 couvre DKR_TMU_SPAN, ses deux fils la moitié, etc. La
       profondeur est le rang du bit de poids fort de (index+1). */
    unsigned int span = DKR_TMU_SPAN;
    unsigned int i = index + 1u;
    while (i > 1u) { i >>= 1; span >>= 1; }
    return span;
}

static unsigned int node_offset(unsigned int index)
{
    /* L'offset se reconstruit en remontant : chaque fois qu'on est le fils
       droit, on a franchi la moitié du bloc parent. */
    unsigned int offset = 0, size = node_size(index), i = index;
    while (i > 0u) {
        if ((i & 1u) == 0u) {          /* fils droit : i = 2p + 2 */
            offset += size;
        }
        size <<= 1;
        i = (i - 1u) >> 1;
    }
    return offset;
}

static unsigned int round_up_pow2(unsigned int v)
{
    unsigned int p = DKR_TMU_MIN_BLOCK;
    while (p < v) { p <<= 1; }
    return p;
}

/* Marque hors d'usage tout ce qui dépasse la mémoire réelle.
 *
 * L'espace mesuré va de 0 à 0x1FFFF8 : **il manque huit octets** pour que la
 * TMU fasse exactement 2 Mio, et l'arbre en couvre 2 Mio pleins. Sans cette
 * réserve, l'allocateur rendrait une adresse que la carte n'accepte pas. Le
 * défaut serait rare — il faudrait que la mémoire soit presque pleine — donc
 * découvert tard, et sur un niveau chargé plutôt que sur un test. */
static void reserve_outside(dkr_tmu *t, unsigned int index)
{
    const unsigned int start = t->base + node_offset(index);
    const unsigned int size  = node_size(index);

    if (start >= t->limit) {
        t->node[index] = DKR_TMU_RESERVED;      /* entièrement dehors */
        return;
    }
    if (start + size <= t->limit) {
        return;                                  /* entièrement dedans */
    }
    /* À cheval : on coupe et l'on recommence sur les deux moitiés. */
    if (size <= DKR_TMU_MIN_BLOCK) {
        t->node[index] = DKR_TMU_RESERVED;       /* on ne coupe plus : on jette */
        return;
    }
    t->node[index] = DKR_TMU_SPLIT;
    reserve_outside(t, index * 2u + 1u);
    reserve_outside(t, index * 2u + 2u);
}

void dkr_tmu_init(dkr_tmu *t, int index, unsigned int base, unsigned int limit,
                  dkr_tmu_download_fn download, void *user)
{
    if (!t) { return; }
    memset(t, 0, sizeof(*t));
    t->index          = index;
    t->base           = base;
    t->limit          = limit;
    t->download       = download;
    t->download_user  = user;
    reserve_outside(t, 0u);
}

/* Descente récursive : trouver un bloc libre de `want` octets, en coupant au
   besoin. Rend l'offset relatif à `base`, ou DKR_TMU_NONE.
 *
 * La première place trouvée est prise, sans chercher la meilleure. Sur des
 * demandes toutes en puissances de deux, « la première » et « la meilleure »
 * désignent le même bloc : l'arbre ne contient pas de bloc de taille
 * intermédiaire où l'on pourrait faire mieux. */
static unsigned int take(dkr_tmu *t, unsigned int index, unsigned int want)
{
    const unsigned int size = node_size(index);

    if (size < want || t->node[index] == DKR_TMU_RESERVED ||
        t->node[index] == DKR_TMU_USED) {
        return DKR_TMU_NONE;
    }
    if (size == want) {
        if (t->node[index] != DKR_TMU_FREE) {
            return DKR_TMU_NONE;                 /* déjà coupé : rien d'entier ici */
        }
        t->node[index] = DKR_TMU_USED;
        return node_offset(index);
    }
    /* Trop grand : couper, puis descendre. */
    if (t->node[index] == DKR_TMU_FREE) {
        t->node[index] = DKR_TMU_SPLIT;
    }
    {
        unsigned int r = take(t, index * 2u + 1u, want);
        if (r != DKR_TMU_NONE) { return r; }
        return take(t, index * 2u + 2u, want);
    }
}

unsigned int dkr_tmu_alloc(dkr_tmu *t, unsigned int bytes)
{
    unsigned int want, offset;

    if (!t || bytes == 0u) { return DKR_TMU_NONE; }
    want = round_up_pow2(bytes);
    if (want > DKR_TMU_SPAN) { return DKR_TMU_NONE; }

    offset = take(t, 0u, want);
    if (offset == DKR_TMU_NONE) { return DKR_TMU_NONE; }

    t->used_bytes += want;
    if (t->used_bytes > t->stats.peak_bytes) {
        t->stats.peak_bytes = t->used_bytes;
    }
    return t->base + offset;
}

/* Libère et **fusionne**. C'est la fusion qui distingue le buddy d'un simple
   allocateur par classes : sans elle, un niveau qui remplace ses textures 64x64
   par des 128x128 échouerait alors que la place existe, éparpillée. */
static int give_back(dkr_tmu *t, unsigned int index, unsigned int offset,
                     unsigned int want)
{
    const unsigned int size = node_size(index);

    if (size < want) { return 0; }
    if (size == want) {
        if (t->node[index] != DKR_TMU_USED || node_offset(index) != offset) {
            return 0;
        }
        t->node[index] = DKR_TMU_FREE;
        return 1;
    }
    if (t->node[index] != DKR_TMU_SPLIT) { return 0; }
    {
        const unsigned int left  = index * 2u + 1u;
        const unsigned int right = index * 2u + 2u;
        if (!give_back(t, left, offset, want) &&
            !give_back(t, right, offset, want)) {
            return 0;
        }
        /* Les deux moitiés libres : le parent redevient un bloc entier. */
        if (t->node[left] == DKR_TMU_FREE && t->node[right] == DKR_TMU_FREE) {
            t->node[index] = DKR_TMU_FREE;
        }
        return 1;
    }
}

void dkr_tmu_free(dkr_tmu *t, unsigned int address, unsigned int bytes)
{
    unsigned int want;
    if (!t || bytes == 0u || address < t->base) { return; }
    want = round_up_pow2(bytes);
    if (give_back(t, 0u, address - t->base, want)) {
        t->used_bytes -= (want <= t->used_bytes) ? want : t->used_bytes;
    }
}

unsigned int dkr_tmu_used(const dkr_tmu *t) { return t ? t->used_bytes : 0u; }

/* --- Le cache de résidence ---------------------------------------------------- */

static dkr_tmu_resident *find_resident(dkr_tmu *t, unsigned long long key)
{
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (t->resident[i].live && t->resident[i].key == key) {
            return &t->resident[i];
        }
    }
    return 0;
}

static dkr_tmu_resident *free_slot(dkr_tmu *t)
{
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        if (!t->resident[i].live) { return &t->resident[i]; }
    }
    return 0;
}

/* Choisit la victime : la plus anciennement employée, et **non protégée**.
 *
 * Rend NULL s'il n'y a rien à évincer, ce qui n'est pas la même chose que
 * « la mémoire est pleine » : cela veut dire que tout ce qui est résident sert
 * à l'image en cours. Continuer d'évincer serait alors nuisible — on
 * retéléchargerait sans cesse ce dont on a besoin, en payant le bus à chaque
 * fois pour n'afficher rien de plus. */
static dkr_tmu_resident *pick_victim(dkr_tmu *t)
{
    dkr_tmu_resident *victim = 0;
    int i;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        dkr_tmu_resident *r = &t->resident[i];
        if (!r->live || r->pinned) { continue; }
        if (!victim || r->last_used < victim->last_used) { victim = r; }
    }
    return victim;
}

unsigned int dkr_tmu_acquire(dkr_tmu *t, unsigned long long key,
                             const void *data, unsigned int bytes)
{
    dkr_tmu_resident *r;
    unsigned int address;

    if (!t || bytes == 0u) { return DKR_TMU_NONE; }

    t->clock++;

    r = find_resident(t, key);
    if (r) {
        r->last_used = t->clock;
        r->pinned    = 1;
        t->stats.hits++;
        return r->address;
    }
    t->stats.misses++;

    /* Faire de la place jusqu'à ce que l'allocation passe. La boucle s'arrête
       aussi quand il n'y a plus de victime : voir `pick_victim`. */
    for (;;) {
        address = dkr_tmu_alloc(t, bytes);
        if (address != DKR_TMU_NONE) { break; }
        {
            dkr_tmu_resident *victim = pick_victim(t);
            if (!victim) {
                t->stats.failures++;
                return DKR_TMU_NONE;
            }
            dkr_tmu_free(t, victim->address, victim->bytes);
            victim->live = 0;
            t->stats.evictions++;
        }
    }

    r = free_slot(t);
    if (!r) {
        /* La table est pleine alors que la mémoire ne l'est pas. Évincer libère
           une entrée aussi bien qu'un bloc. */
        dkr_tmu_resident *victim = pick_victim(t);
        if (!victim) {
            dkr_tmu_free(t, address, bytes);
            t->stats.failures++;
            return DKR_TMU_NONE;
        }
        dkr_tmu_free(t, victim->address, victim->bytes);
        victim->live = 0;
        t->stats.evictions++;
        r = victim;
    }

    if (t->download && !t->download(t->download_user, t->index, address,
                                    data, bytes)) {
        dkr_tmu_free(t, address, bytes);
        t->stats.failures++;
        return DKR_TMU_NONE;
    }
    t->stats.downloads++;
    t->stats.download_bytes      += bytes;
    t->stats.downloads_this_frame++;
    t->stats.bytes_this_frame    += bytes;

    r->key       = key;
    r->address   = address;
    r->bytes     = bytes;
    r->last_used = t->clock;
    r->pinned    = 1;
    r->live      = 1;
    return address;
}

void dkr_tmu_begin_frame(dkr_tmu *t)
{
    int i;
    if (!t) { return; }
    t->stats.downloads_this_frame = 0;
    t->stats.bytes_this_frame     = 0;
    for (i = 0; i < DKR_TMU_MAX_RESIDENT; i++) {
        t->resident[i].pinned = 0;
    }
}

void dkr_tmu_pin(dkr_tmu *t, unsigned long long key)
{
    dkr_tmu_resident *r;
    if (!t) { return; }
    r = find_resident(t, key);
    if (r) { r->pinned = 1; }
}

void dkr_tmu_reset(dkr_tmu *t)
{
    unsigned int base, limit;
    dkr_tmu_download_fn dl;
    void *user;
    int index;
    dkr_tmu_stats keep;

    if (!t) { return; }
    /* Les compteurs cumulés survivent au changement de niveau : ce sont eux qui
       diront, à la fin, si le portage a téléchargé pendant les courses. */
    keep  = t->stats;
    base  = t->base;  limit = t->limit;
    dl    = t->download; user = t->download_user; index = t->index;
    dkr_tmu_init(t, index, base, limit, dl, user);
    t->stats = keep;
    t->stats.downloads_this_frame = 0;
    t->stats.bytes_this_frame     = 0;
}

void dkr_tmu_format_status(const dkr_tmu *t, char *out, unsigned int size)
{
    char line[160];
    unsigned int n;
    if (!out || size == 0u) { return; }
    if (!t) { out[0] = 0; return; }

    sprintf(line, "TMU%d %uK/%uK  succes %lu/%lu  tel %lu (%lu image)  evic %lu  echecs %lu",
            t->index,
            t->used_bytes / 1024u,
            (t->limit - t->base) / 1024u,
            t->stats.hits, t->stats.hits + t->stats.misses,
            t->stats.downloads, t->stats.downloads_this_frame,
            t->stats.evictions, t->stats.failures);
    n = (unsigned int)strlen(line);
    if (n >= size) { n = size - 1u; }
    memcpy(out, line, n);
    out[n] = 0;
}
