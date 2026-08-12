#!/usr/bin/env python3
"""Selectionne les fonctions feuilles du code recompile pour le banc E00-S03.

Une fonction feuille n'appelle aucune autre fonction du jeu : son temps est donc
attribuable a elle seule, sans qu'il faille reconstruire une pile d'appels.
"""
import argparse, pathlib, re, sys

CALL = re.compile(r'^\s{4}[a-zA-Z_][a-zA-Z0-9_]*\(rdram, ctx\);', re.M)

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--funcs-dir", required=True, type=pathlib.Path)
    ap.add_argument("--count", type=int, default=48)
    ap.add_argument("--output", required=True, type=pathlib.Path)
    a = ap.parse_args()

    sizes: dict[str, int] = {}
    for path in sorted(a.funcs_dir.glob("funcs_*.c")):
        text = path.read_text(errors="ignore")
        for part in re.split(r'^RECOMP_FUNC void ', text, flags=re.M)[1:]:
            name = part.split('(')[0]
            body = part
            end = body.find('\nRECOMP_FUNC')
            if end > 0:
                body = body[:end]
            if 'LOOKUP_FUNC' in body or CALL.search(body):
                continue
            sizes[name] = len(body.splitlines())

    top = sorted(sizes.items(), key=lambda kv: -kv[1])[:a.count]
    a.output.write_text("\n".join(f"F({n})" for n, _ in top) + "\n")
    print(f"{len(sizes)} fonctions feuilles, {len(top)} retenues -> {a.output}", file=sys.stderr)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
