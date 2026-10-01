import fs from 'node:fs';
import assert from 'node:assert/strict';
const directory = process.argv[2];
assert(directory);
const report = fs.readFileSync(`${directory}/system/Results/memory-copy-compare.txt`, 'utf8');
assert.equal(fs.readFileSync(`${directory}/system/Results/memory-copy-compare.done`, 'utf8'), 'MEMORY-COPY-COMPARE-PASS\n');
assert.equal(report.split('\n').filter(line => line === 'MEMORY-COPY-COMPARE-PASS').length, 1);
assert(!report.includes('MEMORY-COPY-COMPARE-FAIL'));
const counts = new Map();
for (const line of report.split('\n')) {
  const match = /^([a-z_]+)=(\d+)$/.exec(line);
  if (!match) continue;
  assert(!counts.has(match[1]));
  const value = Number(match[2]);
  assert(Number.isSafeInteger(value) && value >= 0 && value <= 0xffffffff);
  counts.set(match[1], value);
}
const expected = {
  small_expected_cases:8512, small_completed_cases:8512,
  large_expected_cases:384, large_completed_cases:384,
  bulk_expected_cases:12, bulk_completed_cases:12,
  candidate_checked_calls:8908, baseline_checked_calls:8908,
  candidate_abi_failures:0, baseline_abi_failures:0,
  candidate_return_failures:0, baseline_return_failures:0,
  candidate_data_failures:0, baseline_data_failures:0,
  abi_negative_control_cases:2, abi_negative_control_passes:2,
  abi_negative_control_errors:0, allocation_failures:0,
  timing_available:1, timing_buffers_available:1, timing_samples:48,
  timing_order_baseline_first:24, timing_order_candidate_first:24,
  timing_mismatches:0, baseline_timing_batches:56, candidate_timing_batches:56,
  failures:0,
};
for (const [name, value] of Object.entries(expected)) assert.equal(counts.get(name), value, name);
const hz = counts.get('clock_hz');
assert(Number.isSafeInteger(hz) && hz > 0);
const lengths = [0,44,48,264,512,4096,65536,1048576];
const rows = report.split('\n').filter(line => line.startsWith('timing_length='));
assert.equal(rows.length, lengths.length);
let calls = 8, clamps = 0;
const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
const summaries = rows.map((line, index) => {
  const match = /^timing_length=(\d+) repeats=(\d+) calibration_ticks=(\d+),(\d+) baseline_ticks=(\d+(?:,\d+){5}) candidate_ticks=(\d+(?:,\d+){5})$/.exec(line);
  assert(match, line);
  const integers = text => text.split(',').map(raw => {
    const exact = BigInt(raw);
    assert(exact <= 0xffffffffffffffffn && exact >= 0n);
    assert(exact <= BigInt(Number.MAX_SAFE_INTEGER));
    return Number(exact);
  });
  const length = Number(match[1]), repeats = Number(match[2]);
  assert.equal(length, lengths[index]);
  const calibration = [integers(match[3])[0], integers(match[4])[0]];
  const perCall = Math.max(...calibration);
  const required = perCall === 0 ? 20000 : Math.max(1, Math.ceil(Math.ceil(hz / 50) / perCall));
  assert.equal(repeats, Math.min(20000, required));
  if (perCall === 0 || required > 20000) clamps++;
  const baseline = integers(match[5]), candidate = integers(match[6]);
  assert([...baseline,...candidate].every(value => value > 0));
  calls += 6 * repeats;
  return {length, repeats, calibration, baseline, candidate,
    baseline_us_per_call:mean(baseline) / hz * 1e6 / repeats,
    candidate_us_per_call:mean(candidate) / hz * 1e6 / repeats,
    change_percent:(mean(candidate) / mean(baseline) - 1) * 100,
    faster_samples:candidate.filter((value, at) => value < baseline[at]).length,
  };
});
assert.equal(counts.get('baseline_timing_calls'), calls);
assert.equal(counts.get('candidate_timing_calls'), calls);
assert.equal(counts.get('timing_repeat_clamps'), clamps);
console.log(JSON.stringify({counts:Object.fromEntries(counts), summaries}, null, 2));
