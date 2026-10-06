# Ordering-table DMA callback

POPS +0x9364 accepts control 0x11000002 and clamps the byte count to the
unsigned start address. A mismatching control or an empty clamped range returns
one without writing memory, as in the original. On the ordinary word-aligned
path it writes a 0x00FFFFFF terminator at the lowest entry, then ascending
memory writes whose values link to the preceding entry. It returns the actual
clamped byte count, not an invented completed-channel flag.

The implementation uses `rp_ordering_table_link_layout` and connects through
`rp_pops_dma_try_channel`. The existing scheduler owns MADR movement, cycle
debits and channel completion. For ranges above 8192 bytes the original uses
cache operations and switches to the uncached alias; the native model keeps
that alias selection but records cache maintenance as elided on coherent host
backing. This is why the ledger entry remains partial rather than claiming a
PSP cache implementation. Non-word-aligned direct test inputs stop explicitly.

Focused checks pass with AddressSanitizer and UBSan: control/empty no-ops,
address clamping, exact links, untouched neighboring words, a large alias-path
transfer, and a real channel-6 callback/completion with its cycle debit.
Existing CD DMA and Unicorn cache checks also pass.

`out/otc-dma.RrgP2k/result/` completes the real channel-6 transfer and reaches
CPU emitter category 8 (`RP_CAT_LOAD_COP_MEMORY`). The run records 39,470,213
generated-cache instruction observations and 40,357 transfers. There is no
rendered frame or demonstrated FFVI boot; `game_executed` is still false.
