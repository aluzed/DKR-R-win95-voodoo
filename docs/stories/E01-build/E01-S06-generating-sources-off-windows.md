# E01-S06 — Generating the sources off Windows

| | |
|---|---|
| **Epic** | E01 — 32-bit Windows 95 build chain |
| **Status** | TODO |
| **Priority** | P2 |
| **Estimate** | M |
| **Depends on** | — |
| **Blocks** | E09-S03 |

## Context

Today, preparing the generated sources is a Windows-only path. `Build-Linux.sh` says
so without ceremony:

> `Generated DKR functions are missing. Prepare them on Windows with
> Build-DKR-Runtime.cmd first.`

And `docs/BUILDING.md` requires Visual Studio 2022, PowerShell **and** WSL2 for that
step. In other words: building the reference decomp, applying the patches, running
N64Recomp and RSPRecomp all require a complete Windows machine — whereas this
project's target is built by cross-compilation from Linux (E01-S01).

This port's development machine runs Linux. Having to go through a Windows machine
at every change of recompilation policy turns a loop of a few minutes into one of
several tens.

## Objective

To make the complete generation — reference decomp, patches, N64Recomp, RSPRecomp —
runnable from Linux, without Visual Studio or PowerShell.

## Scope

**In:** porting the preparation scripts.

**Out:** any change to the generation's behaviour. The output must be identical, byte
for byte.

## Work

1. Read `scripts/Prepare-DKR-Runtime.ps1` (34 KB) and extract its real steps,
   separating what is intrinsically Windows from what is Windows out of convenience
   of writing.
2. Check what already exists on the Linux side: `scripts/bootstrap_dependencies.py`
   and `scripts/apply-dependency-patches.sh` presumably cover the fetching and the
   patches. Rewrite only what is missing.
3. Build the reference decomp's ELF under Linux. The upstream decomp
   (`extern/dkr-decomp`) builds natively under Linux with a MIPS toolchain — that is
   its normal mode of use, not a diversion.
4. Build N64Recomp and RSPRecomp under Linux and run them with the same
   configuration files: `runtime-recomp/dkr.us.v77.recomp-policy.json` and
   `runtime-recomp/rsp/aspMain.us.v77.toml`.
5. Write `Prepare-DKR-Runtime.sh` and `Generate-DKR-RSP.sh`, with the same ROM
   validation and the same explicit error messages as their Windows equivalents.
6. Prove the equivalence: compare the digests of the sources generated under Linux
   and under Windows. A difference is a defect to fix, not an acceptable variation —
   without which the two paths will diverge silently.
7. Update `docs/BUILDING.md` with the Linux path.

## Acceptance criteria

- [ ] `Prepare-DKR-Runtime.sh` produces `RecompiledFuncs` and `RecompiledPatches`
      from a Linux machine, without Windows.
- [ ] `Generate-DKR-RSP.sh` produces `RecompiledRSP/aspMain.cpp`.
- [ ] The Linux and Windows outputs are identical by digest, or any difference is
      explained and fixed.
- [ ] ROM validation and the error messages are preserved identically.
- [ ] `docs/BUILDING.md` documents the Linux path.
- [ ] The existing Windows scripts go on working.

## Risks

An undetected divergence between the two generation paths would produce two different
sets of sources depending on the machine, and hence bugs that reproduce only at one
person's. Step 6's digest comparison is not a criterion of comfort: it is the only
protection against that scenario.

## References

- `Build-Linux.sh:9-12` — the message that requires Windows
- `docs/BUILDING.md` — current prerequisites
- `scripts/Prepare-DKR-Runtime.ps1`, `scripts/bootstrap_dependencies.py`,
  `scripts/apply-dependency-patches.sh`
