/* E04-S06 — épreuve du décodage de l'état RDP.
 *
 * Les vecteurs viennent des **en-têtes de la décomposition** — les 63 macros
 * `G_CC_*` résolues par `tools/win95/gen_combiner_vectors.py` — et non d'une
 * transcription à la main. La distinction est ce qui donne sa valeur au test :
 * une transcription se trompe silencieusement, et le test partagerait alors
 * l'erreur du code qu'il vérifie.
 *
 * Ce qui est établi ici est étroit et net : **le décodeur retrouve exactement ce
 * que l'encodeur du jeu a écrit**, pour chaque configuration que la
 * décomposition connaît.
 */
#include "render/rdp_state.h"

#include <stdio.h>
#include <string.h>

#include "combiner_vectors.inc"

static int g_fails;
static FILE *g_out;

static void check(const char *what, int condition)
{
    printf("  %s %s\n", condition ? "ok   " : "ECHEC", what);
    if (g_out) {
        fprintf(g_out, "  %s %s\n", condition ? "ok   " : "ECHEC", what);
        fflush(g_out);
    }
    if (!condition) { g_fails++; }
}

int main(void)
{
    const int count = (int)(sizeof(COMBINER_VECTORS) / sizeof(COMBINER_VECTORS[0]));
    int i, mismatches = 0;
    char label[96];

    g_out = fopen("D:\\RDPSTATE.TXT", "w");

    /* --- Le décodage du combineur ------------------------------------------ */
    for (i = 0; i < count; i++) {
        const combiner_vector *v = &COMBINER_VECTORS[i];
        dkr_combiner c;
        int ok;
        dkr_rdp_decode_combine(v->w0, v->w1, &c);
        ok = c.rgb[0].a == v->a && c.rgb[0].b == v->b &&
             c.rgb[0].c == v->c && c.rgb[0].d == v->d &&
             c.alpha[0].a == v->Aa && c.alpha[0].b == v->Ab &&
             c.alpha[0].c == v->Ac && c.alpha[0].d == v->Ad &&
             /* `gsDPSetCombineMode(x, x)` écrit le même jeu dans les deux
                cycles : le second doit donc se décoder à l'identique. Le
                vérifier prend les erreurs de décalage sur les champs du cycle 1,
                qui sont les plus faciles à confondre. */
             c.rgb[1].a == v->a && c.rgb[1].b == v->b &&
             c.rgb[1].c == v->c && c.rgb[1].d == v->d;
        if (!ok) {
            mismatches++;
            if (mismatches <= 5) {
                sprintf(label, "%s : attendu (%u,%u,%u,%u) obtenu (%u,%u,%u,%u)",
                        v->name, v->a, v->b, v->c, v->d,
                        c.rgb[0].a, c.rgb[0].b, c.rgb[0].c, c.rgb[0].d);
                check(label, 0);
            }
        }
    }
    sprintf(label, "les %d configurations de la decomposition se decodent", count);
    check(label, mismatches == 0);

    /* --- La forme canonique ------------------------------------------------ */
    {
        dkr_combiner a, b;
        dkr_rdp_decode_combine(COMBINER_VECTORS[0].w0, COMBINER_VECTORS[0].w1, &a);
        dkr_rdp_decode_combine(COMBINER_VECTORS[1].w0, COMBINER_VECTORS[1].w1, &b);
        check("deux configurations differentes ont deux cles differentes",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) !=
              dkr_rdp_combiner_key(&b, DKR_CYCLE_1));
        check("la meme configuration donne la meme cle",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) ==
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1));
        /* Le point qui compte : le mode de cycle fait partie de l'identite. Le
           meme mot en un cycle et en deux ne produit pas la meme image, le
           second etage n'etant pas evalue dans le premier cas. */
        check("un cycle et deux cycles ne sont pas la meme configuration",
              dkr_rdp_combiner_key(&a, DKR_CYCLE_1) !=
              dkr_rdp_combiner_key(&a, DKR_CYCLE_2));
    }

    /* Toutes les cles doivent etre distinctes deux a deux : une collision
       ferait correspondre une configuration au reglage Glide d'une autre, et
       l'image serait fausse sans que rien ne le signale. */
    {
        int collisions = 0, j;
        for (i = 0; i < count; i++) {
            dkr_combiner ci;
            dkr_rdp_decode_combine(COMBINER_VECTORS[i].w0, COMBINER_VECTORS[i].w1, &ci);
            for (j = i + 1; j < count; j++) {
                dkr_combiner cj;
                dkr_rdp_decode_combine(COMBINER_VECTORS[j].w0, COMBINER_VECTORS[j].w1, &cj);
                if (memcmp(&ci, &cj, sizeof(ci)) != 0 &&
                    dkr_rdp_combiner_key(&ci, DKR_CYCLE_1) ==
                    dkr_rdp_combiner_key(&cj, DKR_CYCLE_1)) {
                    collisions++;
                }
            }
        }
        check("aucune collision de cle entre configurations distinctes",
              collisions == 0);
    }

    /* --- Les autres modes -------------------------------------------------- */
    {
        dkr_rdp_state s;
        /* G_CYC_2CYCLE = 1 << 20 ; G_TF_BILERP = 2 << 12 ; G_TP_PERSP = 1 << 19 */
        dkr_rdp_decode_othermode((1u << 20) | (2u << 12) | (1u << 19), 0u, &s);
        check("le double cycle est reconnu",   s.cycle  == DKR_CYCLE_2);
        check("le filtrage bilineaire aussi",  s.filter == DKR_FILTER_BILINEAR);
        check("et la perspective de texture",  s.texture_persp == 1);

        dkr_rdp_decode_othermode(0u, 0u, &s);
        check("un cycle par defaut",           s.cycle  == DKR_CYCLE_1);
        check("filtrage point par defaut",     s.filter == DKR_FILTER_POINT);

        /* G_TF_AVERAGE vaut 3, et **il n'y a pas de valeur 1**. Traiter le champ
           comme un booleen donnerait bilineaire pour AVERAGE — presque juste,
           donc difficile a voir. */
        dkr_rdp_decode_othermode(3u << 12, 0u, &s);
        check("G_TF_AVERAGE n'est pas pris pour du point",
              s.filter == DKR_FILTER_BILINEAR);

        dkr_rdp_decode_othermode(0u, (1u << 4) | (1u << 5), &s);
        check("le test de profondeur se lit",  s.z_test  == 1);
        check("et son ecriture aussi",         s.z_write == 1);
    }

    /* --- La traduction vers l'etat abstrait -------------------------------- */
    {
        dkr_rdp_state    s;
        dkr_render_state r;
        dkr_combiner     c;
        int exact = -1;

        /* G_CC_MODULATEIDECALA : (TEXEL0, 0, SHADE, 0), alpha (0,0,0,TEXEL0).
           Texel et shading, alpha du texel : c'est le mode a alpha de texture. */
        memset(&s, 0, sizeof(s));
        memset(&c, 0, sizeof(c));
        c.rgb[0].a = DKR_CC_TEXEL0; c.rgb[0].c = DKR_CC_SHADE;
        c.alpha[0].d = DKR_CC_TEXEL0;
        s.combiner = c; s.cycle = DKR_CYCLE_1;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("texel + shading + alpha du texel se traduit",
              r.combine == DKR_COMBINE_TEXTURE_SHADE_ALPHA);
        check("et la traduction s'annonce exacte", exact == 1);

        /* Deux texels : l'interface ne sait pas le dire, et la traduction doit
           **prevenir**. Une traduction approchee qui ne s'annonce pas produit
           une image plausible et fausse — le pire des resultats. */
        c.rgb[1].a = DKR_CC_TEXEL1;
        s.combiner = c;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("deux texels : la traduction se declare approchee", exact == 0);

        /* Le double cycle aussi. */
        memset(&c, 0, sizeof(c));
        c.rgb[0].a = DKR_CC_TEXEL0; c.rgb[0].c = DKR_CC_SHADE;
        s.combiner = c; s.cycle = DKR_CYCLE_2;
        dkr_rdp_to_render_state(&s, &r, &exact);
        check("double cycle : idem", exact == 0);
    }

    /* --- Le filet de securite ---------------------------------------------- */
    {
        dkr_combiner c;
        memset(&c, 0, sizeof(c));
        /* Une configuration que personne n'a repertoriee. */
        c.rgb[0].a = 13; c.rgb[0].b = 11; c.rgb[0].c = 29; c.rgb[0].d = 6;
        check("une configuration inconnue n'est pas nommee",
              dkr_rdp_combiner_name(dkr_rdp_combiner_key(&c, DKR_CYCLE_1)) == NULL);
        check("l'inventaire repertorie quelques configurations",
              dkr_rdp_known_count() > 0);
        {
            unsigned long long key = 0;
            const char *name = NULL;
            int texels = -1;
            check("et l'on peut le parcourir",
                  dkr_rdp_known_at(0, &key, &name, &texels) && name != NULL &&
                  texels >= 0);
            check("chaque entree repertoriee se retrouve par sa cle",
                  dkr_rdp_combiner_name(key) != NULL);
        }
    }

    printf("\n%d echec(s)\n", g_fails);
    if (g_out) { fprintf(g_out, "\n%d echec(s)\n", g_fails); fclose(g_out); }
    return g_fails != 0;
}
