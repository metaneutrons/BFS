# Primary observations: B-tree local scratch lifetime

This is primary-authored interpretation of actual commands and worker review,
not a captured tool transcript. Requirements live in the versioned plan;
the combined candidate is rejected after normal elapsed comparison. The overall <=5x PFS3 target
is unachieved. Earlier rejected experiments remain rejected.

Root changes only private initializers at five mutation-journal sites and
two bounds initializations plus two scan resets. Journal counts and bounds
flags are initialized; unused private arrays are not. On-disk node_init,
padding canonicalization, exact CRC, COW, retirement, allocation policy,
rollback, fences, commit and flush behavior remain unchanged. The shared
core is identical for Linux and Amiga. Candidate B-tree SHA1069dea2…,
baseline5095d6dd…; retained assembler remains b1180cee….

A distinct Luna source reviewer inspects every journal array/count use and
bound key/flag use, finding no blocker. Entries/keys are written before
their count/flag admits a read; all five journal counts and all four bounds
resets precede use. Ancestor bounds are inherited exactly as before. This is
static dataflow review, not qualification from ASan alone.

Actual O2/m68020 emitted baseline/candidate assembly and compiler commands
are retained. Insert's 546-longword journal clear becomes two count stores;
search's 258-longword bounds clear becomes two flag stores. A distinct Luna
review confirms all five journal sites and all four source bounds resets.
The scan compiler duplicates mutually exclusive flag-write blocks; do not sum
them as dynamic executions. Byte/store savings
are not dynamic call counts or elapsed gains. Host structure sizes are2192/
1026 bytes; actual target clears cover2184/1032 bytes respectively.

A private work-removal probe consumes the actual core helpers through one
source inclusion, linked with other unchanged frozen core archive members.
Defined poisoned arrays remain byte-identical while both counts/flags clear.
Two private full-zero initializer controls separately return expected exit1.
They deliberately retain old valid semantics and fail only the physical
work-removal oracle, not functional correctness. No indeterminate byte-read
qualification or performance follows from those probes. Production is never
changed for the negative controls.

Two independent Amiga build directories produce normal a5cf2add… and probe
0b72401e… byte-identically. The unchanged schema11 guest5126e864… is copied,
not rebuilt; the normal guest1fccf207… remains unchanged. Ten fresh systems
are prepared under unique btree-scratch-lifetime labels. Preparation is not
timing, functional success or acceptance. Local quality gates pass78tests
plus ShellCheck, actionlint and clang static analysis.

Existing49-suite/472-case runs pass first. With the final new fixture, both
complete normal and actual ASan/UBSan pass50suites/475cases. Leak detection is
off. New source847d809f… has three registered public tests; root recompiles
and runs all three against frozen old library2322570d… successfully. A
separate Luna reviewer finds no false assertion or blocker. Full512-byte keys,
height>=3 search/scan branch transitions, CRC-valid inherited upper-bound
corruption rejected after two reads, and update allocation/third-write abort
root/mapping/ownership restoration are exercised. Lower-bound corruption,
delete/rekey/root-leaf replacement fault cases and partial writes are not
newly qualified by this focused file; broad existing suites remain mandatory.

Final78quality tests, ShellCheck, actionlint and clang analysis pass. Linux
passes20conformance tests and actual default/options7 FUSE pressure/interrupted
daemon runs. Focused Linux MemorySanitizer uses available clang22.1.8 and
instruments every core translation unit plus the public fixture, with origins2,
O1/PIE and halt-on-error. All three cases pass. This checks only executed paths,
not every possible indeterminate read, fully instrumented libc or physical m68k.

Ten fresh image inputs, both handler identities, guests, ROM and PFS3 hashes
pass preflight. No remote compiler or emulator competes; coffin is inactive,
not disabled. The first local source-hash command refers to nonexistent
src/amiga/memory_copy.s and prints a path error; its partial output is retained
as frozen-source-first.log. The corrected src/amiga/memcpy_68k.s command
completes and is the frozen-source.log identity record. This is a command-path
correction, not a source/build/test failure. No product bytes change.

No CI, push, PR, merge, release, external publication, new user chat, soak or
physical CPU/power/controller/media qualification is performed.

All eight normal and two separate probe runs complete and pass strict
verification. Distinct Luna raw extraction precedes primary actual normal JSON
and agrees in all means, paired deltas, ratios and counts. The qualification
report owns exact numbers: create/lookup improve all four pairs; write/read
means regress5.13%/8.06%, and seven of24BFS pairs are adverse. All create,
write and delete candidate ratios exceed5, twelve of24 overall. No sample is
removed. Host/VM scheduling and large PFS3 drift prevent causal attribution.
Static work removal does not override adverse normal elapsed results.

Both diagnostic orders have zero structural or other non-time differences.
A reviewer notes that input-verification PASS alone is not an equivalence
gate; the separate counter-equivalence.mjs actually asserts both empty arrays
and passes, and offline replay reuses that same gate.

Only the candidate production helpers/call changes are withdrawn via patch.
Source cmp/hash proves exact5095d6dd… restoration with no product diff; a clean
normal handler rebuild matches retained7ec43316… byte-for-byte. The first
direct Make output-file target is invalid (exit2); its failure log is retained,
then the correct phony amiga target succeeds. The unchanged qualified old
core's prior matrix plus new3-case baseline replay are not represented as a
new full restoration matrix. New public tests remain equivalent and retained.

Ten completed systems are recoverably archived at Cachy cache prefix
btree-scratch-lifetime-2026-10-01; all20new TSV hashes match before/after.
No emulator remains at archive time. The rejected source/assembly/patch and
all actual timing observations remain as textual evidence.

Three exact C snapshots and the full-context patch are losslessly wrapped
base64 text so original trailing whitespace remains reconstructable without
hook bypass. Decoded bytes/lengths/hashes are verified; final offline replay
reproduces strict normal/probe JSON and explicit counter equivalence. New
file-only asset scope passes. Actual global audit exits1 on68older unchanged
C1formatter records, not new evidence; no checker/old artifact is modified.
