# ROM setup

DKR-R supports Diddy Kong Racing US 1.0/v77 only. The expected SHA-1 after
byte-order normalisation is:

```text
0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

The launcher accepts `.z64`, `.v64` and `.n64` dumps, identifies byte order from
the header and validates a normalized in-memory copy. It does not modify or
bundle the selected file.

For normal play, select the ROM in the Play tab. For development preparation,
place or select the ROM when `Build-DKR-Runtime.cmd` prompts. The build creates
ignored local decomp/recomp working data; those files must never be committed or
distributed.

If validation fails, redump your own cartridge and verify the revision. ROM
patches, modified regional releases and bad dumps are intentionally rejected.
