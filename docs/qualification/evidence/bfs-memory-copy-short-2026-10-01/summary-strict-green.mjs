import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

/* Synthetic parser fixtures only; these do not represent AmigaOS timings. */
const summary = fileURLToPath(new URL('./memory-copy-short-summary.mjs', import.meta.url));
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'bfs-memory-copy-short-summary-'));
const lengths = [0, 1, 4, 8, 16, 32, 43, 44, 48, 264, 512,
  4096, 65536, 1048576];
const repeats = 10;
const perKernelCalls = lengths.length + lengths.length * 6 * repeats;

function validReport() {
  const numeric = {
    small_expected_cases: 8512,
    small_completed_cases: 8512,
    large_expected_cases: 384,
    large_completed_cases: 384,
    bulk_expected_cases: 12,
    bulk_completed_cases: 12,
    short_timing_mode: 1,
    timing_length_count: lengths.length,
    candidate_checked_calls: 8908,
    baseline_checked_calls: 8908,
    candidate_abi_failures: 0,
    baseline_abi_failures: 0,
    candidate_return_failures: 0,
    baseline_return_failures: 0,
    candidate_data_failures: 0,
    baseline_data_failures: 0,
    abi_negative_control_cases: 2,
    abi_negative_control_passes: 2,
    abi_negative_control_errors: 0,
    allocation_failures: 0,
    timing_available: 1,
    timing_buffers_available: 1,
    clock_hz: 500,
    timing_samples: lengths.length * 6,
    timing_order_baseline_first: lengths.length * 3,
    timing_order_candidate_first: lengths.length * 3,
    timing_repeat_clamps: 0,
    timing_mismatches: 0,
    baseline_timing_calls: perKernelCalls,
    candidate_timing_calls: perKernelCalls,
    baseline_timing_batches: lengths.length * 7,
    candidate_timing_batches: lengths.length * 7,
    failures: 0,
  };
  const rows = lengths.map(length =>
    `timing_length=${length} repeats=${repeats} calibration_ticks=1,1 ` +
    'baseline_ticks=10,10,10,10,10,10 candidate_ticks=11,11,11,11,11,11');
  return [
    'mode=AmigaOS memcpy candidate comparison',
    'timing_mode=short',
    `timing_lengths=${lengths.join(',')}`,
    ...Object.entries(numeric).map(([key, value]) => `${key}=${value}`),
    ...rows,
    'MEMORY-COPY-COMPARE-PASS',
    '',
  ].join('\n');
}

const original = validReport();
const goodMarker = 'MEMORY-COPY-COMPARE-PASS\n';
function swapRows(report, firstLength, secondLength) {
  const rows = report.split('\n');
  const first = rows.findIndex(line => line.startsWith(`timing_length=${firstLength} `));
  const second = rows.findIndex(line => line.startsWith(`timing_length=${secondLength} `));
  assert(first >= 0 && second >= 0, 'fixture rows must exist');
  [rows[first], rows[second]] = [rows[second], rows[first]];
  return rows.join('\n');
}

const tests = [
  ['duplicate-malformed-counter', original.replace('clock_hz=500\n',
    'clock_hz=500\nclock_hz=-1\n'), goodMarker, false],
  ['valid-synthetic', original, goodMarker, true],
  ['missing-row', original.replace(/^timing_length=43 .*\n/m, ''), goodMarker, false],
  ['duplicate-row', original.replace(/^timing_length=43 .*\n/m,
    match => `${match}${match}`), goodMarker, false],
  ['wrong-row-order', swapRows(original, 0, 1), goodMarker, false],
  ['malformed-row', original.replace('timing_length=43 repeats=10',
    'timing_length=43 repeats=10 extra=1'), goodMarker, false],
  ['wrong-repeats', original.replace('timing_length=0 repeats=10',
    'timing_length=0 repeats=11'), goodMarker, false],
  ['wrong-calls', original.replace(`baseline_timing_calls=${perKernelCalls}`,
    `baseline_timing_calls=${perKernelCalls + 1}`), goodMarker, false],
  ['wrong-order', original.replace('timing_order_candidate_first=42',
    'timing_order_candidate_first=41'), goodMarker, false],
  ['wrong-sample-count', original.replace('timing_samples=84',
    'timing_samples=85'), goodMarker, false],
  ['negative-count', original.replace('timing_samples=84',
    'timing_samples=-1'), goodMarker, false],
  ['duplicate-counter', original.replace('clock_hz=500\n',
    'clock_hz=500\nclock_hz=500\n'), goodMarker, false],
  ['extra-counter', original.replace('clock_hz=500\n',
    'clock_hz=500\nunexpected_counter=0\n'), goodMarker, false],
  ['counter-overflow', original.replace('clock_hz=500',
    'clock_hz=4294967296'), goodMarker, false],
  ['tick-overflow', original.replace('baseline_ticks=10,10',
    'baseline_ticks=18446744073709551616,10'), goodMarker, false],
  ['zero-sample-tick', original.replace('baseline_ticks=10,10',
    'baseline_ticks=0,10'), goodMarker, false],
  ['wrong-repeat-clamps', original.replace('timing_repeat_clamps=0',
    'timing_repeat_clamps=1'), goodMarker, false],
  ['timing-mismatch', original.replace('timing_mismatches=0',
    'timing_mismatches=1'), goodMarker, false],
  ['abi-failure', original.replace('candidate_abi_failures=0',
    'candidate_abi_failures=1'), goodMarker, false],
  ['duplicate-pass', `${original}MEMORY-COPY-COMPARE-PASS\n`, goodMarker, false],
  ['bad-done', original, 'MEMORY-COPY-COMPARE-FAIL\n', false],
  ['missing-done', original, '', false],
  ['fail-report', original.replace('MEMORY-COPY-COMPARE-PASS',
    'MEMORY-COPY-COMPARE-FAIL'), goodMarker, false],
  ['truncated-report', original.slice(0, -30), goodMarker, false],
  ['wrong-mode', original.replace('timing_mode=short',
    'timing_mode=default'), goodMarker, false],
];

try {
  for (const [name, report, marker, expected] of tests) {
    const directory = path.join(root, name);
    const results = path.join(directory, 'system', 'Results');
    fs.mkdirSync(results, {recursive: true});
    fs.writeFileSync(path.join(results, 'memory-copy-compare.txt'), report);
    fs.writeFileSync(path.join(results, 'memory-copy-compare.done'), marker);
    const result = spawnSync(process.execPath, [summary, directory], {
      encoding: 'utf8',
    });
    assert.equal(result.error, undefined, `${name}: ${result.error}`);
    assert.equal(result.status === 0, expected,
      `${name}: expected ${expected ? 'accept' : 'reject'}, got ${result.status}: ${result.stderr}`);
    if (expected) {
      const decoded = JSON.parse(result.stdout);
      assert.equal(decoded.qualification, false);
      assert.equal(decoded.rows.length, lengths.length);
      assert.equal(decoded.rows[0].candidate_slower_samples, 6);
    }
    console.log(`${name}: ${expected ? 'accepted' : 'rejected'} as expected`);
  }
  console.log(`Synthetic parser checks passed (${tests.length} fixtures); no emulator timing or qualification performed.`);
} finally {
  fs.rmSync(root, {recursive: true, force: true});
}
