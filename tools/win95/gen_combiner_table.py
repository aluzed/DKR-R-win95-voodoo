#!/usr/bin/env python3
"""E05-S03 - generates the RDP -> Glide combiner mapping table.

The 33 configurations come from the neighbouring port's inventory
(`combiner-inventory.md`), itself generated from the game's static tables.
Their definitions come from `include/PR/gbi.h` and `include/f3ddkr.h`.

**Why generate rather than write.** Thirty-three quadruples copied by hand
invite typos, and a typo here raises no error: it renders one configuration
wrongly, hence one kind of surface, which gets noticed late and blamed on the
wrong thing. The name -> numeric value conversion is mechanical; it should be.

**The classification is done here by explicit rules**, not case by case. The
ticket warns against the temptation to handle configurations one at a time
"until it looks about right". Rules can be re-read, argued with, and applied
uniformly.
"""
import sys

# --- The definitions, taken from the game's source ---------------------------
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

# --- The numeric values, per position ----------------------------------------
# The same name does not have the same value depending on its position: that is
# the trap the E04-S06 decoder already documents, and reproducing it here would
# be a silent way of reintroducing it.
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

# --- The configurations in use, according to the inventory -------------------
# (cycle1, cycle2 or None, table entries)
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

# --- The Glide 2.x enumerations, as glide_backend.c uses them ----------------
FN  = {"ZERO":0, "LOCAL":1, "SCALE_OTHER":3, "SCALE_OTHER_ADD_LOCAL":4, "BLEND":7}
FAC = {"ZERO":0, "LOCAL":1, "OTHER_ALPHA":2, "LOCAL_ALPHA":3, "TEXTURE_ALPHA":4, "ONE":8}
LOC = {"ITERATED":0, "CONSTANT":1}
OTH = {"ITERATED":0, "TEXTURE":1, "CONSTANT":2}

def _base(part):
    """The register an input designates, without its alpha suffix."""
    if part.startswith("ENV_ALPHA"):
        return "ENVIRONMENT"
    if part.startswith("SHADE"):
        return "SHADE"
    return part.replace("_ALPHA", "")

def sources(names, term="all"):
    """Which RDP sources the configuration reads.

    `term` is "rgb", "alpha" or "all". **Telling them apart is what keeps us
    from declaring the wall too early**: Glide has only one constant register,
    but if the colour term reads only one and the alpha term the other, the
    second can be carried differently - see `classify`."""
    used = set()
    for n in names:
        parts = [x.strip() for x in SRC[n].replace(" ", "").split(",")]
        field = parts[0:4] if term == "rgb" else parts[4:8] if term == "alpha" else parts
        for part in field:
            if part not in ("0", "1"):
                used.add(_base(part))
    return used

def fold(c1, c2):
    """Folds the second cycle away when it is reducible.

    The ticket names this way out itself: "simplification when the second stage
    is neutral". Two cases fold, and recognising them changes the classification
    of half the inventory:

    - `G_CC_PASS2` is `(0,0,0,COMBINED)`, that is, **the identity**. The
      two-cycle configuration behaves exactly like its first cycle.
    - a second stage of the form `(COMBINED, 0, X, 0)` scales the previous
      result. If the first cycle fits in Glide's "other", the composition is
      `other x X`, which is a `SCALE_OTHER` - hence a single pass.

    Returns the name of the cycle to classify, and the folded factor if there is
    one. Without this folding, every second cycle would be declared multipass,
    which would double the fill on the game's most common surfaces for nothing.
    """
    if c2 is None:
        return c1, None
    r2, a2, p2 = parse(c2)
    if p2[0] == "0" and p2[1] == "0" and p2[2] == "0" and p2[3] == "COMBINED":
        return c1, None                       # identity
    if p2[0] == "COMBINED" and p2[1] == "0" and p2[3] == "0":
        return c1, p2[2]                      # scaling by p2[2]
    return c1, "IRREDUCIBLE"

def classify(c1, c2):
    """Files the configuration. The rules, in the order they apply."""
    names = [c1] + ([c2] if c2 else [])
    used = sources(names)

    # 1. Two texels: that is the second TMU, hence E05-S04. This is not an
    #    approximation, it is a referral.
    if "TEXEL1" in used:
        return ("DKR_CC_TWO_TEXELS", "DKR_CONST_NONE",
                "reads TEXEL1: deferred to E05-S04, second TMU")

    # 2. Both constant registers at once. **This is the wall**: the RDP has two,
    #    Glide only one.
    #
    #    But it must not be declared too early. Glide has **separate** colour and
    #    alpha combiners, and a vertex's alpha value belongs to us - the chain
    #    writes it. If the colour term reads only one constant register and the
    #    alpha term the other, the second can be **carried by the vertex
    #    colour**: both are constant per draw call, hence known to the CPU at the
    #    time the vertices are written. It costs nothing and saves a whole pass.
    #
    #    The manoeuvre has a price, and it is named: the vertex alpha can no
    #    longer carry anything else. It therefore only applies if the alpha term
    #    does not otherwise read SHADE.
    rgb_used   = sources(names, "rgb")
    alpha_used = sources(names, "alpha")
    both = "PRIMITIVE" in used and "ENVIRONMENT" in used
    if both:
        rgb_c   = {x for x in rgb_used   if x in ("PRIMITIVE", "ENVIRONMENT")}
        alpha_c = {x for x in alpha_used if x in ("PRIMITIVE", "ENVIRONMENT")}
        carriable = (len(rgb_c) <= 1 and len(alpha_c) <= 1 and rgb_c != alpha_c
                     and "SHADE" not in alpha_used)
        if not carriable:
            return ("DKR_CC_MULTIPASS", "DKR_CONST_BOTH",
                    "reads PRIMITIVE and ENVIRONMENT in the same term: "
                    "Glide has only one constant register")
        const = ("DKR_CONST_PRIMITIVE" if "PRIMITIVE" in rgb_c
                 else "DKR_CONST_ENVIRONMENT")
        # classification continues: the second constant is carried by the vertex
        # alpha, which is no longer available for anything else.
    else:
        const = ("DKR_CONST_PRIMITIVE" if "PRIMITIVE" in used else
                 "DKR_CONST_ENVIRONMENT" if "ENVIRONMENT" in used else
                 "DKR_CONST_NONE")

    rgb, alpha, parts = parse(c1)

    # 3a. **The degenerate form `(0, 0, 0, X)`.** The result is X, with no
    #     computation. It cannot satisfy the `d == b` test that follows - b is
    #     zero and d is not - and the first version of these rules therefore
    #     filed it under "approximate". `G_CC_PRIMITIVE`, a plain constant
    #     colour, ended up classified as unreachable, which is absurd and
    #     signalled that the order of the rules was wrong rather than the rules
    #     themselves.
    if parts[0] == "0" and parts[1] == "0" and parts[2] == "0":
        if c2:
            _, factor = fold(c1, c2)
            if factor is not None and factor != "IRREDUCIBLE":
                return ("DKR_CC_EXACT", const,
                        "direct result, second stage folded into SCALE_OTHER")
            if factor == "IRREDUCIBLE":
                return ("DKR_CC_MULTIPASS", const,
                        "direct result, but irreducible second cycle")
        return ("DKR_CC_EXACT", const,
                "degenerate form (0,0,0,X): the result is X, with no computation")
    # 3. `d == b`: the RDP form then lands exactly on Glide's BLEND function,
    #    `f x (other - local) + local`.
    zero_b = parts[1] == "0"
    zero_d = parts[3] == "0"
    form_ok = (parts[1] == parts[3]) or (zero_b and zero_d)
    if not form_ok:
        return ("DKR_CC_APPROXIMATE", const,
                "d differs from b: the form does not land on BLEND")

    # 4. The factor `c` must be expressible. Glide only offers the local colour
    #    or an alpha as a factor; a *texture* colour as a factor is not there.
    c_name = parts[2]
    if c_name in ("TEXEL0", "TEXEL1"):
        return ("DKR_CC_APPROXIMATE", const,
                "factor = texture colour: Glide only offers TEXTURE_ALPHA")
    # **Measured, not deduced.** Sweeping the sixteen factor values on the card
    # (`combine_enum_probe.c`) established that *none* delivers the constant
    # register's alpha: the confirmed factors are ONE, LOCAL and ONE_MINUS_LOCAL,
    # all functions of the local colour or of nothing.
    #
    # The whole BLENDI/BLENDT family rests on `ENV_ALPHA` as a factor, and is
    # therefore out of reach as things stand. It was classified as "exact" on the
    # strength of enumeration values written from memory; the measurement harness
    # caught it out with a gap of 140 to 156 units, and that is exactly what it
    # was asked to do.
    #
    # A way out exists and is not yet tested: ENV_ALPHA is a constant known to
    # the CPU, hence carriable in the vertex alpha, where `LOCAL_ALPHA` would go
    # and fetch it. Until that is measured, the configuration stays approximate -
    # announcing as exact what is not is precisely the failing this ticket warns
    # against.
    if c_name in ("ENV_ALPHA", "PRIMITIVE_ALPHA"):
        return ("DKR_CC_APPROXIMATE", const,
                "factor = alpha of a constant register: measured on the card, "
                "no Glide factor delivers it")
    if c_name == "SHADE_ALPHA" and const != "DKR_CONST_NONE":
        # local is the constant, so LOCAL_ALPHA is the constant's alpha and not
        # the vertex's.
        return ("DKR_CC_APPROXIMATE", const,
                "factor = vertex alpha while local is the constant")
    if c_name == "SCALE":
        return ("DKR_CC_APPROXIMATE", const,
                "SCALE: the RDP's scaling register, with no equivalent")
    if c2:
        _, factor = fold(c1, c2)
        if factor is None:
            return ("DKR_CC_EXACT", const,
                    "neutral second stage (PASS2): folds onto the first")
        if factor == "IRREDUCIBLE":
            return ("DKR_CC_MULTIPASS", const,
                    "irreducible second cycle: one stage more than Glide offers")
        if factor in ("SHADE", "PRIMITIVE", "ENVIRONMENT", "TEXEL0_ALPHA"):
            return ("DKR_CC_EXACT", const,
                    "second stage = scaling: folds into SCALE_OTHER")
        return ("DKR_CC_APPROXIMATE", const,
                "second stage scaled by a source inexpressible as a factor")
    return ("DKR_CC_EXACT", const, "form (a-b)c+d with d=b, expressible factor")

def recipe(c1, cat):
    """The Glide setup. Only exact configurations get a complete one; the others
    carry the setup of their approximation, which is measured."""
    rgb, alpha, parts = parse(c1)
    a, b, c, d = parts[0], parts[1], parts[2], parts[3]
    use_tex = 1 if "TEXEL" in (a + b + c + d) else 0

    if a == "0" and b == "0" and c == "0":
        # Result = d, a constant or a simple source.
        if d == "TEXEL0":
            return (FN["SCALE_OTHER"], FAC["ONE"], LOC["ITERATED"], OTH["TEXTURE"], 1)
        if d == "SHADE":
            return (FN["LOCAL"], FAC["ONE"], LOC["ITERATED"], OTH["ITERATED"], 0)
        return (FN["LOCAL"], FAC["ONE"], LOC["CONSTANT"], OTH["CONSTANT"], 0)

    if b == "0" and d == "0":
        # Result = a x c: a plain scaling.
        fac = (FAC["LOCAL"] if c in ("SHADE", "PRIMITIVE", "ENVIRONMENT")
               else FAC["TEXTURE_ALPHA"] if c == "TEXEL0_ALPHA" else FAC["LOCAL"])
        loc = LOC["ITERATED"] if c == "SHADE" else LOC["CONSTANT"]
        oth = OTH["TEXTURE"] if a == "TEXEL0" else OTH["ITERATED"]
        return (FN["SCALE_OTHER"], fac, loc, oth, use_tex)

    # BLEND form: f x (other - local) + local.
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
    out.append("/* GENERATED by tools/win95/gen_combiner_table.py - do not edit. */")
    out.append("/* The definitions come from include/PR/gbi.h and include/f3ddkr.h of the")
    out.append("   neighbouring port; the list of configurations in use comes from")
    out.append("   docs/research/combiner-inventory.md, generated from the game's")
    out.append("   static tables. */")
    out.append("static const dkr_cc_entry CC_TABLE[] = {")
    seen = set()
    stats = {}
    for c1, c2, entries in CONFIGS:
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
        out.append(f'      "{note}", {entries} }},')
    out.append("};")
    sys.stderr.write("classification:\n")
    for k in sorted(stats):
        sys.stderr.write(f"  {k:22s} {stats[k]}\n")
    sys.stderr.write(f"  total                  {len(seen)}\n")
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    sys.stdout.write(emit())
