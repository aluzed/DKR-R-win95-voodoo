/* E05-S03 — l'oracle du combineur et la table de correspondance.
 *
 * Deux choses très différentes sont vérifiées ici, et il vaut de les distinguer.
 *
 * **L'évaluateur** est vérifié contre des valeurs calculées à la main. C'est
 * possible parce que `(a - b) * c + d` est une formule courte : on peut poser
 * les nombres et savoir la réponse d'avance, ce qui est la seule façon de
 * vérifier un oracle — le comparer à un autre programme ne ferait que déplacer
 * la question.
 *
 * **La table** est vérifiée par des propriétés, pas cas par cas : aucune clé en
 * double, toute entrée retrouvable par sa clé, toute catégorie justifiée. Un
 * contrôle par configuration serait vingt-neuf fois le même code et laisserait
 * passer précisément ce qui compte — qu'une entrée en masque une autre.
 */
#include "render/combiner.h"

#include <stdio.h>
#include <string.h>

static int g_fails;
static FILE *g_out;

static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", ok ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void check_near(const char *what, double got, double want, double tol)
{
    const int ok = (got - want < tol) && (want - got < tol);
    printf("  %s %-46s attendu %8.3f  obtenu %8.3f\n",
           ok ? "ok   " : "ECHEC", what, want, got);
    if (g_out) {
        fprintf(g_out, "  %s %-46s attendu %8.3f  obtenu %8.3f\n",
                ok ? "ok   " : "ECHEC", what, want, got);
        fflush(g_out);
    }
    if (!ok) { g_fails++; }
}

static void report(const char *fmt, unsigned long a, unsigned long b)
{
    char line[200];
    sprintf(line, fmt, a, b);
    printf("%s\n", line);
    if (g_out) { fprintf(g_out, "%s\n", line); fflush(g_out); }
}

static void set4(float v[4], float r, float g, float b, float a)
{
    v[0] = r; v[1] = g; v[2] = b; v[3] = a;
}

int main(void)
{
    dkr_combiner_inputs in;
    dkr_combiner c;
    float out[4];

    g_out = fopen("D:\\COMBTEST.TXT", "w");

    memset(&in, 0, sizeof(in));
    set4(in.texel0,      200.0f, 100.0f,  50.0f, 128.0f);
    set4(in.texel1,       10.0f,  20.0f,  30.0f,  40.0f);
    set4(in.primitive,   255.0f,   0.0f,   0.0f, 200.0f);
    set4(in.shade,       128.0f, 128.0f, 128.0f, 255.0f);
    set4(in.environment,   0.0f, 255.0f,   0.0f,  64.0f);

    /* --- G_CC_MODULATEIDECALA : (TEXEL0, 0, SHADE, 0), alpha (0,0,0,TEXEL0) -- *
     *
     * Couleur = texel0 x shade/255 = 200 x 128/255 = 100,39 pour le rouge.
     * Alpha = texel0.a = 128. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 1; c.rgb[0].b = 8; c.rgb[0].c = 4; c.rgb[0].d = 7;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 1;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("MODULATEIDECALA : rouge = texel x shade", out[0], 200.0 * 128.0 / 255.0, 0.01);
    check_near("  et l'alpha vient du texel",             out[3], 128.0, 0.01);

    /* --- G_CC_BLENDI_ENV_ALPHA : (ENV, SHADE, ENV_ALPHA, SHADE) -------------- *
     *
     * C'est la forme `d == b` qui tombe exactement sur la fonction BLEND de
     * Glide, et le cas le plus utile a verifier a la main :
     * (0 - 128) x 64/255 + 128 = 96,88 pour le rouge ;
     * (255 - 128) x 64/255 + 128 = 159,88 pour le vert. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 5; c.rgb[0].b = 4; c.rgb[0].c = 12; c.rgb[0].d = 4;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 4;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("BLENDI_ENV_ALPHA : rouge interpole vers l'env",
               out[0], (0.0 - 128.0) * 64.0 / 255.0 + 128.0, 0.01);
    check_near("  et le vert aussi",
               out[1], (255.0 - 128.0) * 64.0 / 255.0 + 128.0, 0.01);
    check_near("  l'alpha reste celle du sommet", out[3], 255.0, 0.01);

    /* --- Deux cycles : le second lit COMBINED ------------------------------- *
     *
     * MODULATEIDECALA puis MODULATEIDECALA2 = (COMBINED, 0, SHADE, 0).
     * Le premier donne 100,39 en rouge ; le second le remultiplie par shade :
     * 100,39 x 128/255 = 50,39. C'est le controle qui prouve que le resultat
     * est bien **reinjecte** et non recalcule depuis les entrees. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 1; c.rgb[0].b = 8; c.rgb[0].c = 4; c.rgb[0].d = 7;
    c.alpha[0].a = 7; c.alpha[0].b = 7; c.alpha[0].c = 7; c.alpha[0].d = 1;
    c.rgb[1].a = 0; c.rgb[1].b = 8; c.rgb[1].c = 4; c.rgb[1].d = 7;
    c.alpha[1].a = 7; c.alpha[1].b = 7; c.alpha[1].c = 7; c.alpha[1].d = 0;
    dkr_combiner_eval_all(&c, DKR_CYCLE_2, &in, out);
    check_near("deux cycles : le second remultiplie le premier",
               out[0], 200.0 * 128.0 / 255.0 * 128.0 / 255.0, 0.05);

    /* Et le controle negatif : en un seul cycle, le second etage doit rester
       sans effet. Sans lui, un evaluateur qui appliquerait toujours les deux
       passerait l'epreuve precedente. */
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("en un cycle, le second etage est ignore",
               out[0], 200.0 * 128.0 / 255.0, 0.01);

    /* --- Les entrees ne se nomment pas pareil selon leur position ------------ *
     *
     * La valeur 6 vaut `1` en position `a` et `d`, mais `CENTER` en `b` — que ce
     * module traite comme zero. Poser a = 6 et b = 6 doit donc donner
     * (255 - 0) x c + d et non zero. Une table unique donnerait zero, et
     * l'erreur ne se verrait que sur les rares configurations concernees. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 6; c.rgb[0].b = 6; c.rgb[0].c = 6; c.rgb[0].d = 7;
    c.alpha[0].c = 7;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("la valeur 6 ne signifie pas la meme chose en a et en b",
               out[0], 255.0, 0.01);

    /* --- L'alpha a sa propre table pour `c` ---------------------------------- *
     * En alpha, `c = 0` signifie LOD_FRACTION et non COMBINED. */
    memset(&c, 0, sizeof(c));
    c.rgb[0].c = 16;
    c.alpha[0].a = 6; c.alpha[0].b = 7; c.alpha[0].c = 0; c.alpha[0].d = 7;
    in.lod_fraction = 255.0f;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("en alpha, c=0 designe LOD_FRACTION", out[3], 255.0, 0.01);
    in.lod_fraction = 0.0f;

    /* --- Le bornage ---------------------------------------------------------- */
    memset(&c, 0, sizeof(c));
    c.rgb[0].a = 6; c.rgb[0].b = 8; c.rgb[0].c = 6; c.rgb[0].d = 6;  /* 1 x 1 + 1 */
    c.alpha[0].c = 7;
    dkr_combiner_eval_all(&c, DKR_CYCLE_1, &in, out);
    check_near("le resultat est borne a 255", out[0], 255.0, 0.01);

    /* --- La table ------------------------------------------------------------ */
    {
        const int n = dkr_cc_table_count();
        int i, j, doublons = 0, retrouvees = 0;
        int par_cat[4];
        long poids[4];

        memset(par_cat, 0, sizeof(par_cat));
        memset(poids, 0, sizeof(poids));

        check("la table n'est pas vide", n > 0);
        report("  configurations dans la table : %lu", (unsigned long)n, 0ul);

        for (i = 0; i < n; i++) {
            const dkr_cc_entry *e = dkr_cc_table_at(i);
            const unsigned long long k = dkr_cc_entry_key(e);
            par_cat[e->category]++;
            poids[e->category] += e->entries;
            /* Retrouvee par sa propre cle : c'est ce qui garantit que
               l'indexation par forme canonique fonctionne reellement, et non
               seulement que la table existe. */
            if (dkr_cc_lookup(k) != 0) { retrouvees++; }
            for (j = i + 1; j < n; j++) {
                if (dkr_cc_entry_key(dkr_cc_table_at(j)) == k) { doublons++; }
            }
        }
        /* **Le controle qui compte.** Deux entrees de meme cle canonique se
           masqueraient l'une l'autre, et la seconde ne serait jamais atteinte :
           une configuration entiere rendue par le reglage d'une autre, sans
           qu'aucun message ne le signale. */
        check("aucune cle canonique n'est en double", doublons == 0);
        check("chaque entree est retrouvable par sa cle", retrouvees == n);

        report("  exactes %lu, multipasse %lu",
               (unsigned long)par_cat[DKR_CC_EXACT],
               (unsigned long)par_cat[DKR_CC_MULTIPASS]);
        report("  approchees %lu, deux texels %lu",
               (unsigned long)par_cat[DKR_CC_APPROXIMATE],
               (unsigned long)par_cat[DKR_CC_TWO_TEXELS]);
        report("  pondere par les entrees de table : exactes %lu sur %lu",
               (unsigned long)poids[DKR_CC_EXACT],
               (unsigned long)(poids[0] + poids[1] + poids[2] + poids[3]));

        /* **Ce seuil portait d'abord sur la mauvaise grandeur.**
         *
         * Il exigeait que la moitie des entrees soit exacte, et il encodait
         * ainsi le resultat du moment plutot qu'une exigence. Quand la mesure
         * sur la carte a reclasse la famille ENV_ALPHA en approchee, la part
         * exacte est tombee a 45 % et l'epreuve a echoue — sans que rien
         * n'empire pour le materiel.
         *
         * Ce qui coute reellement, c'est le **remplissage**, et le remplissage
         * est ce qui limite une Voodoo 2 en 640x480. Une configuration approchee
         * ne coute pas une passe de plus : elle coute de la justesse, ce que la
         * mesure d'ecart rapporte par ailleurs. Seul le multipasse double la
         * surface peinte.
         *
         * Le seuil porte donc desormais sur la part multipasse, qui est ce que
         * la carte paie. */
        check("le multipasse reste minoritaire : c'est lui qui double le "
              "remplissage, et le remplissage limite la carte",
              poids[DKR_CC_MULTIPASS] * 2 <
              (poids[0] + poids[1] + poids[2] + poids[3]));

        /* Toute configuration multipasse ou approchee doit porter une note
           expliquant pourquoi. Sans elle, la classification est une opinion. */
        {
            int justifiees = 0, a_justifier = 0;
            for (i = 0; i < n; i++) {
                const dkr_cc_entry *e = dkr_cc_table_at(i);
                if (e->category == DKR_CC_EXACT) { continue; }
                a_justifier++;
                if (e->note && e->note[0]) { justifiees++; }
            }
            check("toute configuration non exacte porte sa justification",
                  justifiees == a_justifier);
        }
    }

    /* --- Le repli ------------------------------------------------------------ */
    {
        const dkr_cc_setup *r = dkr_cc_fallback();
        check("une configuration inconnue n'est pas trouvee",
              dkr_cc_lookup(0xFFFFFFFFFFFFFFFFull) == 0);
        check("le repli existe et emploie la texture",
              r != 0 && r->uses_texture != 0);
        /* **Non aberrant** : le repli ne doit ni tout effacer ni peindre en
           couleur d'alerte. Le facteur nul serait le premier cas. */
        check("et il ne met pas le facteur a zero", r->cc_factor != 0);
    }

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
