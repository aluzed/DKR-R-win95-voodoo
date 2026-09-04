# The oracle stops approximating

Changed on 4 September 2026, after `win95-multipass-visible.md` showed why it had
to be.

## What was wrong with it

E04-S08's rasteriser evaluated `dkr_render_state.combine` — a **four-mode
shorthand**: shade, texel, texel×shade, texel×constant. That shorthand exists
because the Voodoo has one combiner stage and the translation has to end
somewhere. The oracle was under no such obligation and took it anyway.

The consequence, measured the same day: on the hub the oracle and the card agreed
to **165 divergent pixels out of 307,200** while both drew a whole character as a
**black silhouette**. An oracle that shares the backend's approximation cannot
detect the backend's approximation. The agreement was measuring the shorthand.

E05-S03 had already met this from the other side: *"The ticket assumed E04-S08's
rasteriser implemented the combiner faithfully; **that was not the case**, and
`dkr_combiner_eval` had to be written for this criterion to have any meaning."*
The evaluator was written and the rasteriser went on not using it.

## What it does now

`recipe` names the catalogued configuration and the catalogue holds the real mux
fields, so the oracle looks the entry up and calls `dkr_combiner_eval_all`:
`(a − b) × c + d`, one cycle or two, with the second fed the first's result as
`COMBINED`.

Recipe zero — an uncatalogued configuration, or a mode forced by a diagnostic
switch — keeps the four-mode path, which is then the best description available
of what was asked for.

**Both constant registers are now carried in the state.** `constant_color` holds
the one Glide can be given, chosen by whichever register the *colour* mux names;
that choice is a Glide limitation and not a fact about the RDP, which has two and
reads both in the same configuration — `G_CC_MODULATEIA_PRIM` in cycle 1 with
`G_CC_BLEND_ENV_ALPHA2` in cycle 2 does exactly that. `prim_color` and
`env_color` are carried unconditionally, both repacked to `0xAARRGGBB` at the
source. The block stays free of implicit padding and the assertion still holds.

**It is slower per pixel and that is the right trade here.** `software.h` says
the rasteriser is "allowed to be slow and not allowed to be complicated", and one
faithful path is simpler than four hand-written cases, not more complex.

## What changed in the image

| capture | pixels changed | what |
|---|---|---|
| `CAP0050` intro | — | the aeroplane beside the logo, a black silhouette, is coloured |
| `CAP0150` / `CAP0160` | — | the copyright text goes from a black smudge to white |
| `CAP0250` hub | 10587 of 307200 | **Diddy Kong**, a black silhouette, becomes a red cap and a blue kart |
| `CAP0400` race | 17 of 307200 | nothing of substance |

Every difference is in the same direction, and it is the direction E05-S03 named
when it recorded that an approximate entry *"took over surfaces the fallback had
been rendering. Wizpig came out a black silhouette."*

The race changes by 17 pixels, which is worth noting on its own: the frames this
port has looked at most were the ones the shorthand happened to fit.

## What it did not change

`test_software.c`, `test_combiner.c` and `test_rdp_state.c` pass unchanged, and
so does the synthetic-scene comparison on the machine — that scene's state does
not name a catalogue entry, so it takes the four-mode path, which is the control
that says the new path was added rather than substituted.

## What it makes visible

The card still draws Diddy black: it has one combiner stage and no second pass.
That divergence was invisible while both sides shared the approximation, and the
harness reports it now.

Measured on the machine the same day, on `CAP0250.BIN`:

| | before | after |
|---|---|---|
| oracle against card, off-edge | 165 of 307,200 | **11,396 of 307,200** |
| per million | 537 | **37,096** |
| painted surface, oracle | 303,784 | 306,746 |

The difference map is Diddy Kong, in red, where the card paints him black.
`SOFTRAS.EXE` and `COMPARE.EXE` both report 0 failures on the machine, the
synthetic scene's worst gap still 9 — the control that says the new path was
added and the old one left intact.

That is the point of the change and not a side effect. `E05-S03`'s multipass work
now has a reference to be measured against, which is what
`win95-multipass-visible.md` said was the smaller half of the job and the half
that makes the other half checkable.
