#!/usr/bin/env python3
"""Derive a 32-bit-guest-register variant of N64Recomp's recomp.h.

DKR uses a general register at its full 64-bit width in exactly one function
(dmacopy_doubleword); everywhere else the width is pure sign extension of a
32-bit value.  Narrowing `gpr` therefore removes work from every emitted
instruction, but it also invalidates two things in the upstream header:

  1. address formation, which subtracts a 64-bit constant that assumes the
     register carries a sign-extended KSEG0 address;
  2. SD, which shifts a value right by 32.

Both are rewritten here.  The address rewrite is exact rather than approximate:
the upstream form's low 32 bits are always `(uint32_t)addr - 0x80000000`, so a
32-bit target that computes exactly that is bit-identical, not merely
equivalent for the address ranges DKR happens to use.

Usage: make_narrow_header.py <upstream recomp.h> <output recomp.h>
"""

import re
import sys

PROLOGUE = """
/* --- 32-bit guest register variant (see docs/research/cpu-budget.md) --- */

/* Exact 32-bit equivalent of `addr - 0xFFFFFFFF80000000`: the upstream
   expression's low word is always `(uint32_t)addr - 0x80000000`, and on a
   32-bit host only the low word reaches the pointer. */
#define DKR_GUEST_OFF(addr) ((uint32_t)(addr) - 0x80000000u)

/* SD is reached both from a narrowed general register and from a float
   register's u64 field, which keeps its width.  Widen by the argument's own
   size so the first sign-extends and the second passes through untouched. */
#define DKR_WIDEN(v) \\
    (sizeof(v) > 4 ? (uint64_t)(v) : (uint64_t)(int64_t)(int32_t)(v))
"""

REPLACEMENTS = [
    ("""#define SIGNED(val) \\
    ((int64_t)(val))""",
     """/* A narrowed register holds a 32-bit value; widening it to int64_t without
   saying so first would zero-extend, and every signed comparison in the game
   would read a negative number as a large positive one. */
#define SIGNED(val) \\
    ((int64_t)(int32_t)(val))"""),

    ("typedef uint64_t gpr;",
     PROLOGUE + "\ntypedef uint32_t gpr;"),

    ("""#define SD(val, offset, reg) { \\
    *(uint32_t*)(rdram + ((((reg) + (offset) + 4)) - 0xFFFFFFFF80000000)) = (uint32_t)((gpr)(val) >> 0); \\
    *(uint32_t*)(rdram + ((((reg) + (offset) + 0)) - 0xFFFFFFFF80000000)) = (uint32_t)((gpr)(val) >> 32); \\
}""",
     """#define SD(val, offset, reg) { \\
    uint64_t dkr_sd_value = DKR_WIDEN(val); \\
    *(uint32_t*)(rdram + DKR_GUEST_OFF((reg) + (offset) + 4)) = (uint32_t)(dkr_sd_value >> 0); \\
    *(uint32_t*)(rdram + DKR_GUEST_OFF((reg) + (offset) + 0)) = (uint32_t)(dkr_sd_value >> 32); \\
}"""),
]

# Every remaining `(<expr>) - 0xFFFFFFFF80000000` inside a pointer cast.
ADDRESS_FORM = re.compile(r"\(\((.+?)\) - 0xFFFFFFFF80000000\)")


def main() -> int:
    source, destination = sys.argv[1], sys.argv[2]
    with open(source, "r", encoding="utf-8") as handle:
        text = handle.read()

    # The address rewrite runs before the prologue is inserted, so that the
    # prologue's own comment may quote the form it replaces.
    for needle, replacement in REPLACEMENTS[2:] + REPLACEMENTS[0:1]:
        if needle not in text:
            print("pattern not found in upstream header:\n" + needle[:80],
                  file=sys.stderr)
            return 1
        text = text.replace(needle, replacement, 1)

    text, count = ADDRESS_FORM.subn(r"DKR_GUEST_OFF(\1)", text)
    if count == 0:
        print("no address forms rewritten", file=sys.stderr)
        return 1

    if "0xFFFFFFFF80000000" in text:
        print("an address form survived the rewrite", file=sys.stderr)
        return 1

    needle, replacement = REPLACEMENTS[1]
    if needle not in text:
        print("the gpr typedef was not found", file=sys.stderr)
        return 1
    text = text.replace(needle, replacement, 1)

    with open(destination, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("rewrote %d address forms" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
