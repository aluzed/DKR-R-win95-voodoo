# The F3DDKR microcode, command by command

A map established by reading `runtime-recomp/src/game/f3ddkr_rt64.cpp`, the
decoder that runs today. It has a value of its own: it is a description of Rare's
microcode, independent of this port and of the rendering engine used.

Every command is **two 32-bit words**, `w0` then `w1`, `w0` carrying the opcode in
its high byte.

## The thirteen opcodes

| Opcode | Name | Role |
|---:|---|---|
| `0x01` | `Matrix` | loads a matrix from RDRAM, selects a slot |
| `0x02` | `TextureOffset` | offset applied to the texture coordinates |
| `0x03` | `MoveMem` | writes a block of the RSP's memory |
| `0x04` | `Vertex` | loads vertices into the 32-entry cache |
| `0x05` | `Triangle` | draws triangles indexed into that cache |
| `0x06` | `DisplayListBranch` | branch to, or call, a nested list |
| `0x07` | `CountedDisplayList` | a list whose command count is given |
| `0xB8` | `EndDisplayList` | return from a list, or end |
| `0xBC` | `MoveWord` | writes a state word |
| `0xBF` | `DMAOffsets` | **addressing bases for the matrices and the vertices** |
| `0xF3` | `LoadBlock` | loads a block of texels |
| `0xF6` | `FillRect` | solid rectangle |
| `0xFD` | `SetTextureImage` | address, format and size of the texture image |

A fourteenth handler, `PresentationGroup`, has no opcode: it is reached through
`MoveWord` with type `0xFE` and a magic word — see below.

## `DMAOffsets` (0xBF) — the central mechanism

```
w0 : matrix base   (masked by 0x00FFFFFF)
w1 : vertex base   (masked by 0x00FFFFFF)
```

This is **Rare's microcode's distinguishing feature**, and the point where an
error is unforgiving: matrices and vertices are not addressed absolutely but by
offsets relative to those two bases. A wrong base does not produce a crash but
entirely absurd geometry, which is far harder to diagnose.

## `Vertex` (0x04)

```
w0  bit 16      : add to the current batch rather than replace it
    bits 19..23 : number of vertices, minus one
    bits  9..13 : destination index in the cache
w1              : address, relative to the vertex base
```

Each vertex occupies **ten bytes**: `x`, `y`, `z` as signed 16-bit integers, then
`r`, `g`, `b`, `a` as bytes. **It carries no texture coordinates** — those arrive
per corner at triangle time, which decides the shape of the rendering interface
(E04-S01).

The cache holds **32 entries**.

## `Triangle` (0x05)

```
w0  bits 16..19 : texture state (enable)
    bits 20..23 : number of triangles, minus one
w1              : address of the triangle table
```

Each triangle occupies **sixteen bytes**:

| Offset | Contents |
|---:|---|
| 0 | flags — bit `0x40` disables culling |
| 1, 2, 3 | the three indices into the vertex cache |
| 4, 6 | `s`, `t` of the first corner, as signed 16-bit |
| 8, 10 | `s`, `t` of the second corner |
| 12, 14 | `s`, `t` of the third corner |

The culling direction depends on the **sign of the viewport's x scale**: positive,
back faces are eliminated; negative, front faces.

## `Matrix` (0x01)

```
w0  bits  0..15 : must be 64 — otherwise the command is ignored
    bits 16..19 : slot, or 0
    bits 22..23 : fallback slot when the previous is 0
w1              : address, relative to the matrix base
```

Three slots at most; the index is clamped to 2. The matrix is 64 bytes.

## `MoveWord` (0xBC)

```
w0  bits 0..7 : type
w1            : value
```

| Type | Effect |
|---:|---|
| `0x02` | billboard mode — bit 0 of `w1` |
| `0x0A` | matrix selection — bits 6..7 of `w1`, clamped to 2 |
| `0xFE` | presentation group, if `w1 & ~0xFF` is `0x444B5200` |

The presentation group is **an extension of the port**, not of the original
microcode: the magic word `'DKR\0'` distinguishes the commands added by the modern
engine from the game's own. Its modes are the shadow (2), the vehicle part (4),
the billboard (6) and the surface (7).

## Flow control

`DisplayListBranch` (0x06) branches or calls according to a flag; the target
address is masked by `0x00FFFFF8` — **aligned on eight bytes**, one command's
size. `EndDisplayList` (0xB8) pops. `CountedDisplayList` (0x07) carries its
command count in bits 16..23 of `w0`.

The return stack holds **32 entries**.

## Range validation, and why it survives the extraction

The current decoder **validates every range before using it**, and rejects invalid
data with a bounded error rather than letting the host's memory be addressed.

| Command | What is checked |
|---|---|
| `Matrix` | address ≤ 8 MiB − 64 |
| `Vertex` | count ≤ 32, destination + count ≤ 32, end ≤ 8 MiB |
| `Triangle` | count ≠ 0, end ≤ 8 MiB, **every index < 32** |
| `DisplayListBranch` | target ≤ 8 MiB − 8, stack depth < 32 |
| `CountedDisplayList` | count ≠ 0, address ≠ 0, end ≤ 8 MiB |
| `LoadBlock` | address within RDRAM |

RDRAM is **8 MiB** (`0x00800000`) and the addresses are masked by `0x00FFFFFF`.

This discipline protects against a modified ROM as much as against a bug in the
port. The rejection is **contained**: it interrupts the command, not the game —
and it is logged, with a counter bounding the volume, because a corrupted display
list would otherwise produce thousands of lines per frame.

A detail that matters for the extraction: `Triangle`'s validation **checks the
whole batch before drawing the first of them**. Validating as it goes would let
valid triangles be drawn before the batch is rejected, which makes the defect
depend on the content and therefore hard to reproduce.
