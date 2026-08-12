#!/usr/bin/env python3
"""Table d'exports et d'imports d'un binaire PE32, sans dependance.

    pe_symbols.py --exports KERNEL32.DLL
    pe_symbols.py --imports DKR-R.EXE

Sert a deux choses dans ce projet :

  * constituer la reference des symboles reellement offerts par Windows 95, en
    lisant les DLL de la machine cible plutot qu'une documentation ;
  * verifier qu'un binaire produit pour Win95 ne reclame que ces symboles.

La verification porte sur la table d'imports et non sur le graphe d'appels :
Windows 95 resout *tous* les imports au chargement, donc un symbole absent est
fatal meme si la fonction n'est jamais appelee.
"""
import struct
import sys


class PE:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        if d[:2] != b"MZ":
            raise ValueError(f"{path} : pas un executable MZ")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"{path} : pas un binaire PE")
        nsec, = struct.unpack_from("<H", d, pe + 6)
        optsz, = struct.unpack_from("<H", d, pe + 20)
        opt = pe + 24
        magic, = struct.unpack_from("<H", d, opt)
        self.ddir = opt + (96 if magic == 0x10B else 112)
        self.sects = []
        for i in range(nsec):
            b = opt + optsz + i * 40
            vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, b + 8)
            self.sects.append((va, max(vsz, rsz), ptr))

    def off(self, rva):
        for va, sz, ptr in self.sects:
            if va <= rva < va + sz:
                return ptr + (rva - va)
        return None

    def cstr(self, p):
        return self.d[p:self.d.index(b"\0", p)].decode("latin-1")

    def exports(self):
        rva, _ = struct.unpack_from("<II", self.d, self.ddir)
        if not rva:
            return []
        e = self.off(rva)
        nnames, = struct.unpack_from("<I", self.d, e + 24)
        nt = self.off(struct.unpack_from("<I", self.d, e + 32)[0])
        out = []
        for i in range(nnames):
            r, = struct.unpack_from("<I", self.d, nt + 4 * i)
            out.append(self.cstr(self.off(r)))
        return out

    def imports(self):
        rva, _ = struct.unpack_from("<II", self.d, self.ddir + 8)
        if not rva:
            return []
        e = self.off(rva)
        out = []
        while True:
            oft, _, _, name_rva, first = struct.unpack_from("<IIIII", self.d, e)
            if name_rva == 0:
                break
            dll = self.cstr(self.off(name_rva)).upper()
            thunk = self.off(oft or first)
            while True:
                v, = struct.unpack_from("<I", self.d, thunk)
                if v == 0:
                    break
                # Bit 31 : import par ordinal, sans nom.
                out.append((dll, f"#{v & 0xFFFF}" if v & 0x80000000
                            else self.cstr(self.off(v) + 2)))
                thunk += 4
            e += 20
        return out


def main(argv):
    if len(argv) < 3 or argv[1] not in ("--exports", "--imports"):
        print(__doc__.strip())
        return 2
    for path in argv[2:]:
        pe = PE(path)
        if argv[1] == "--exports":
            for name in sorted(pe.exports()):
                print(name)
        else:
            for dll, sym in sorted(pe.imports()):
                print(f"{dll}\t{sym}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
