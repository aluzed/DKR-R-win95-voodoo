# Development workflow

## Branch and source policy

Keep original decomp changes separate from host-platform code wherever possible. The intended layout
for later milestones is:

```text
extern/dkr-decomp/       pinned upstream reference
src/ and include/        clean native foundation
port/                    future hardware/platform replacements
extraction/              future Torch schemas and resource factories
```

Do not copy large decomp directories into the port layer without preserving provenance and licence
information.

## Quality gates

Before committing:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug
./build/linux-gcc-debug/bin/Debug/DKRPort --headless-self-test --portable
python scripts/scan_for_game_assets.py
```

Platform pull requests should also exercise their native CMake preset.

## Coding rules

- C++20 for the host scaffold.
- Fixed-width integers for binary formats.
- Explicit byte-order conversion.
- No host pointer stored in a 32-bit integer.
- No unchecked ROM offset arithmetic.
- No gameplay enhancement before baseline parity.
- Tests for every new decoder, resource type and save format.
- Warnings should remain clean; `DKRPORT_ENABLE_WARNINGS_AS_ERRORS` is available for CI tightening.
