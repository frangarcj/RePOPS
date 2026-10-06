# Reverse coverage snapshot

## GPU fill and ready-event update

The latest ledger adds the partial GPU-ready entry +0x125F0. Against the
same 536-entry historical internal inventory, 66 bodies are complete (12.3%)
and 54 partial, or 120 combined (22.4%). Full POPS ledger totals are 77 complete
and 74 partial. Fill remains a branch within +0x133D0, not a new function.

## Latest comparable count: timer/DMA readers and linked GPU DMA

`out/gpu-dma-linked.qooyux/result/coverage.json` compares the updated ledger
against the same 536 historical internal entries. There are 66 matched complete
bodies (12.3%) and 53 matched partial bodies (22.2% complete or partial).
The full POPS ledger has 77 complete and 73 partial entries; additional starts
outside the old inventory are still not mixed into that denominator.

The new timer/DMA reader starts do not appear as separate functions in that
older inventory. They therefore improve execution without increasing its
complete-body percentage. The new GPU DMA entry is partial, not a finished GPU
or rendered game. This is a body-status count, never a remaining-time estimate.

## Historical snapshot

Measured at source commit `10216324da532df2ca5b2672e6c7e5a635609b6b`.
Input ledger: `data/function_progress.csv`. Reference inventory:
`out/decompiled/pops_660/index.json` from the existing Allegrex/Ghidra analysis.
The detailed computed snapshot is `out/structure_coverage.qgui60xk/coverage.json`.
No firmware or raw decompiler output is newly tracked by this report.

## Comparable denominator

The inventory has 695 function entries. 159 entries are import stubs in
0x3CF9C..0x3D49B. Excluding them leaves 536 internal entries. Matching ledger
entries to this denominator by exact entry address gives:

| Measure | Entries | Fraction of 536 |
| --- | ---: | ---: |
| Complete reconstructed bodies | 60 | 11.2% |
| Partial reconstructed bodies | 46 | 8.6% |
| Complete or partial | 106 | 19.8% |
| Not yet complete | 476 | 88.8% |

Eight matched entries are adapters, not reconstructed original functions. Two
are explicitly pending. The rest are not represented as audited work in the
ledger. A status of complete means the body is reconstructed at the recorded
validation scope; it does not prove all dependencies, exceptional inputs,
timing or device behavior.

## Why the ledger totals differ

The POPS ledger itself has 66 complete and 60 partial entries. Six complete
entries and fourteen partial entries are absent as distinct function starts
from this older Ghidra inventory. Offset zero, the ME callback, is one such
partial entry. They are reported separately instead of dividing a larger
numerator by an incompatible denominator.

Complete starts outside the inventory: 0x27EA8, 0x98C4, 0x15FE4, 0x96AC,
0x9850, 0x85F4.

Partial starts outside the inventory: 0x2888, 0x1A90, 0x1DD0, 0x2128,
0x1AA90, 0x0, 0x1265C, 0x2918, 0x2450, 0x2468, 0x267C, 0x2110,
0x9C60, 0x8A54.

Some starts need boundary reconciliation against larger or split inventory
bodies. Simply adding all starts to the denominator would imply an audited
non-overlapping function census that has not been established.

## Interpretation

Use roughly **11% closed, 20% started including partials** for this historical
internal-function inventory. This is not 11% of remaining engineering time,
code bytes, game compatibility, structure recovery or emulator performance.
Large functions and small wrappers each count once. The ME increment extends
one existing partial callback, so it advances behavior without increasing the
number of complete functions.

The separate POPSMAN profiles must not be combined with the POPS denominator.
The 9,580,458 generated-instruction hook observations and 1,688 entry transfers
are runtime diagnostics and are not reverse-coverage percentages.

## Reproducibility

The JSON snapshot records both input hashes. At this snapshot:

- Ledger SHA-256: `211391c2330315588d9cd81bb3e7e8bdd43fd4541242f47958a4f82a8b063f2a`.
- Ghidra index SHA-256: `f54c528bffad3f518f9a07b6371839fa6cca609ed8d387b9ee3dcb36d3e3565b`.

Repeat by excluding those 159 import starts and intersecting each ledger status
with the remaining entry-address set. A future inventory refresh should report
its own denominator rather than silently reusing these percentages.
