# Signed-half state-store policy

The policy-1 branch of +0x46A0 at +0x4930..+0x49FC is reconstructed rather
than stopping when a guest value is not already sign-extended from 16 bits.
This remains one partial original function; specialized GTE destinations
are separate unfinished paths of that same function.

For a known value already equal to its sign-extended low half, the original
ordinary word-store path is reused. Otherwise the compiler reserves a distinct
temporary. A GPR source emits SEH into that temporary; a saved-slot source
emits LH directly; an FPR source is reloaded and then extended. SW publishes
the full extended word, and the temporary is released. The guest's live source
and known-value metadata are not changed to the truncated value.

`pops_ir.h` names the original policy values. `pops_emit.h` describes selected
compiler wire fields (temporary ownership, mappings, known mask/values and
saved-slot base). The updated path and shared initialization use named fields;
the rest of the earlier emitter's anonymous offsets are not claimed migrated.

Focused checks cover all three storage cases plus known values requiring and
not requiring extension. They check exact emitted instructions, temporary
release, unchanged guest mapping/value and existing emitter regressions.
`make native test-native-emit test-unicorn-cache` passes. This is not exhaustive
binary equivalence or a claim that all coprocessor state operations are done.

`out/signed-state.axjdmp/result/` passes the policy and reaches the active
ME envelope reconfiguration branch at +0x16E4. It records 39,468,863 generated
instruction observations, 40,333 transfers and `game_executed: false`.
