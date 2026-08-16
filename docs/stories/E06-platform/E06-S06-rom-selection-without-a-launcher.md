# E06-S06 — ROM selection without a launcher

| | |
|---|---|
| **Epic** | E06 — Windows 95 platform |
| **Status** | TODO |
| **Priority** | P2 |
| **Estimate** | S |
| **Depends on** | E02-S04, E06-S05 |
| **Blocks** | E09-S05 |

## Context

DKR-R asks for its ROM at first launch through an SDL launcher drivable with a gamepad.
That launcher disappears with SDL2 and ImGui, and the game obviously cannot start
without a ROM — the user supplies their own, no asset being redistributed
(`docs/ASSET_POLICY.md`).

A simple route to designate it is therefore needed, suited to the target's usages: on
Windows 95, a game is installed in its folder, and dropping a file next to the
executable is a natural gesture.

E02-S06 already put the strict minimum in place to start. This ticket makes the thing
usable by somebody other than the developer.

## Objective

To allow a user to designate their ROM without a graphical launcher, with a clear
message when it is absent or invalid.

## Scope

**In:** discovering the ROM, validating it, error messages.

**Out:** the loading itself (E02-S04).

## Work

1. Implement the search in order of priority: command-line argument, then the path from
   the configuration file (E06-S05), then an automatic search in the executable's
   folder.
2. Implement the automatic search: walk the folder looking for a `.z64`, `.n64` or
   `.v64` file whose digest matches. It is the path that makes installation obvious —
   drop your ROM next to the game and launch.
3. Record the validated path in the configuration, to avoid revalidating at every
   launch. The SHA-1 validation of 12 MB is not free on a Pentium II (E02-S04).
4. Write the error messages for each case: ROM absent, ROM unreadable, wrong version,
   incorrect digest. Each must say what to do, not only what is wrong.
5. Display those messages visibly on the target: a Win32 message box, since there is no
   console — plus the trace in the log.
6. Document the procedure in the distribution package's `README.TXT` (E09-S05), with
   the expected digest.
7. Check that the message stays correct on a ROM of the right version but in a different
   byte order — the runtime normalises before computing the digest, and that case must
   not be wrongly rejected.

## Acceptance criteria

- [ ] The three discovery routes work, in order of priority.
- [ ] A ROM dropped next to the executable is found automatically.
- [ ] The validated path is remembered and revalidation avoided.
- [ ] Every error case produces a message that says what to do.
- [ ] The messages are visible without a console, and traced in the log.
- [ ] The three byte orders are accepted.
- [ ] The procedure is documented in the distribution package.

> **Correction of 15 August 2026**: this criterion had been marked blocked by the absence of the ROM. The ROM was present — see `docs/research/win95-rom-available.md`. The blockage no longer exists; what remains to be done remains so for other reasons, or simply has not been done yet.

## Risks

This is the user's first contact with the port. An obscure error message at this stage,
on a machine with no console and no tools, ends in abandonment. The care taken over the
messages is not cosmetic.

## References

- `docs/ROM_SETUP.md`, `docs/ASSET_POLICY.md`
- `README.md` — SHA-1 digest expected after normalisation
- E02-S04 — loading and validation
