# Independent review and integration

Bounded Luna reviews covered the mounted-owner eligibility, expanded reserve
policy, complete-settlement exclusion, shared contiguous tail withdrawal,
ephemeral geometry hoist, and exact transient duplicate/active-pool set.
No unresolved implementation blocker was found in the final candidate.
The set contains reserve entries only; active pool entries are lookup inputs,
not insertions. Its occupancy bound is 128 in 256 slots, independent of pool
size. The handler's stack-switch entry requests 128 KiB; the new temporary
table uses 1 KiB. Earlier review wording using the 32 KiB device-node default
was corrected against the entry assembly.

The test author was separate from the reviewer. Integration found false
fixture assumptions about the number of free extents and ordinary provenance
of every recursive-prefix block. Exact all-extent comparison replaced the
fixed-count assumption. LLDB identified inactive pool-origin block 34 in
reserve prefix index 4, while the actual working FreeTree root was 4091.
Owned-reserve validation and stricter ordinary-output provenance were split;
all current/committed roots, backup locations, free absence, active-pool aliases
and duplicate checks remain. No established production oracle was removed.

The independent review required and subsequently verified separate real
current-versus-committed directory-root cases, a probe collision crossing slot
255 to 0, exact reserve-plus-active-pool ownership conservation after a partial
write, unchanged pending/SB root state, and write guarding on settlement-error
exits. Final source/test hashes are in `input-identities.log`. The main agent
ran the final 44-suite normal and ASan/UBSan gates: 433 passed, zero failed in
each, and recomputed the normal benchmark means and threshold counts directly
from all sixteen raw TSVs.

Functional proof did not establish a performance benefit across workloads.
The default policy was rejected on the mixed normal timing result, its full
patch retained, and production source restored exactly to `6aadfac`. Forward
patch application was checked against that restored base. The unchanged
normal/probe handlers were rebuilt to verify restoration identities. No CI,
external write, push, PR, merge or release occurred.
Copied `.log` files have only trailing horizontal whitespace removed for
repository hygiene. Raw TSVs, completion markers and machine-info files are
unchanged; the manifest covers the persisted copies. Generated assembly keeps
only the two relevant stock-predicate/validation excerpts, not complete
duplicate object-source listings.
