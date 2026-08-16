# What happens to a player without a 3dfx card

Measured on 14 August 2026 by removing the Voodoo 2 from the emulator's
configuration (`voodoo = 0`), then putting it back — the configuration restored
byte for byte, verified by digest.

## Glide displays its own box, and nothing allows one to get ahead of it

`dkr_glide_detect` loads `glide2x.dll` through `LoadLibrary` precisely so that the
absence of a card becomes a sentence rather than a refusal to load by the system.
That does protect against the absence of the *library*. Against the absence of the
*card*, it does not:

    _GlideInitEnvironment: glide2x.dll expected Voodoo, none detected

That text is not ours. It comes from the DLL's initialisation, which runs during
the `LoadLibrary` itself, and it appears in a **modal box** the program does not
see coming. The player therefore receives a message signed by a library they have
never heard of, before reaching our error path.

The box also has a consequence one does not expect: it steals the focus, which
blocked the shutdown of the test machine and left the transfer volume marked
dirty. An unforeseen modal box inconveniences more than the person in front of the
screen.

## The registry does not allow one to know

The natural idea is to check the card's presence *before* loading the DLL.
`glide_registry_probe.c` recorded the registry in both configurations, with and
without a card.

The result is clear: **the two reports are identical**.

    Enum\PCI
      VEN_121A&DEV_0001   instance BUS_00&DEV_0C&FUNC_00
                          ConfigFlags=0x00000000  "Voodoo2 3D Accelerator"
      VEN_121A&DEV_0002   instance BUS_00&DEV_0C&FUNC_00
                          ConfigFlags=0x00000000  "Voodoo2 3D Accelerator"
    Software\3Dfx Interactive\Voodoo2        present
    C:\WINDOWS\SYSTEM\glide2x.dll            present

Windows 95 keeps in `Enum` the devices it has known, and it does not mark these as
removed: `ConfigFlags` is zero in both cases. The driver's software key obviously
survives the card's removal, since it belongs to the driver.

One detail finishes off the criterion: `VEN_121A&DEV_0001` appears in both reports
although **that card never existed on this machine**. It is a leftover of the
Voodoo 1 configuration corrected by E00-S05, and the registry presents it exactly
as it presents the card really present. Trusting `Enum` would therefore have
produced a false positive on the very machine used to write the test.

Reading the PCI configuration space directly would require a VxD, which is out of
proportion with the benefit.

## What is done instead

The box cannot be avoided; it can be **attached to**. The text of
`DKR_GLIDE_ERR_NO_BOARD` now mentions explicitly the message that precedes it, so
that the player reads one problem instead of two:

    no 3dfx card detected - which is what the glide2x.dll message said as well

Verified on the machine without a card: after the box, the program regains
control, `grSstQueryHardware` fails, and `DKR_GLIDE_ERR_NO_BOARD` travels up to
the witness. The complete sequence is therefore: an incomprehensible box from a
third party, then our own explanation that defuses it. That is not ideal, and it
is the most that can be reached without rewriting the driver.

## What remains unexercised

The resolution fallback. The card's memory has always sufficed, and 86Box offers
no Voodoo configuration poor enough to force 640×480 to fail. The budget
computation is written and reviewed; it is not put to the test.
