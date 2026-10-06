# Memory and coprocessor transfer emission

Categories 8 (`RP_CAT_LOAD_COP_MEMORY`) and 4 (`RP_CAT_STORE_COP_MEMORY`)
of +0x6914 are now connected through existing reconstructed emitters.

The load branch uses the original policy byte at module +0xD42FC indexed
by the record's COP selector. Policy 1 selects LH, policy 2 selects LHU,
and other policies select LW. It loads into encoded host V0 and applies
+0x46A0's state-write policy at selector * 4. In particular, policy 3 still
performs the memory read before ignoring the destination write. The store
branch loads the COP state into encoded host A1 using +0x4504, then emits
SW through +0x3A90.

`rp_cop_memory_record_layout` names the 16-byte analysis view's signed
base/displacement and COP selector. It is used to calculate addresses, not
cast over guest memory. It is not an always-live view of records after their
fields become code/patch pointers. Encoded host registers V0/A1 have distinct
names from normalized opcodes and pseudo-op selectors.

Focused emitter checks pass with sanitizers for all four destination policies,
the actual helper call words, a negative displacement, an ignored destination
that retains its read and the reverse store direction. The Unicorn cache
smoke also passes. No instruction-level equivalence claim is made.

`out/cop-memory.CXKuaJ/result/` reaches GTE command emission (category 18)
after emitting the COP memory operations. It stops while compiling that same
block, before the new block can execute. Counts therefore remain 39,470,213
generated-cache observations and 40,357 entry transfers; `game_executed` is
false. Special state helpers retain their existing boundaries. This expands
the original partial emitter, not a new original function or a GTE engine.
