import fs from 'node:fs';
import assert from 'node:assert/strict';

const filename = process.argv[2];
assert(filename, 'probe result path required');
const raw = fs.readFileSync(filename, 'utf8');
const lines = raw.trimEnd().split('\n');
const expected = {
  small_vector_cases: 17024, large_vector_cases: 320,
  small_chain_cases: 17024, large_chain_cases: 84,
  null_zero_cases: 4, known_vector_cases: 1,
  abi_negative_control_cases: 2, abi_negative_control_passes: 2,
  abi_negative_control_errors: 0,
  baseline_crc_calls: 51565, candidate_crc_calls: 51565,
  baseline_abi_failures: 0, candidate_abi_failures: 0,
  baseline_crc_failures: 0, candidate_crc_failures: 0,
  timer_available: 1, clock_hz: 709379,
  timing_samples: 48, timing_mismatches: 0,
  timing_order_baseline_first: 24, timing_order_candidate_first: 24,
  baseline_timing_calls: 651480, candidate_timing_calls: 651480,
  failures: 0,
};
const seen = new Map();
const rows = [];
const lengths = [0, 1, 3, 4, 44, 256, 4096, 65536];
const repeats = [50000, 20000, 20000, 15000, 3000, 512, 64, 4];
const uint64Max = (1n << 64n) - 1n;
for (const line of lines) {
  if (line === 'mode=AmigaOS CRC32 kernel probe' ||
      line === 'timing_note=raw EClock ticks only; call order alternates; no speed claim' ||
      line === 'timing_seed=12345678 data_offset=11' ||
      line === 'timing_checksum=00000000' || line === 'CRC32-PROBE-PASS') {
    seen.set(line, (seen.get(line) || 0) + 1);
    assert.equal(seen.get(line), 1, `duplicate ${line}`);
    continue;
  }
  const scalar = /^([a-z_]+)=([0-9]+)$/.exec(line);
  if (scalar) {
    const [, key, value] = scalar;
    assert(Object.hasOwn(expected, key), `unexpected scalar ${key}`);
    assert.equal(BigInt(value), BigInt(expected[key]), key);
    seen.set(key, (seen.get(key) || 0) + 1);
    assert(seen.get(key) <= (key === 'clock_hz' ? 2 : 1), `duplicate ${key}`);
    continue;
  }
  const row = /^timing_length=([0-9]+) repeats=([0-9]+) samples=([0-9,]+) candidate_ticks=([0-9,]+)$/.exec(line);
  assert(row, `malformed or unexpected line ${line}`);
  const index = rows.length;
  assert(index < lengths.length, 'too many timing rows');
  assert.equal(BigInt(row[1]), BigInt(lengths[index]), 'timing length/order');
  assert.equal(BigInt(row[2]), BigInt(repeats[index]), 'repeats');
  const arrays = row.slice(3).map(part => part.split(',').map(value => {
    const tick = BigInt(value);
    assert(tick > 0n && tick <= uint64Max, 'nonpositive/out-of-range ticks');
    return tick;
  }));
  for (const values of arrays) assert.equal(values.length, 6, 'sample count');
  const sums = arrays.map(values => values.reduce((a, b) => a + b, 0n));
  const means = sums.map(sum => Number(sum) / 6);
  assert(sums.every(sum => sum <= BigInt(Number.MAX_SAFE_INTEGER)), 'unsafe floating conversion');
  rows.push({
    length: lengths[index], repeats: repeats[index],
    baseline_ticks: arrays[0].map(String), candidate_ticks: arrays[1].map(String),
    baseline_mean_ticks: means[0], candidate_mean_ticks: means[1],
    baseline_us_per_call: means[0] * 1e6 / expected.clock_hz / repeats[index],
    candidate_us_per_call: means[1] * 1e6 / expected.clock_hz / repeats[index],
    elapsed_change_percent: (means[1] / means[0] - 1) * 100,
    candidate_faster_samples: arrays[0].filter((tick, sample) => arrays[1][sample] < tick).length,
  });
}
assert.equal(rows.length, 8, 'eight timing lengths required');
for (const key of Object.keys(expected))
  assert.equal(seen.get(key), key === 'clock_hz' ? 2 : 1, `missing ${key}`);
for (const key of [
  'mode=AmigaOS CRC32 kernel probe',
  'timing_note=raw EClock ticks only; call order alternates; no speed claim',
  'timing_seed=12345678 data_offset=11', 'timing_checksum=00000000',
  'CRC32-PROBE-PASS',
]) assert.equal(seen.get(key), 1, `missing ${key}`);
assert.equal(lines.at(-1), 'CRC32-PROBE-PASS', 'final marker');
console.log(JSON.stringify({
  result_file: filename, strict_gate: 'PASS', raw_clock_hz: expected.clock_hz,
  functional_calls_per_kernel: 51565, timing_calls_per_kernel: 651480,
  timing_note: 'Inclusive timed loops in one emulator invocation, not filesystem elapsed or hardware qualification.',
  rows,
}, null, 2));
