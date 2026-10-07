# Scratchpad store helpers

The integrated path after the configured-PC hook writes to 0x1F8003D3.
The +0x1DD0 helper takes its +0x1C70 branch: region bits 23..28 must be 63,
then bits 10..22 must be zero. It patches its caller's JAL to +0x1DB4 and
writes the byte through the core's named 1 KiB PS1 scratchpad backing.

The halfword (+0x2110 -> +0x20F4) and word (+0x2450 -> +0x2434) cases have
matching control flow. The recovered C handles all three and preserves the
original patch words, return address, A0/V0/A2 effects and unchanged T9.
There is no read-helper cycle refund on these store paths. CACHE/SYNC remain
host-coherent operations managed by the existing generated-code adapter.

A warm specialized helper compares the original upper-page value, not the
cold helper's region predicate. When the same callsite later writes RAM,
the original fallback maps the low 21 address bits without repatching the
call or changing T9. This transition is reached in the integrated run.
The remaining device fallback stays explicit, not another scratchpad write.
The existing dispatcher still handles cold helpers' already recovered RAM
and device paths. Unaligned multi-byte stores remain outside this contract.

`make test-native-scratchpad` checks the three widths, cold aliases, caller
patching, unchanged neighboring bytes and cycle register, and warm use without
another patch. This is a C contract fixture under ASan/UBSan, not a complete
hardware-equivalence proof. The integration result is recorded in progress.md.
