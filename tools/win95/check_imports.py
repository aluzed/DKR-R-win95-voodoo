#!/usr/bin/env python3
"""E01-S04 — refuse tout binaire qui ne pourrait pas se charger sous Windows 95.

    tools/win95/check_imports.py build/win95/bin/WITNESS.EXE
    tools/win95/check_imports.py --objects build/win95 build/win95/bin/WITNESS.EXE
    tools/win95/check_imports.py --self-test
    tools/win95/check_imports.py --refresh

Sous Windows 95, le chargeur resout **tous** les imports au demarrage : un
symbole absent empeche le processus de demarrer, meme si la fonction n'est
jamais appelee. Le symptome est donc binaire et tardif — on ne le decouvre qu'en
lancant le binaire sur la machine cible.

Or la table d'imports d'un PE est statique, et la liste des exports de
Windows 95 aussi. La verification appartient donc au build, pas a la relecture.

Trois categories de DLL, et la distinction compte :

  systeme   Sa table d'exports est dans `exports/`. Chaque symbole est verifie.
  pilote    Fournie par le materiel — `glide2x.dll` vient de la carte 3dfx, pas
            de l'OS. Ses symboles ne sont pas verifiables ici ; sa presence l'est
            au lancement. Signalee, jamais ignoree en silence.
  inconnue  Ni l'un ni l'autre : erreur. C'est le cas qui empeche une nouvelle
            dependance de passer inapercue.
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
EXPORTS_DIR = HERE / "exports"
EXCEPTIONS = EXPORTS_DIR / "exceptions.json"
PREFIX = pathlib.Path(os.environ.get("DKR_WIN95_PREFIX",
                                     pathlib.Path.home() / ".local/dkr-win95"))

sys.path.insert(0, str(HERE))
from pe_symbols import PE  # noqa: E402

# DLL que le materiel ou le paquet fournit, avec la raison. Une DLL n'entre ici
# que si sa presence sur la machine cible est etablie ou assuree par le paquet.
DRIVER_DLLS = {
    "GLIDE2X.DLL": "pilote 3dfx — API de rendu retenue par l'ADR 0002",
    "GLIDE3X.DLL": "pilote 3dfx — installee a cote de glide2x par le meme pilote",
}

RED, GREEN, YELLOW, BLUE, OFF = (
    "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[1;34m", "\033[0m")


def say(msg):
    print(f"{BLUE}==>{OFF} {msg}")


def load_reference():
    """Charge la base d'exports : {DLL majuscule -> ensemble de symboles}."""
    if not EXPORTS_DIR.is_dir():
        sys.exit(f"base d'exports absente : {EXPORTS_DIR}\n"
                 f"la reconstituer avec --refresh depuis la machine de test")
    ref = {}
    for path in sorted(EXPORTS_DIR.glob("*.txt")):
        ref[path.stem.upper() + ".DLL"] = set(path.read_text().split())
    if not ref:
        sys.exit(f"aucune liste d'exports dans {EXPORTS_DIR}")
    return ref


def load_exceptions():
    """Exceptions explicites. Chacune doit porter une justification ecrite :
    sans cela, la liste devient l'endroit ou l'on fait taire l'outil."""
    if not EXCEPTIONS.is_file():
        return {}
    data = json.loads(EXCEPTIONS.read_text())
    out = {}
    for entry in data.get("exceptions", []):
        sym = entry.get("symbol")
        why = (entry.get("justification") or "").strip()
        if not sym:
            sys.exit(f"{EXCEPTIONS} : une entree sans 'symbol'")
        if len(why) < 20:
            sys.exit(f"{EXCEPTIONS} : l'exception '{sym}' n'a pas de "
                     f"justification ecrite — elle est refusee.")
        out[sym] = why
    return out


def attribute(symbols, objdirs):
    """Retrouve quel objet importe chaque symbole fautif.

    La table d'imports du PE ne conserve pas cette information : elle est perdue
    au lien. On la reconstruit en relisant les objets et archives, ou le symbole
    apparait comme indefini sous la forme `__imp__X@n` ou `_X`.
    Sans cela, le rapport nomme le symbole mais laisse le diagnostic a faire."""
    if not objdirs:
        return {}
    nm = shutil.which("i686-w64-mingw32-nm") or shutil.which("nm")
    if not nm:
        return {}
    wanted = {s: set() for s in symbols}
    pats = {s: re.compile(rf"\b_?_?imp_?_?{re.escape(s)}\b|\b_{re.escape(s)}\b")
            for s in symbols}
    for d in objdirs:
        root = pathlib.Path(d)
        if not root.exists():
            continue
        files = [p for p in root.rglob("*") if p.suffix in (".o", ".obj", ".a")]
        for f in files:
            try:
                out = subprocess.run([nm, "-u", str(f)], capture_output=True,
                                     text=True, timeout=30).stdout
            except (subprocess.SubprocessError, OSError):
                continue
            for sym, rx in pats.items():
                if rx.search(out):
                    wanted[sym].add(f.name)
    return {k: sorted(v) for k, v in wanted.items() if v}


def check(binary, ref, exceptions, objdirs):
    """Renvoie True si le binaire peut se charger sous Windows 95."""
    try:
        imports = PE(str(binary)).imports()
    except Exception as exc:                                # noqa: BLE001
        print(f"  {RED}illisible{OFF} : {binary} ({exc})")
        return False

    by_dll = {}
    for dll, sym in imports:
        by_dll.setdefault(dll.upper(), []).append(sym)

    print(f"### {binary} — {len(imports)} symboles, {len(by_dll)} DLL")

    missing, unknown_dlls, driver, excused = [], [], [], []
    for dll, syms in sorted(by_dll.items()):
        if dll in ref:
            for s in syms:
                if s in ref[dll]:
                    continue
                if s in exceptions:
                    excused.append((dll, s))
                else:
                    missing.append((dll, s))
        elif dll in DRIVER_DLLS:
            driver.append((dll, len(syms)))
        else:
            unknown_dlls.append((dll, len(syms)))

    for dll, n in driver:
        print(f"  {YELLOW}PILOTE{OFF}  {dll} ({n} symboles) — {DRIVER_DLLS[dll]}")
        print(f"          non verifiable ici ; sa presence l'est au lancement.")
    for dll, sym in excused:
        print(f"  {YELLOW}TOLERE{OFF}  {dll}:{sym} — {exceptions[sym]}")

    ok = True
    if unknown_dlls:
        ok = False
        for dll, n in unknown_dlls:
            print(f"  {RED}DLL INCONNUE{OFF}  {dll} ({n} symboles)")
            print(f"          ni dans la base d'exports, ni declaree comme "
                  f"fournie par un pilote.")

    if missing:
        ok = False
        owners = attribute([s for _, s in missing], objdirs)
        for dll, sym in missing:
            src = owners.get(sym)
            where = f"  <- {', '.join(src)}" if src else ""
            print(f"  {RED}ABSENT{OFF}  {dll}:{sym}{where}")
        if not objdirs:
            print(f"          (relancer avec --objects <repertoire de build> "
                  f"pour nommer l'objet fautif)")

    if ok:
        print(f"  {GREEN}chargeable sous Windows 95{OFF}")
    return ok


def refresh():
    """Relit les DLL depuis l'image disque de la machine de test."""
    disk = pathlib.Path(os.environ.get(
        "DKR_WIN95_DISK", PREFIX / "vm/dkr-p2-voodoo2/win95.img"))
    if not disk.is_file():
        sys.exit(f"image disque introuvable : {disk}")
    mcopy = shutil.which("mcopy") or str(PREFIX / "bin/mcopy")
    if not pathlib.Path(mcopy).exists():
        sys.exit("mtools absent — voir scripts/Setup-Win95-TestVM.sh")

    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    # La partition commence au secteur 63 : viser le systeme de fichiers, pas
    # le debut de l'image.
    part = f"{disk}@@{63 * 512}"
    EXPORTS_DIR.mkdir(parents=True, exist_ok=True)
    names = [p.stem.upper() for p in EXPORTS_DIR.glob("*.txt")] or [
        "KERNEL32", "USER32", "GDI32", "ADVAPI32", "WINMM", "MSVCRT",
        "DDRAW", "DSOUND", "SHELL32", "COMDLG32", "COMCTL32", "OLE32",
        "VERSION", "WSOCK32"]
    with tempfile.TemporaryDirectory() as tmp:
        for name in sorted(names):
            dst = pathlib.Path(tmp) / f"{name}.DLL"
            r = subprocess.run([mcopy, "-n", "-i", part,
                                f"::/WINDOWS/SYSTEM/{name}.DLL", str(dst)],
                               env=env, capture_output=True)
            if r.returncode != 0 or not dst.exists():
                say(f"{name}.DLL absente de l'image — ignoree")
                continue
            syms = sorted(set(PE(str(dst)).exports()))
            (EXPORTS_DIR / f"{name}.txt").write_text("\n".join(syms) + "\n")
            say(f"{name}.DLL : {len(syms)} exports")
    say(f"base reconstituee dans {EXPORTS_DIR}")
    say("penser a mettre PROVENANCE.md a jour si le systeme de reference a change")


def self_test():
    """Critere d'acceptation : l'outil doit detecter un import interdit
    introduit volontairement. Un outil casse et un outil satisfait se taisent de
    la meme maniere."""
    cc = (shutil.which("i686-w64-mingw32-gcc-posix")
          or shutil.which("i686-w64-mingw32-gcc"))
    if not cc:
        sys.exit("mingw-w64 i686 absent : auto-test impossible")
    ref, exc = load_reference(), load_exceptions()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        # Temoin propre : n'appelle que des API presentes sous Windows 95.
        (tmp / "clean.c").write_text(
            "#include <windows.h>\n"
            "int main(void){ Sleep(1); return (int)GetTickCount(); }\n")
        # Temoin sale : GetTickCount64 est de Vista, et Windows 95 ne l'a pas.
        (tmp / "dirty.c").write_text(
            "#include <windows.h>\n"
            "int main(void){ return (int)GetTickCount64(); }\n")
        for name in ("clean", "dirty"):
            subprocess.run([cc, "-O2", "-march=pentium2", "-mno-sse", "-static",
                            str(tmp / f"{name}.c"), "-o", str(tmp / f"{name}.exe")],
                           check=True, capture_output=True)

        say("temoin propre : uniquement des API de Windows 95")
        if not check(tmp / "clean.exe", ref, exc, []):
            print(f"{RED}le temoin propre est refuse — l'outil est trop strict{OFF}")
            return 1

        say("temoin sale : GetTickCount64, absente de Windows 95")
        if check(tmp / "dirty.exe", ref, exc, []):
            print(f"{RED}le temoin sale est accepte — l'outil ne detecte rien{OFF}")
            return 1
        say(f"{GREEN}l'outil fonctionne{OFF}")
    return 0


def main():
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binaries", nargs="*", help="binaires PE32 a verifier")
    ap.add_argument("--objects", action="append", default=[], metavar="REP",
                    help="repertoire d'objets, pour nommer l'objet fautif")
    ap.add_argument("--refresh", action="store_true",
                    help="reconstituer la base depuis la machine de test")
    ap.add_argument("--self-test", action="store_true",
                    help="verifier l'outil par injection d'un import interdit")
    args = ap.parse_args()

    if args.refresh:
        refresh()
        return 0
    if args.self_test:
        return self_test()
    if not args.binaries:
        ap.print_help()
        return 2

    ref, exc = load_reference(), load_exceptions()
    status = 0
    for b in args.binaries:
        p = pathlib.Path(b)
        if not p.is_file():
            print(f"  {RED}introuvable{OFF} : {b}")
            status = 1
            continue
        if not check(p, ref, exc, args.objects):
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
