#!/usr/bin/env python3
"""E01-S04 - refuses any binary that could not load under Windows 95.

    tools/win95/check_imports.py build/win95/bin/WITNESS.EXE
    tools/win95/check_imports.py --objects build/win95 build/win95/bin/WITNESS.EXE
    tools/win95/check_imports.py --self-test
    tools/win95/check_imports.py --refresh

Under Windows 95, the loader resolves **every** import at startup: a missing
symbol stops the process from starting, even if the function is never called.
The symptom is therefore binary and late - it is only discovered by running the
binary on the target machine.

But a PE's import table is static, and so is Windows 95's export list. The check
therefore belongs to the build, not to code review.

Three categories of DLL, and the distinction matters:

  system    Its export table is in `exports/`. Every symbol is checked.
  driver    Supplied by the hardware - `glide2x.dll` comes from the 3dfx card,
            not from the OS. Its symbols cannot be checked here; its presence is
            checked at launch. Reported, never silently ignored.
  unknown   Neither: an error. That is the case that stops a new dependency from
            slipping through unnoticed.
"""
import argparse
import fnmatch
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
STUBS_DIR = EXPORTS_DIR / "stubs"
EXCEPTIONS = EXPORTS_DIR / "exceptions.json"
PREFIX = pathlib.Path(os.environ.get("DKR_WIN95_PREFIX",
                                     pathlib.Path.home() / ".local/dkr-win95"))

sys.path.insert(0, str(HERE))
from pe_symbols import PE  # noqa: E402

# DLLs the hardware or the package supplies, with the reason. A DLL only enters
# here if its presence on the target machine is established or guaranteed by the
# package.
DRIVER_DLLS = {
    "GLIDE2X.DLL": "3dfx driver - the rendering API chosen by ADR 0002",
    "GLIDE3X.DLL": "3dfx driver - installed alongside glide2x by the same driver",
}

RED, GREEN, YELLOW, BLUE, OFF = (
    "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[1;34m", "\033[0m")


def say(msg):
    print(f"{BLUE}==>{OFF} {msg}")


def load_reference():
    """Loads the export baseline: {upper-case DLL -> set of symbols}."""
    if not EXPORTS_DIR.is_dir():
        sys.exit(f"export baseline missing: {EXPORTS_DIR}\n"
                 f"rebuild it with --refresh from the test machine")
    ref = {}
    for path in sorted(EXPORTS_DIR.glob("*.txt")):
        ref[path.stem.upper() + ".DLL"] = set(path.read_text().split())
    if not ref:
        sys.exit(f"no export list in {EXPORTS_DIR}")
    return ref


def load_stubs():
    """Loads the survey of exports that do nothing: {DLL -> set}.

    A symbol absent from the export table is a loud problem - Windows 95 refuses
    to load the program and names it. A symbol that is **exported and empty** is
    silent, and that is worse: the link passes, the load passes, this check used
    to pass, and the function does nothing.

    That is how `CreateSemaphoreW` nearly carried off `ultramodern`'s scheduler
    without a single guard rail flinching (E02-S01). The survey is produced by
    `find_stubs.py`, which recognises the pattern in the disassembly rather than
    guessing from the name.

    This function **fails** if the survey is missing or empty, instead of
    returning an empty dictionary. Returning {} would silently disable half the
    check, and the tool would print "loadable under Windows 95" in green: that
    would be exactly the silent failure it is meant to prevent, this time inside
    the guard rail itself.
    """
    if not STUBS_DIR.is_dir():
        sys.exit(f"stub survey missing: {STUBS_DIR}\n"
                 f"rebuild it with tools/win95/find_stubs.py --write, on the "
                 f"test machine's DLLs.")
    out = {}
    for path in sorted(STUBS_DIR.glob("*.txt")):
        out[path.stem.upper() + ".DLL"] = set(path.read_text().split())
    if not out:
        sys.exit(f"no stub list in {STUBS_DIR} - see "
                 f"tools/win95/find_stubs.py")
    return out


def load_exceptions():
    """Explicit exceptions. Each must carry a written justification: without
    that, the list becomes the place where the tool is silenced.

    Each must also carry `referenced_by`: the project objects allowed to
    reference the symbol, which is the part of the justification a machine can
    read. The prose says where an import comes from; only this list can be
    compared with the build. `MoveFileExW` stood for a merge on prose alone."""
    if not EXCEPTIONS.is_file():
        return {}
    data = json.loads(EXCEPTIONS.read_text())
    out = {}
    for entry in data.get("exceptions", []):
        sym = entry.get("symbol")
        why = (entry.get("justification") or "").strip()
        if not sym:
            sys.exit(f"{EXCEPTIONS}: an entry with no 'symbol'")
        if len(why) < 20:
            sys.exit(f"{EXCEPTIONS}: the exception '{sym}' has no written "
                     f"justification - it is refused.")
        if "referenced_by" not in entry:
            sys.exit(f"{EXCEPTIONS}: the exception '{sym}' has no "
                     f"'referenced_by' - it is refused. List the project "
                     f"objects that may reference the symbol, or [] if the "
                     f"import comes from the toolchain alone.")
        if not isinstance(entry["referenced_by"], list):
            sys.exit(f"{EXCEPTIONS}: 'referenced_by' of '{sym}' is not a list")
        # `binaries` restricts the scope to certain executables, by file name.
        # Without it the exception applies everywhere - which is rarely what is
        # wanted: an API tolerated in a witness that exercises it on purpose
        # must not be tolerated in the game.
        out[sym] = (why, [b.upper() for b in entry.get("binaries", [])],
                    list(entry["referenced_by"]))
    return out


def excused_here(exceptions, symbol, binary):
    """Returns the justification if the exception covers this binary, else
    None."""
    entry = exceptions.get(symbol)
    if entry is None:
        return None
    why, binaries, _ = entry
    if binaries and pathlib.Path(binary).name.upper() not in binaries:
        return None
    return why


def first_line(why, width=96):
    """The ALLOWED line gives the gist; exceptions.json holds the reasoning.

    These justifications run to a paragraph each - which is the point of them,
    and which would bury the rest of a thirty-eight binary sweep."""
    why = " ".join((why or "").split())
    return why if len(why) <= width else why[:width - 1].rstrip() + "..."


NM_LINE = re.compile(r"^(?P<loc>.+?):\s+U\s+(?P<sym>\S+)$")


def object_name(loc):
    """`nm -A` prints the archive *and* the member: keep the member.

    `libwin95librecomp.a:mods.cpp.obj` and the loose
    `CMakeFiles/.../mods.cpp.obj` are the same object seen twice. Reducing both
    to `mods.cpp.obj` is what makes them comparable with `referenced_by`."""
    head, sep, tail = loc.rpartition(":")
    if sep and tail.endswith((".o", ".obj")):
        loc = tail
    return pathlib.Path(loc).name


def attribute(symbols, objdirs):
    """Finds which object references each symbol, by archive member.

    The PE's import table does not keep that information: it is lost at link
    time. We rebuild it by re-reading the objects and archives, where the symbol
    appears undefined in the form `__imp__X@n` or `_X`. Without this, the report
    names the symbol but leaves the diagnosis to be done.

    It only sees the project's objects - the toolchain's archives are not in
    these directories. That is the useful half: an excused symbol referenced
    from *here* is the project's own call, whatever the justification says about
    libstdc++."""
    if not objdirs:
        return {}
    nm = shutil.which("i686-w64-mingw32-nm") or shutil.which("nm")
    if not nm:
        return {}
    wanted = {s: set() for s in symbols}
    pats = {s: re.compile(rf"^_?_?imp_?_?{re.escape(s)}(@\d+)?$|^_{re.escape(s)}$")
            for s in symbols}
    for d in objdirs:
        root = pathlib.Path(d)
        if not root.exists():
            continue
        files = [str(p) for p in root.rglob("*")
                 if p.suffix in (".o", ".obj", ".a")]
        # By the batch: this runs on every link, and a `nm` per object over a
        # tree of two hundred costs more in process starts than in reading.
        for i in range(0, len(files), 64):
            try:
                out = subprocess.run([nm, "-u", "-A"] + files[i:i + 64],
                                     capture_output=True, text=True,
                                     timeout=120).stdout
            except (subprocess.SubprocessError, OSError):
                continue
            for line in out.splitlines():
                m = NM_LINE.match(line)
                if not m:
                    continue
                for sym, rx in pats.items():
                    if rx.match(m.group("sym")):
                        wanted[sym].add(object_name(m.group("loc")))
    return {k: sorted(v) for k, v in wanted.items() if v}


def check_provenance(excused, exceptions, objdirs):
    """Compares each tolerance's declared provenance with the build.

    Returns (undeclared, vanished): objects that reference a tolerated symbol
    without being declared, and declarations that match nothing any more.

    This is the check that was missing on 14 September 2026. `MoveFileExW` was
    tolerated because libstdc++ asked for it; a merge put a direct call back
    into `save_manager.cpp`, and the tool went on printing ALLOWED because it
    only ever read the symbol. The prose could not go stale loudly. This can."""
    if not objdirs or not excused:
        return [], []
    symbols = sorted({s for _, s in excused})
    owners = attribute(symbols, objdirs)
    undeclared, vanished = [], []
    for sym in symbols:
        declared = exceptions[sym][2]
        seen = owners.get(sym, [])
        extra = [o for o in seen
                 if not any(fnmatch.fnmatch(o, d) for d in declared)]
        if extra:
            undeclared.append((sym, extra))
        gone = [d for d in declared
                if not any(fnmatch.fnmatch(o, d) for o in seen)]
        if gone:
            vanished.append((sym, gone))
    return undeclared, vanished


def check(binary, ref, stubs, exceptions, objdirs):
    """Returns True if the binary can load under Windows 95."""
    try:
        imports = PE(str(binary)).imports()
    except Exception as exc:                                # noqa: BLE001
        print(f"  {RED}unreadable{OFF}: {binary} ({exc})")
        return False

    by_dll = {}
    for dll, sym in imports:
        by_dll.setdefault(dll.upper(), []).append(sym)

    print(f"### {binary} - {len(imports)} symbols, {len(by_dll)} DLLs")

    missing, unknown_dlls, driver, excused, hollow = [], [], [], [], []
    for dll, syms in sorted(by_dll.items()):
        if dll in ref:
            for s in syms:
                if s in ref[dll]:
                    # Exported - but does it do anything?
                    if s in stubs.get(dll, ()):
                        if excused_here(exceptions, s, binary):
                            excused.append((dll, s))
                        else:
                            hollow.append((dll, s))
                    continue
                if excused_here(exceptions, s, binary):
                    excused.append((dll, s))
                else:
                    missing.append((dll, s))
        elif dll in DRIVER_DLLS:
            driver.append((dll, len(syms)))
        else:
            unknown_dlls.append((dll, len(syms)))

    for dll, n in driver:
        print(f"  {YELLOW}DRIVER{OFF}  {dll} ({n} symbols) - {DRIVER_DLLS[dll]}")
        print(f"          not checkable here; its presence is, at launch.")
    for dll, sym in excused:
        print(f"  {YELLOW}ALLOWED{OFF}  {dll}:{sym} - "
              f"{first_line(excused_here(exceptions, sym, binary))}")

    ok = True
    undeclared, vanished = check_provenance(excused, exceptions, objdirs)
    for sym, gone in vanished:
        print(f"  {YELLOW}STALE{OFF}  {sym} - declared as referenced by "
              f"{', '.join(gone)}, which no longer reference it.")
        print(f"          The tolerance may have outlived its reason: read it "
              f"again before keeping it.")
    if undeclared:
        ok = False
        for sym, extra in undeclared:
            print(f"  {RED}UNDECLARED{OFF}  {sym}  <- {', '.join(extra)}")
            declared = exceptions[sym][2]
            print(f"          the tolerance names "
                  f"{', '.join(declared) if declared else 'no project object'}"
                  f" - this reference is not covered by it.")
        print(f"          The symbol is tolerated for a reason that does not "
              f"cover the project's own call.")
        print(f"          Read the justification before adding the object to "
              f"'referenced_by': a tolerance")
        print(f"          whose reason has disappeared passes an import nobody "
              f"re-examines.")

    if unknown_dlls:
        ok = False
        for dll, n in unknown_dlls:
            print(f"  {RED}UNKNOWN DLL{OFF}  {dll} ({n} symbols)")
            print(f"          neither in the export baseline nor declared as "
                  f"supplied by a driver.")

    if hollow:
        ok = False
        owners = attribute([s for _, s in hollow], objdirs)
        for dll, sym in hollow:
            src = owners.get(sym)
            where = f"  <- {', '.join(src)}" if src else ""
            print(f"  {RED}STUB{OFF}  {dll}:{sym}{where}")
        print(f"          exported but empty: returns 0 and sets "
              f"ERROR_CALL_NOT_IMPLEMENTED.")
        print(f"          The program will load and the function will do "
              f"nothing - a silent failure.")
        print(f"          Use the ...A variant, or supply it from "
              f"platform/win95/compat.c.")

    if missing:
        ok = False
        owners = attribute([s for _, s in missing], objdirs)
        for dll, sym in missing:
            src = owners.get(sym)
            where = f"  <- {', '.join(src)}" if src else ""
            print(f"  {RED}MISSING{OFF}  {dll}:{sym}{where}")
        if not objdirs:
            print(f"          (rerun with --objects <build directory> to name "
                  f"the offending object)")

    if ok:
        print(f"  {GREEN}loadable under Windows 95{OFF}")
    return ok


def refresh():
    """Re-reads the DLLs from the test machine's disk image."""
    disk = pathlib.Path(os.environ.get(
        "DKR_WIN95_DISK", PREFIX / "vm/dkr-p2-voodoo2/win95.img"))
    if not disk.is_file():
        sys.exit(f"disk image not found: {disk}")
    mcopy = shutil.which("mcopy") or str(PREFIX / "bin/mcopy")
    if not pathlib.Path(mcopy).exists():
        sys.exit("mtools missing - see scripts/Setup-Win95-TestVM.sh")

    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    # The partition starts at sector 63: aim at the file system, not at the
    # start of the image.
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
                say(f"{name}.DLL absent from the image - skipped")
                continue
            syms = sorted(set(PE(str(dst)).exports()))
            (EXPORTS_DIR / f"{name}.txt").write_text("\n".join(syms) + "\n")
            say(f"{name}.DLL: {len(syms)} exports")
    say(f"baseline rebuilt in {EXPORTS_DIR}")
    say("remember to update PROVENANCE.md if the reference system has changed")
    # The stub survey is not rebuilt here: it needs the DLLs themselves, which
    # `refresh` extracts into a temporary directory. Say so explicitly, otherwise
    # the stub list ages without anyone noticing - and a stale stub list restores
    # exactly the silence it was meant to remove.
    say(f"{YELLOW}the stub survey is NOT regenerated by this command{OFF}: "
        f"run tools/win95/find_stubs.py --write on the same DLLs.")


def self_test():
    """Acceptance criterion: the tool must detect a forbidden import introduced
    on purpose. A broken tool and a satisfied tool keep quiet the same way."""
    cc = (shutil.which("i686-w64-mingw32-gcc-posix")
          or shutil.which("i686-w64-mingw32-gcc"))
    if not cc:
        sys.exit("mingw-w64 i686 missing: self-test impossible")
    ref, stubs, exc = load_reference(), load_stubs(), load_exceptions()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        # Clean witness: only calls APIs present under Windows 95.
        (tmp / "clean.c").write_text(
            "#include <windows.h>\n"
            "int main(void){ Sleep(1); return (int)GetTickCount(); }\n")
        # Dirty witness: GetTickCount64 is from Vista, and Windows 95 has not
        # got it.
        (tmp / "dirty.c").write_text(
            "#include <windows.h>\n"
            "int main(void){ return (int)GetTickCount64(); }\n")
        # Hollow witness: CreateSemaphoreW *is* exported by Windows 95, and
        # does nothing. That is the case the export check alone let through, and
        # which cost ultramodern's scheduler (E02-S01).
        (tmp / "hollow.c").write_text(
            "#include <windows.h>\n"
            "int main(void){ return CreateSemaphoreW(0,0,1,0) != 0; }\n")
        for name in ("clean", "dirty", "hollow"):
            subprocess.run([cc, "-O2", "-march=pentium2", "-mno-sse", "-static",
                            str(tmp / f"{name}.c"), "-o", str(tmp / f"{name}.exe")],
                           check=True, capture_output=True)

        say("clean witness: only Windows 95 APIs")
        if not check(tmp / "clean.exe", ref, stubs, exc, []):
            print(f"{RED}the clean witness is refused - the tool is too strict{OFF}")
            return 1

        say("dirty witness: GetTickCount64, absent from Windows 95")
        if check(tmp / "dirty.exe", ref, stubs, exc, []):
            print(f"{RED}the dirty witness is accepted - the tool detects nothing{OFF}")
            return 1

        say("hollow witness: CreateSemaphoreW, exported but empty")
        if check(tmp / "hollow.exe", ref, stubs, exc, []):
            print(f"{RED}the hollow witness is accepted - the stub check detects "
                  f"nothing{OFF}")
            return 1

        # Undeclared witness: a tolerated symbol, called from an object the
        # tolerance does not name. That is the MoveFileExW shape - the symbol
        # was excused for libstdc++'s sake and the project called it directly -
        # and the only case the three witnesses above cannot produce.
        sym = next((s for s, (_, b, r) in exc.items() if not b and not r), None)
        if sym is None:
            print(f"{YELLOW}no toolchain-only exception left to test the "
                  f"provenance check with - skipped{OFF}")
            return 0
        say(f"undeclared witness: {sym}, tolerated but called from here")
        (tmp / "undecl.c").write_text(
            "#include <windows.h>\n"
            "volatile void *sink;\n"
            f"int main(void){{ sink = (void *)&{sym}; return 0; }}\n")
        subprocess.run([cc, "-O2", "-march=pentium2", "-mno-sse", "-static",
                        "-c", str(tmp / "undecl.c"), "-o", str(tmp / "undecl.o")],
                       check=True, capture_output=True)
        subprocess.run([cc, "-O2", "-march=pentium2", "-mno-sse", "-static",
                        str(tmp / "undecl.o"), "-o", str(tmp / "undecl.exe")],
                       check=True, capture_output=True)
        if check(tmp / "undecl.exe", ref, stubs, exc, [str(tmp)]):
            print(f"{RED}the undeclared witness is accepted - the provenance "
                  f"check detects nothing{OFF}")
            return 1
        say(f"{GREEN}the tool works{OFF}")
    return 0


def main():
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binaries", nargs="*", help="PE32 binaries to check")
    ap.add_argument("--objects", action="append", default=[], metavar="DIR",
                    help="object directory, to name the offending object")
    ap.add_argument("--refresh", action="store_true",
                    help="rebuild the baseline from the test machine")
    ap.add_argument("--self-test", action="store_true",
                    help="check the tool by injecting a forbidden import")
    args = ap.parse_args()

    if args.refresh:
        refresh()
        return 0
    if args.self_test:
        return self_test()
    if not args.binaries:
        ap.print_help()
        return 2

    ref, stubs, exc = load_reference(), load_stubs(), load_exceptions()
    status = 0
    for b in args.binaries:
        p = pathlib.Path(b)
        if not p.is_file():
            print(f"  {RED}not found{OFF}: {b}")
            status = 1
            continue
        if not check(p, ref, stubs, exc, args.objects):
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
