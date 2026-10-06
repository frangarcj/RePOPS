# POPS, POPSMAN and platform boundaries

Do not collapse the binary module boundary, imported-library names and the
graphics execution boundary into a single layering rule.

## Measured module relationship

The pinned POPS imports match 30 provider entries in the measured POPSMAN
reference: 29 in library sceMeAudio and one in scePopsMan. This is a join of
library plus NID, not a claim that sceMeAudio is a separate sound module.
The older provider export audit uses the ARK-derived image whose SHA starts
83ed5373; current integration also has the corpus reference starting ff4222e8.
Do not erase those provenance distinctions. See interface.md and
popsman_contract.md for the concrete export tables, offsets and limitations.

POPSMAN selects/loads POPS and provides runtime services. POPS also directly
imports ordinary PSP services; not every kernel or GE operation passes through
POPSMAN. Starting a module with zero arguments is not a portable emulator API.

## Graphics: preserve the GE-list boundary

POPS owns GP0/GP1 parsing, emulated GPU state and translation into GE words.
For example, POPS +0x127D8 collects port writes and +0x133D0 consumes packets,
updating its list cursor and emitting GE commands.

The resulting platform boundary consists of the list memory and its submission,
stall, synchronization and related services. Some are direct sceGe imports.
Others are provided by POPSMAN under sceMeAudio names. In the audited provider:

| Import NID | Provider offset | Observed body |
| --- | --- | --- |
| 2AC64C3F | +0x23AC | GE EDRAM size services |
| 7014C540 | +0x3A00 | GE MMIO/cache/interrupt path and enqueue/sync fallback |
| E7F06E2B | +0x3ACC | Mask argument and write GE MMIO register 0xBD40010C |

These are not audio-mixing calls. Native rendering must consume the recovered
GE command boundary, not bypass it by drawing from PS1 commands directly.

The current headless adapter records selected GE operations. It does not
execute a full GE renderer, prove queue completion or reconstruct all provider
bodies. Emitting and retaining correct list words is a different milestone.

## Audio: a callback crosses back into POPS

POPS +0x1A038 supplies its module-relative-zero callback and the ME stack to
the registration service. POPSMAN +0x3490 stores the callback and starts the
ME through +0x35D8. The POPSMAN worker at +0x2F88 invokes that POPS callback
and consumes its result as the output sample. See media_engine_reverse.md.

Thus the mixer code belongs to POPS even when the ME processor executes it;
the surrounding worker belongs to POPSMAN. Callback registration, shared
state and control/acknowledgement memory are part of the contract in addition
to imported functions. Provider offsets never index the POPS image just
because both analysis databases have image base zero.

## Disc and current native representation

POPS contains the emulated CD controller, response timing, FIFO and DMA paths.
It requests data through platform/file services; the audited POPSMAN includes
seek/read and close wrappers. For the admitted plain local PBP, the native
filesystem and DEFLATE adapters are identified as adapters, not reconstructed
Sony implementations.

RePops currently combines reconstructed POPS functions, bounded POPSMAN
reconstructions and explicit host adapters; it is not two completed drop-in C
modules. New code should retain the original service identity/provider at
each crossing so those adapters can be replaced independently. The native
ownership of source functions, the processor running them and the provider of
a named import are three separate facts.
