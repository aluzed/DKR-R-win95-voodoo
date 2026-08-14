#!/usr/bin/env python3
"""E05-S03 — engendre la table de correspondance combineur RDP -> Glide.

Les 33 configurations viennent de l'inventaire du portage voisin
(`combiner-inventory.md`), lui-meme engendre depuis les tables statiques du jeu.
Leurs definitions viennent de `include/PR/gbi.h` et `include/f3ddkr.h`.

**Pourquoi engendrer plutot qu'ecrire.** Trente-trois quadruplets recopies a la
main invitent la faute de frappe, et une faute ici ne provoque aucune erreur :
elle produit un rendu faux sur une seule configuration, donc sur un seul type de
surface, ce qui se remarque tard et s'impute mal. La conversion nom -> valeur
numerique est mecanique ; elle doit l'etre.

**La classification est faite ici par regles explicites**, pas cas par cas. Le
ticket met en garde contre la tentation de traiter les configurations une par une
« jusqu'a ce que ca ressemble ». Des regles se relisent, se discutent, et
s'appliquent uniformement.
"""
import sys

# --- Les definitions, relevees dans la source du jeu -------------------------
SRC = {
 "G_CC_PRIMITIVE":            "0,0,0,PRIMITIVE, 0,0,0,PRIMITIVE",
 "G_CC_SHADE":                "0,0,0,SHADE, 0,0,0,SHADE",
 "G_CC_MODULATEIA":           "TEXEL0,0,SHADE,0, TEXEL0,0,SHADE,0",
 "G_CC_MODULATEIDECALA":      "TEXEL0,0,SHADE,0, 0,0,0,TEXEL0",
 "G_CC_MODULATERGBA":         "TEXEL0,0,SHADE,0, TEXEL0,0,SHADE,0",
 "G_CC_MODULATEIA_PRIM":      "TEXEL0,0,PRIMITIVE,0, TEXEL0,0,PRIMITIVE,0",
 "G_CC_DECALRGB":             "0,0,0,TEXEL0, 0,0,0,SHADE",
 "G_CC_DECALRGBA":            "0,0,0,TEXEL0, 0,0,0,TEXEL0",
 "G_CC_BLENDPE":              "PRIMITIVE,ENVIRONMENT,TEXEL0,ENVIRONMENT, TEXEL0,0,SHADE,0",
 "G_CC_PASS2":                "0,0,0,COMBINED, 0,0,0,COMBINED",
 "G_CC_MODULATEIA_PRIM2":     "COMBINED,0,PRIMITIVE,0, COMBINED,0,PRIMITIVE,0",
 "G_CC_ENVIRONMENT":          "0,0,0,ENVIRONMENT, 0,0,0,ENVIRONMENT",
 "G_CC_BLENDTEX_MODULATEA_1_PRIM":"TEXEL1,TEXEL0,SHADE,TEXEL0, 1,TEXEL0,PRIMITIVE,TEXEL0",
 "G_CC_BLENDI_ENV_ALPHA":     "ENVIRONMENT,SHADE,ENV_ALPHA,SHADE, 0,0,0,SHADE",
 "G_CC_BLENDI_ENV_ALPHA_A_PRIM":"ENVIRONMENT,SHADE,ENV_ALPHA,SHADE, 0,0,0,PRIMITIVE",
 "G_CC_BLENDT_ENV_ALPHA_A_PRIM":"ENVIRONMENT,TEXEL0,ENV_ALPHA,TEXEL0, 0,0,0,PRIMITIVE",
 "G_CC_BLENDT_ENV_ALPHA_A_TxP":"ENVIRONMENT,TEXEL0,ENV_ALPHA,TEXEL0, TEXEL0,0,PRIMITIVE,0",
 "G_CC_BLENDT_ENV_ALPHA_A_T1xP":"ENVIRONMENT,TEXEL0,ENV_ALPHA,TEXEL0, TEXEL1,0,PRIMITIVE,0",
 "G_CC_DECAL_A_PRIM":         "0,0,0,TEXEL0, TEXEL0,0,PRIMITIVE,0",
 "G_CC_BLEND_SHADEALPHA":     "TEXEL0,PRIMITIVE,SHADE_ALPHA,PRIMITIVE, TEXEL0,0,PRIMITIVE,0",
 "G_CC_BLENDPE_A_PRIM":       "PRIMITIVE,ENVIRONMENT,TEXEL0,ENVIRONMENT, TEXEL0,0,PRIMITIVE,0",
 "G_CC_DECAL_SCALE":          "TEXEL0,0,SCALE,0, 0,0,0,TEXEL0",
 "G_CC_ENV_DECALA":           "0,0,0,ENVIRONMENT, 0,0,0,TEXEL0",
 "G_CC_BLENDTEX_PRIM":        "TEXEL1,TEXEL0,PRIMITIVE,TEXEL0, TEXEL1,TEXEL0,PRIMITIVE,TEXEL0",
 "G_CC_MODULATEA_PRIM2":      "0,0,0,COMBINED, COMBINED,0,PRIMITIVE,0",
 "G_CC_BLENDI_ENV_ALPHA_PRIM2":"ENVIRONMENT,COMBINED,ENV_ALPHA,COMBINED, COMBINED,0,PRIMITIVE,0",
 "G_CC_BLEND_ENV_ALPHA2":     "ENVIRONMENT,COMBINED,ENV_ALPHA,COMBINED, 0,0,0,COMBINED",
 "G_CC_BLENDI_SHADE":         "ENVIRONMENT,COMBINED,SHADE,COMBINED, 0,0,0,COMBINED",
 "G_CC_BLENDI_ENV_ALPHA_MODULATEA2":"ENVIRONMENT,COMBINED,ENV_ALPHA,COMBINED, COMBINED,0,SHADE,0",
 "G_CC_MODULATEIDECALA2":     "COMBINED,0,SHADE,0, 0,0,0,COMBINED",
}

# --- Les valeurs numeriques, par position ------------------------------------
# Le meme nom n'a pas la meme valeur selon la position : c'est le piege que le
# decodeur de E04-S06 documente deja, et le reproduire ici serait une facon
# silencieuse de le reintroduire.
A = {"COMBINED":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,"1":6,"NOISE":7,"0":8}
B = {"COMBINED":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,"CENTER":6,"K4":7,"0":8}
C = {"COMBINED":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,"SCALE":6,
     "COMBINED_ALPHA":7,"TEXEL0_ALPHA":8,"TEXEL1_ALPHA":9,"PRIMITIVE_ALPHA":10,"SHADE_ALPHA":11,
     "ENV_ALPHA":12,"LOD_FRACTION":13,"PRIM_LOD_FRAC":14,"K5":15,"0":16}
D = {"COMBINED":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,"1":6,"0":7}
AA= {"COMBINED":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,"1":6,"0":7}
AC= {"LOD_FRACTION":0,"TEXEL0":1,"TEXEL1":2,"PRIMITIVE":3,"SHADE":4,"ENVIRONMENT":5,
     "PRIM_LOD_FRAC":6,"0":7,"SHADE_ALPHA":4,"ENV_ALPHA":5,"PRIMITIVE_ALPHA":3,
     "TEXEL0_ALPHA":1,"TEXEL1_ALPHA":2}

def parse(name):
    p = [x.strip() for x in SRC[name].replace(" ", "").split(",")]
    assert len(p) == 8, (name, p)
    return ([A[p[0]], B[p[1]], C[p[2]], D[p[3]]],
            [AA[p[4]], AA[p[5]], AC[p[6]], AA[p[7]]],
            p)

# --- Les configurations employees, d'apres l'inventaire ----------------------
# (cycle1, cycle2 ou None, entrees de table)
CONFIGS = [
 ("G_CC_BLENDTEX_PRIM", "G_CC_MODULATEIDECALA2", 32),
 ("G_CC_MODULATEIDECALA", None, 32),
 ("G_CC_MODULATEIDECALA", "G_CC_BLENDI_ENV_ALPHA_PRIM2", 20),
 ("G_CC_MODULATEIDECALA", "G_CC_PASS2", 20),
 ("G_CC_MODULATEIA_PRIM", None, 18),
 ("G_CC_MODULATEIDECALA", "G_CC_MODULATEIA_PRIM", 8),
 ("G_CC_BLENDI_ENV_ALPHA_A_PRIM", None, 8),
 ("G_CC_MODULATEIA_PRIM", "G_CC_BLEND_ENV_ALPHA2", 8),
 ("G_CC_BLENDI_ENV_ALPHA_A_PRIM", "G_CC_MODULATEIA_PRIM2", 8),
 ("G_CC_BLEND_SHADEALPHA", "G_CC_BLENDI_SHADE", 16),
 ("G_CC_DECAL_A_PRIM", None, 8),
 ("G_CC_MODULATERGBA", "G_CC_MODULATEA_PRIM2", 4),
 ("G_CC_MODULATERGBA", "G_CC_BLENDI_ENV_ALPHA_PRIM2", 4),
 ("G_CC_BLENDI_ENV_ALPHA", "G_CC_MODULATEIA_PRIM2", 4),
 ("G_CC_BLENDPE_A_PRIM", None, 1),
 ("G_CC_BLENDPE_A_PRIM", "G_CC_PASS2", 1),
 ("G_CC_BLENDPE", None, 1),
 ("G_CC_PRIMITIVE", None, 9),
 ("G_CC_SHADE", None, 2),
 ("G_CC_BLENDT_ENV_ALPHA_A_TxP", None, 1),
 ("G_CC_ENVIRONMENT", None, 1),
 ("G_CC_BLENDT_ENV_ALPHA_A_T1xP", "G_CC_PASS2", 1),
 ("G_CC_MODULATEIA", None, 1),
 ("G_CC_ENV_DECALA", None, 1),
 ("G_CC_DECALRGB", None, 1),
 ("G_CC_DECALRGBA", None, 1),
 ("G_CC_DECAL_SCALE", None, 1),
 ("G_CC_BLENDTEX_MODULATEA_1_PRIM", "G_CC_BLENDI_ENV_ALPHA_MODULATEA2", 1),
 ("G_CC_BLENDT_ENV_ALPHA_A_PRIM", "G_CC_MODULATEIDECALA2", 1),
]

# --- Les enumerations de Glide 2.x, telles que glide_backend.c les emploie ---
FN  = {"ZERO":0, "LOCAL":1, "SCALE_OTHER":3, "SCALE_OTHER_ADD_LOCAL":4, "BLEND":7}
FAC = {"ZERO":0, "LOCAL":1, "OTHER_ALPHA":2, "LOCAL_ALPHA":3, "TEXTURE_ALPHA":4, "ONE":8}
LOC = {"ITERATED":0, "CONSTANT":1}
OTH = {"ITERATED":0, "TEXTURE":1, "CONSTANT":2}

def _base(part):
    """Le registre que designe une entree, sans son suffixe d'alpha."""
    if part.startswith("ENV_ALPHA"):
        return "ENVIRONMENT"
    if part.startswith("SHADE"):
        return "SHADE"
    return part.replace("_ALPHA", "")

def sources(names, terme="tout"):
    """Quelles sources RDP la configuration lit.

    `terme` vaut « rgb », « alpha » ou « tout ». **Les distinguer est ce qui
    permet de ne pas declarer le mur trop tot** : Glide n'a qu'un registre
    constant, mais si le terme de couleur n'en lit qu'un et le terme d'alpha
    l'autre, le second peut etre porte autrement — voir `classify`."""
    used = set()
    for n in names:
        parts = [x.strip() for x in SRC[n].replace(" ", "").split(",")]
        champ = parts[0:4] if terme == "rgb" else parts[4:8] if terme == "alpha" else parts
        for part in champ:
            if part not in ("0", "1"):
                used.add(_base(part))
    return used

def fold(c1, c2):
    """Replie le second cycle quand il est reductible.

    Le ticket nomme lui-meme cette sortie : « simplification quand le second
    etage est neutre ». Deux cas se replient, et les reconnaitre change la
    classification de la moitie de l'inventaire :

    - `G_CC_PASS2` vaut `(0,0,0,COMBINED)`, c'est-a-dire **l'identite**. La
      configuration a deux cycles se comporte exactement comme son premier.
    - un second etage de la forme `(COMBINED, 0, X, 0)` est une mise a l'echelle
      du resultat precedent. Si le premier cycle tient dans le « other » de
      Glide, le compose vaut `other x X`, qui est un `SCALE_OTHER` — donc une
      seule passe.

    Rend le nom du cycle a classer, et le facteur replie s'il y en a un. Sans ce
    repliage, tout second cycle serait declare multipasse, ce qui doublerait le
    remplissage sur les surfaces les plus courantes du jeu pour rien.
    """
    if c2 is None:
        return c1, None
    r2, a2, p2 = parse(c2)
    if p2[0] == "0" and p2[1] == "0" and p2[2] == "0" and p2[3] == "COMBINED":
        return c1, None                       # identite
    if p2[0] == "COMBINED" and p2[1] == "0" and p2[3] == "0":
        return c1, p2[2]                      # mise a l'echelle par p2[2]
    return c1, "IRREDUCTIBLE"

def classify(c1, c2):
    """Range la configuration. Les regles, dans l'ordre ou elles s'appliquent."""
    names = [c1] + ([c2] if c2 else [])
    used = sources(names)

    # 1. Deux texels : c'est la seconde TMU, donc E05-S04. Ce n'est pas une
    #    approximation, c'est un renvoi.
    if "TEXEL1" in used:
        return ("DKR_CC_DEUX_TEXELS", "DKR_CONST_AUCUNE",
                "lit TEXEL1 : renvoye a E05-S04, seconde TMU")

    # 2. Les deux registres constants a la fois. **C'est le mur** : le RDP en a
    #    deux, Glide un seul.
    #
    #    Mais il ne faut pas le declarer trop tot. Glide a un combineur de
    #    couleur et un combineur d'alpha **separes**, et la valeur d'alpha d'un
    #    sommet nous appartient — la chaine l'ecrit. Si le terme de couleur ne
    #    lit qu'un registre constant et le terme d'alpha l'autre, le second peut
    #    etre **porte par la couleur du sommet** : les deux sont constants par
    #    appel de dessin, donc connus du processeur au moment d'ecrire les
    #    sommets. Cela ne coute rien et evite une passe entiere.
    #
    #    La manoeuvre a un prix, et il est nomme : l'alpha du sommet ne peut plus
    #    porter autre chose. Elle ne s'applique donc que si le terme d'alpha ne
    #    lit pas SHADE par ailleurs.
    rgb_used   = sources(names, "rgb")
    alpha_used = sources(names, "alpha")
    deux = "PRIMITIVE" in used and "ENVIRONMENT" in used
    if deux:
        rgb_c   = {x for x in rgb_used   if x in ("PRIMITIVE", "ENVIRONMENT")}
        alpha_c = {x for x in alpha_used if x in ("PRIMITIVE", "ENVIRONMENT")}
        portable = (len(rgb_c) <= 1 and len(alpha_c) <= 1 and rgb_c != alpha_c
                    and "SHADE" not in alpha_used)
        if not portable:
            return ("DKR_CC_MULTIPASSE", "DKR_CONST_LES_DEUX",
                    "lit PRIMITIVE et ENVIRONMENT dans le meme terme : "
                    "Glide n'a qu'un registre constant")
        const = ("DKR_CONST_PRIMITIVE" if "PRIMITIVE" in rgb_c
                 else "DKR_CONST_ENVIRONMENT")
        # on continue la classification : la seconde constante est portee par
        # l'alpha du sommet, qui n'est plus disponible pour autre chose.
    else:
        const = ("DKR_CONST_PRIMITIVE" if "PRIMITIVE" in used else
                 "DKR_CONST_ENVIRONMENT" if "ENVIRONMENT" in used else
                 "DKR_CONST_AUCUNE")

    rgb, alpha, parts = parse(c1)

    # 3a. **La forme degeneree `(0, 0, 0, X)`.** Le resultat vaut X, sans calcul.
    #     Elle ne peut pas satisfaire le test `d == b` qui suit — b vaut zero et
    #     d ne vaut pas zero — et la premiere version de ces regles la rangeait
    #     donc en « approchee ». `G_CC_PRIMITIVE`, une simple couleur constante,
    #     s'y retrouvait classee inatteignable, ce qui est absurde et signalait
    #     que l'ordre des regles etait faux plutot que les regles elles-memes.
    if parts[0] == "0" and parts[1] == "0" and parts[2] == "0":
        if c2:
            _, facteur = fold(c1, c2)
            if facteur is not None and facteur != "IRREDUCTIBLE":
                return ("DKR_CC_EXACTE", const,
                        "resultat direct, second etage replie en SCALE_OTHER")
            if facteur == "IRREDUCTIBLE":
                return ("DKR_CC_MULTIPASSE", const,
                        "resultat direct, mais second cycle irreductible")
        return ("DKR_CC_EXACTE", const,
                "forme degeneree (0,0,0,X) : le resultat vaut X, sans calcul")
    # 3. `d == b` : la forme du RDP tombe alors exactement sur la fonction BLEND
    #    de Glide, `f x (other - local) + local`.
    zero_b = parts[1] == "0"
    zero_d = parts[3] == "0"
    forme_ok = (parts[1] == parts[3]) or (zero_b and zero_d)
    if not forme_ok:
        return ("DKR_CC_APPROCHEE", const,
                "d different de b : la forme ne tombe pas sur BLEND")

    # 4. Le facteur `c` doit etre exprimable. Glide n'offre en facteur que la
    #    couleur locale ou une alpha ; une couleur *de texture* en facteur n'y
    #    est pas.
    c_name = parts[2]
    if c_name in ("TEXEL0", "TEXEL1"):
        return ("DKR_CC_APPROCHEE", const,
                "facteur = couleur de texture : Glide n'offre que TEXTURE_ALPHA")
    # **Mesure, et non deduction.** Le balayage des seize valeurs de facteur sur
    # la carte (`combine_enum_probe.c`) a etabli qu'*aucune* ne delivre l'alpha
    # du registre constant : les facteurs confirmes sont ONE, LOCAL et
    # ONE_MINUS_LOCAL, tous fonctions de la couleur locale ou de rien.
    #
    # Toute la famille BLENDI/BLENDT repose sur `ENV_ALPHA` en facteur, et se
    # trouve donc hors d'atteinte en l'etat. Elle etait classee « exacte » sur la
    # foi de valeurs d'enumeration ecrites de memoire ; le harnais de mesure l'a
    # prise en defaut avec 140 a 156 unites d'ecart, et c'est exactement ce qu'on
    # lui demandait de faire.
    #
    # Une issue existe et n'est pas encore eprouvee : ENV_ALPHA est une constante
    # connue du processeur, donc portable dans l'alpha du sommet, ou
    # `LOCAL_ALPHA` irait la chercher. Tant que ce n'est pas mesure, la
    # configuration reste approchee — annoncer exact ce qui ne l'est pas est
    # precisement le defaut contre lequel ce ticket met en garde.
    if c_name in ("ENV_ALPHA", "PRIMITIVE_ALPHA"):
        return ("DKR_CC_APPROCHEE", const,
                "facteur = alpha d'un registre constant : mesure sur la carte, "
                "aucun facteur Glide ne le delivre")
    if c_name == "SHADE_ALPHA" and const != "DKR_CONST_AUCUNE":
        # local est la constante, donc LOCAL_ALPHA vaut l'alpha de la constante
        # et non celle du sommet.
        return ("DKR_CC_APPROCHEE", const,
                "facteur = alpha du sommet alors que local est la constante")
    if c_name == "SCALE":
        return ("DKR_CC_APPROCHEE", const,
                "SCALE : registre de mise a l'echelle du RDP, sans equivalent")
    if c2:
        _, facteur = fold(c1, c2)
        if facteur is None:
            return ("DKR_CC_EXACTE", const,
                    "second etage neutre (PASS2) : se replie sur le premier")
        if facteur == "IRREDUCTIBLE":
            return ("DKR_CC_MULTIPASSE", const,
                    "second cycle irreductible : un etage de plus que Glide n'en offre")
        if facteur in ("SHADE", "PRIMITIVE", "ENVIRONMENT", "TEXEL0_ALPHA"):
            return ("DKR_CC_EXACTE", const,
                    "second etage = mise a l'echelle : se replie en SCALE_OTHER")
        return ("DKR_CC_APPROCHEE", const,
                "second etage a l'echelle par une source inexprimable en facteur")
    return ("DKR_CC_EXACTE", const, "forme (a-b)c+d avec d=b, facteur exprimable")

def recipe(c1, cat):
    """Le reglage Glide. Seules les configurations exactes en recoivent un
    complet ; les autres portent celui de leur approximation, qui est mesuree."""
    rgb, alpha, parts = parse(c1)
    a, b, c, d = parts[0], parts[1], parts[2], parts[3]
    use_tex = 1 if "TEXEL" in (a + b + c + d) else 0

    if a == "0" and b == "0" and c == "0":
        # Resultat = d, une constante ou une source simple.
        if d == "TEXEL0":
            return (FN["SCALE_OTHER"], FAC["ONE"], LOC["ITERATED"], OTH["TEXTURE"], 1)
        if d == "SHADE":
            return (FN["LOCAL"], FAC["ONE"], LOC["ITERATED"], OTH["ITERATED"], 0)
        return (FN["LOCAL"], FAC["ONE"], LOC["CONSTANT"], OTH["CONSTANT"], 0)

    if b == "0" and d == "0":
        # Resultat = a x c : une simple mise a l'echelle.
        fac = (FAC["LOCAL"] if c in ("SHADE", "PRIMITIVE", "ENVIRONMENT")
               else FAC["TEXTURE_ALPHA"] if c == "TEXEL0_ALPHA" else FAC["LOCAL"])
        loc = LOC["ITERATED"] if c == "SHADE" else LOC["CONSTANT"]
        oth = OTH["TEXTURE"] if a == "TEXEL0" else OTH["ITERATED"]
        return (FN["SCALE_OTHER"], fac, loc, oth, use_tex)

    # Forme BLEND : f x (other - local) + local.
    oth = (OTH["TEXTURE"] if a == "TEXEL0" else
           OTH["CONSTANT"] if a in ("PRIMITIVE", "ENVIRONMENT") else OTH["ITERATED"])
    loc = LOC["ITERATED"] if b == "SHADE" else LOC["CONSTANT"]
    fac = (FAC["OTHER_ALPHA"] if c in ("ENV_ALPHA", "PRIMITIVE_ALPHA") and
           oth == OTH["CONSTANT"] else
           FAC["LOCAL_ALPHA"] if c in ("ENV_ALPHA", "PRIMITIVE_ALPHA") else
           FAC["TEXTURE_ALPHA"] if c == "TEXEL0_ALPHA" else FAC["LOCAL"])
    return (FN["BLEND"], fac, loc, oth, use_tex)

def emit():
    out = []
    out.append("/* ENGENDRE par tools/win95/gen_combiner_table.py — ne pas editer. */")
    out.append("/* Les definitions viennent de include/PR/gbi.h et include/f3ddkr.h du")
    out.append("   portage voisin ; la liste des configurations employees vient de")
    out.append("   docs/research/combiner-inventory.md, engendre depuis les tables")
    out.append("   statiques du jeu. */")
    out.append("static const dkr_cc_entree CC_TABLE[] = {")
    seen = set()
    stats = {}
    for c1, c2, entrees in CONFIGS:
        key = (c1, c2)
        if key in seen:
            continue
        seen.add(key)
        cat, const, note = classify(c1, c2)
        stats[cat] = stats.get(cat, 0) + 1
        r1, a1, _ = parse(c1)
        r2, a2 = ([0, 0, 16, 0], [0, 0, 7, 0])
        if c2:
            r2, a2, _ = parse(c2)
        fn, fac, loc, oth, use_tex = recipe(c1, cat)
        cyc = "DKR_CYCLE_2" if c2 else "DKR_CYCLE_1"
        n2 = f'"{c2}"' if c2 else "0"
        out.append(f'    {{ "{c1}", {n2},')
        out.append(f'      {{ {{{r1[0]},{r1[1]},{r1[2]},{r1[3]}}}, {{{r2[0]},{r2[1]},{r2[2]},{r2[3]}}} }},')
        out.append(f'      {{ {{{a1[0]},{a1[1]},{a1[2]},{a1[3]}}}, {{{a2[0]},{a2[1]},{a2[2]},{a2[3]}}} }},')
        out.append(f'      {cyc}, {cat}, {const},')
        out.append(f'      {{ {fn}, {fac}, {loc}, {oth}, {fn}, {fac}, {loc}, {oth}, 1, 0, {use_tex} }},')
        out.append(f'      "{note}", {entrees} }},')
    out.append("};")
    sys.stderr.write("classification :\n")
    for k in sorted(stats):
        sys.stderr.write(f"  {k:22s} {stats[k]}\n")
    sys.stderr.write(f"  total                  {len(seen)}\n")
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    sys.stdout.write(emit())
