# F3DDKR graphics research spike

Diddy Kong Racing uses Rare's custom F3DDKR display-list microcode. This is the largest early
technical risk because a standard Fast3D decoder cannot be assumed to understand it.

## Confirmed structural items tracked in Milestone 0

- `G_MTX / gSPMatrixDKR`
- `G_VTX / gSPVertexDKR`
- `G_TRIN / gSPPolygon` at opcode `0x05`
- `G_DMADL / gDkrDmaDisplayList` at opcode `0x07`
- billboarding through MoveWord index `0x02`
- MVP matrix selection through MoveWord index `0x0A`
- three indexed matrix slots used by the game-side macros

## What the current test does

The registry checks for duplicate command opcodes and verifies the confirmed DKR-specific constants.
The renderer command creates an original SVG scene containing a polygon, checkerboard placeholder and
matrix axes:

```bash
DKR-R --renderer-test output.svg
```

This proves the test and reporting path, not the graphics implementation. Every command descriptor is
correctly marked `implemented: false`.

## Milestone 1/2 implementation sequence

1. Compare F3DDKR macros and generated display lists against the decomp.
2. Define a decoder interface isolated from game source.
3. Add binary fixtures built from synthetic command words.
4. Implement matrix selection and vertex loading.
5. Implement polygon batches and DMA display-list chaining.
6. Implement billboard state.
7. Validate state translation across Direct3D 11, OpenGL and Metal through Fast3D/libultraship.
8. Add captured game display lists only as user-generated local test artefacts, never repository data.
