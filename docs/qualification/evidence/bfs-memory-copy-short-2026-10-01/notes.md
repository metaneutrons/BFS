# Primary observations: short memcpy pilot

This is root-authored interpretation of command results and independent
worker reviews, not a captured tool transcript. The production assembly stays
at b1180cee… and normal handler at qualified7ec43316… throughout M1.

- Root authors private candidate f9ecf90f… only. It inserts56bytes of unsigned
  dispatch/scalar short path before the retained save frame. Independent Luna
  source/disassembly review finds no blocker for0/1/3/4/43/44, exact scalar
  count, caller-saved D0/D1/A0/A1, untouchedSP and original destination return.
  Zero length has no data dereference; this is not C NULL/overlap qualification.
- Root actual assembler output is decoded independently from emitted opcode
  words. Candidate84-byte large memcpy body equals old body byte-for-byte;
  all124memset opcode bytes equal old bytes. SHA outputs/scripts are retained.
  Dispatch adds runtime overhead even above44; byte equality does not prove
  equal latency. Static review does not qualify runtime behavior.
- A separate Luna author extends the shared harness with compile-time opt-in
  BFS_MEMORY_COPY_SHORT_TIMING=1. Default preserves8timinglengths; opt-in uses
  the14explicitlengths,84samples,42perorder and98batchesperkernel. All8908
  functional/ABI calls per kernel and independent guards/source/return/negative
  controls remain. Final C source c55343fa… is frozen for guest build/run.
  An interim runtime-GetVar approach and incorrect counter field names are
  caught during primary review before any build/freeze and corrected. They
  are not runtime qualification or final source.
- Two unique clean private builds run.l3US52 and run.dnMTQJ produce identical
  guest193d0a15… (15244bytes), with strict O2/m68020/-fno-builtin flags and the
  opt-in define. No warnings are suppressed beyond existing NDK pointer-sign
  boundary. Source/hash/configuration/private binaries are separated from
  eventual text-only evidence.
- First parser9d3f1e9b… accepts one positive synthetic report and rejects23
  malformed fixtures, but an independent primary duplicate-malformed counter
  probe demonstrates RED: a valid clock_hz plus clock_hz=-1 is ignored/accepted.
  First parser/probes and exact RED log remain. Author corrects only parser and
  fixtures; frozen C/ASM/binary unchanged. Final parser474c0e1d… registers names
  before conversion and rejects duplicate/malformed/unknown fields, while
  allowing named descriptive fields. Final fixture source3e25f8df… has29
  synthetic cases. These are parser behavior, not Amiga/kernel timings.
- One bounded300s guest starts with the unchanged licensed ROM, FS-UAE3.2.35,
  A1200/68040max,8MiBfast/2MiBchip, Stack32768 and no competing remotecompiler/
  emulator. File markers do not independently witness command exit or marker
  publication faults. No physical CPU or full UINT32 buffers are qualified.

Final independent Luna harness/parser review finds no blocker. The conservative
report bound is about7230bytes of8192; main report/arrays about9.8KiB before
small locals, within32KiBstack. This is static bounding, not publication-fault
qualification. Root reruns29syntheticfixturesPASS and independently rebinds
the exact first-RED fixture to the corrected parser:25/25PASS. Both versions,
scripts, RED and GREEN logs remain. Review does not claim those fixtures were
executed by the read-only reviewer.

Actual first guest finishes inside300s with exit0 in the bounded wrapper;
report and marker are copied unchanged. Report is3686bytes, has every8908
functional/ABI call per kernel and84paired samples, failures0/completePASS.
Rawreport SHAa22e2753…; marker1e57f53f…. Primary actual JSON is still absent
when separate Luna raw extraction begins. Timing result/decision is pending
that extraction. This wrapper exit is not an independent guest command-exit
witness or marker-close-fault qualification.

No normal BFS/PFS3 comparison or production speedup is implied by preparation,
static review, synthetic fixtures or a prospective microresult. Every actual
sample, mismatch, count and adverse observation must be retained. The overall
six-workload five-times goal remains unachieved.

## Completed extraction, handler trial and rejection

Independent Luna micro extraction precedes primary actual JSON and agrees
on all14 lengths,1172738 calls/kernel, nine clamps and53/25/6 faster/slower/
tied pairs. The unchanged >=44-byte bodies have15/22/5 signs; the dispatch
still adds overhead. Useful <44-byte means justify only a bounded normal
trial. No exclusive kernel latency or causal filesystem improvement follows.

Root applies exact private candidate f9ecf90f… to production assembly for M2.
Two independent build directories reproduce normal ad1f7478… and probe
70ff502b… byte-for-byte. Host and actual ASan/UBSan each pass49suites/472cases;
leak detection is off. All78qualitytests plus ShellCheck/actionlint/clang pass.
Private Linux source identities match;20conformance cases and two actual
FUSE runs pass. Host/Linux gates do not execute Amiga assembly or qualify a
complete geometry matrix. Core/allocator/CRC/snapshot/recovery logic remains
unchanged. No CI or external publication occurs.

Eight fresh normal systems provide four runs/revision balanced by order and
repeat. Independent Luna extraction of16rawTSVs precedes primary normal JSON;
all six workload means, matched deltas, ratios and counts agree. The two
separate diagnostic systems complete too:10systems/20rawTSVs, unchanged strict
verifier PASS. All non-time probe counters match retained absence controls in
both orders; those controls are not contemporaneous normal timing controls.
Inclusive wall scopes do not establish exclusive CPU attribution.

Candidate means change +10.4339% create and +8.9192% write; both are adverse
in every one of four matched pairs. Delete mean is -6.4985%, but two pairs are
adverse. Overall15/24pairs are adverse. Every candidate create/write/delete
run exceeds5x same-run PFS3:12/24ratios fail. Their mean ratios are12.023170,
9.935289 and21.198438 respectively. This rejects adoption without attributing
the regression causally to dispatch, code layout or scheduling.

Root withdraws only the inserted production entry/short path via exact patch.
Assembly matches b1180cee… and a rebuilt normal handler matches qualified
7ec43316… byte-for-byte. The additional equivalent harness tests remain;
keeping tests is not acceptance of rejected assembly. Host/Linux gates are
not rerun solely for Amiga assembly rollback: unchanged shared core already
passes and those gates do not execute the restored assembly.

Ten completed normal/probe systems are recoverably moved to the named Cachy
home cache, with all20before/after raw TSV hashes equal. One completed kernel
system and one unused reproducibility preparation are separately recoverably
archived; both completed micro report/marker hashes remain equal. No unused
second preparation is claimed as a run, and no unrelated user's system is
removed. Source, raw records, logs, configurations, extractors and identities
will be persisted as text only, without binaries, HDFs or licensed assets.

Independent final report review by the distinct metadata-test worker finds
no blocker. It checks normal/micro/probe summaries, source/binary identities,
host/sanitizer/quality/Linux logs and restoration, agreeing with all counts
and the rejection. This paragraph is a primary transcription, not a captured
tool transcript. The reviewer identifies the still-pending evidence manifest.

Direct replay of the renamed evidence fixture scripts initially fails with
MODULE_NOT_FOUND: their unchanged relative imports expect the original parser
filenames. Exact failure/exit1 logs remain. The offline replay wrapper creates
unique temporary directories, copies the original bytes under those expected
names, and executes first24fixtures plus the exact semantic duplicate-counter
RED/exit1, final29fixtures and independent25GREENcases. Every expected gate
passes. No parser, fixture oracle, production source or measured result changes
to repair packaging. Only this wrapper's own temporary directory is removed.

Raw-copy checks prove22newrecords and4retained-control TSVs byte-identical.
All12FS-UAE configurations are explicitly staged despite ignore rules. The
new scoped text-asset audit passes. The actual global audit remains exit1
on68older unchanged C1 formatter records; it is not represented as globally
green. Only new copied formatter text is ASCII-CSI normalized with hashes.
Manifest/inventory/staging checks follow finalization. The five-times target
is still unachieved; rejected experiments must not be reinstated.
