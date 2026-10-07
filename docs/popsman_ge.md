# POPSMAN 7014C540: recovered synchronization provider

## Source and boundary

`src/popsman_ge.c` reconstructs ARK-reference POPSMAN +0x3A00..+0x3ACB.
It uses the same pinned provider as `popsman_contract.md`, not an implicitly
substituted stock-firmware or corpus profile. The local provider SHA-256 was
checked as `83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855`;
the reference listing hash is
`6f4ab6a1bbf2f22cbec82cffa37689a5e964631abb3e25e33d9f839cd462996d`.

The bus exposes numeric guest writes, MMIO reads, interrupt state, EPC/cache
operations and the two GE driver calls. The C never dereferences guest numbers
as host pointers. Device behavior belongs to those callbacks, not to the
reconstructed provider's control flow.

## Ordered behavior

After saving/disabling interrupts, the provider masks the continuation to
29 bits, writes FINISH immediately before its uncached alias, and publishes
the physical continuation as GE stall. It reads EPC and performs up to 20
cache-operation/status-read pairs, starting at EPC+64. Address addition wraps
in 32 bits. Only status mask 0x4 (bit 2) selects the completion branch.

When the bit is observed, the routine writes 4 to the acknowledgement register,
restores interrupts with synchronization and returns the original list id.
It does not enqueue a replacement list or write END in this branch.

Otherwise it writes END at the continuation, releases the stall, restores
interrupts, enqueues a new list already stalled at that continuation, then
synchronizes the old id. The enqueue return is preserved even when its high
bit is set; the list-sync return does not replace it. These are control-flow
observations, not recovered Sony function names or proof of device timing.

## Native host use

`src/native/pops_ge_host.c` supplies an explicitly headless capture adapter.
Its cache operations are elided on coherent backing, its interrupt state is
host-only, and its status has no completed GE execution to acknowledge.
Consequently it takes the original fallback rather than forcing the fast
completion bit. Existing GPU submission sites now call this C provider;
FINISH/END writes and continuation ids retain their previous capture behavior.

A readback invokes the same provider but requires pixel-producing execution.
At its old-list synchronization the adapter stops with
`GE_backend_execution_required`; it does not let the CPU read untouched
shadow EDRAM as a successful render. The preceding direct sceGeListSync /
sceGeListEnQueue restart route remains a separate dependency when selected.

The provider is reconstructed; the host bus is not a PSP interrupt/MMIO model
and is not a GE renderer. An actual backend must execute queued lists with
their live source data before acknowledging readback.

## Tests

`make test-popsman-ge` checks 20 ordered bus contracts: completion at polls
1, 7 and 20, no completion within 20 polls, unrelated status bits, address
masking, cache-address wrap, interrupt restoration, enqueue-before-sync order,
and both ordinary and high-bit-set return values. These are scripted contract
tests with the original instruction listing as reference, not instruction-level
differential or hardware tests. Native GPU and readback suites also pass.

The integrated trace in `out/ge-provider.6czKIc/result/` reaches this exact
fallback while reading the real PBP. It queues continuation 0x49A00160 as id
186, then refuses the pixel-dependent wait for old id 185 at +0x3A98. Its
result remains `game_executed: false`; no rasterization is claimed.
