# ROM setup

DKR-R supports Diddy Kong Racing US v1.0/v77 and US Rev A/v1.1/v80. Expected
SHA-1 values after byte-order normalisation are:

```text
0cb115d8716dbbc2922fda38e533b9fe63bb9670
6d96743d46f8c0cd0edb0ec5600b003c89b93755
```

The launcher accepts `.z64`, `.v64` and `.n64` dumps and identifies byte order
from the ROM header rather than the filename extension. It never modifies or
bundles the selected file. For a byte-swapped or little-endian dump, DKR-R
creates one hash-verified big-endian copy in its private `rom-cache` directory
and reuses that canonical copy on later launches. This gives every byte order
the same runtime input and avoids doing conversion work during presentation.

For normal play, select the ROM in the Play tab. The launcher identifies the
revision before starting the renderer or audio and selects the matching private
engine automatically. Players download and start one application and never
choose an engine manually. The selected engine validates the revision again
before registering generated code. For development preparation, keep each
revision's generated work in its own ignored build directory. Those files must
never be committed or distributed.

If validation fails, redump your own cartridge and verify the revision. ROM
patches, modified regional releases and bad dumps are intentionally rejected.

## Windows 95

The Windows 95 build has no launcher and runs **US v1.0 (v77) only**; the Rev A
ROM is recognised and refused with a message saying so. It looks for the ROM in
this order:

1. a path given on the command line (`DKRR.EXE C:\GAMES\DKR.Z64`);
2. `Rom=` under `[Paths]` in `DKRR.INI`, beside `DKRR.EXE`;
3. any `.z64`, `.n64` or `.v64` file in the folder of `DKRR.EXE`.

So the simplest installation is to copy your ROM into the game's folder and
start `DKRR.EXE`. The first ROM accepted is written to `DKRR.INI`, and its
identity is kept in `dkr-runtime-data\rom-identities-v1.txt`, so later starts
neither search nor hash it again (2.3 s saved on the test machine). A `.v64`
or `.n64` dump is normalised once into `dkr-runtime-data\rom-cache`.

When no ROM is accepted, a message box says where to copy it, how to name it in
`DKRR.INI`, and lists every file it looked at with the reason it was not used.
The same text is in `dkr-runtime-data\logs\runtime.log`.
