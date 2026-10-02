import fs from 'node:fs';
import assert from 'node:assert/strict';

const directory = process.argv[2];
assert(directory, 'usage: node build/memory-copy-short-summary.mjs <run-dir>');

const results = `${directory}/system/Results`;
const report = fs.readFileSync(`${results}/memory-copy-compare.txt`, 'utf8');
const marker = fs.readFileSync(`${results}/memory-copy-compare.done`, 'utf8');
const pass = 'MEMORY-COPY-COMPARE-PASS';
const fail = 'MEMORY-COPY-COMPARE-FAIL';
const expectedLengths = [0, 1, 4, 8, 16, 32, 43, 44, 48, 264, 512,
  4096, 65536, 1048576];
const expectedLengthText = expectedLengths.join(',');
const maxU32 = 0xffffffffn;
const maxU64 = 0xffffffffffffffffn;
const maxRepeats = 20000n;

assert.equal(marker, `${pass}\n`, 'completion marker must be exact PASS');
assert(report.endsWith('\n'), 'report is truncated or lacks final newline');
assert(!report.includes(fail), 'report contains FAIL marker');
const lines = report.split('\n');
assert.equal(lines[lines.length - 2], pass, 'PASS must be the final report line');
assert.equal(lines.filter(line => line === pass).length, 1,
  'report must contain exactly one PASS marker');
assert.equal(lines.filter(line => line === fail).length, 0,
  'report must not contain a FAIL marker');

function exactlyOneLine(prefix, expected) {
  const found = lines.filter(line => line.startsWith(prefix));
  assert.equal(found.length, 1, `${prefix} must appear exactly once`);
  assert.equal(found[0], expected, `${prefix} value`);
}

exactlyOneLine('timing_mode=', 'timing_mode=short');
exactlyOneLine('timing_lengths=', `timing_lengths=${expectedLengthText}`);

const counts = new Map();
for (const line of lines) {
  const match = /^([a-z_]+)=(0|[1-9][0-9]*)$/.exec(line);
  if (!match) continue;
  assert(!counts.has(match[1]), `duplicate counter ${match[1]}`);
  const value = BigInt(match[2]);
  assert(value <= maxU32, `${match[1]} exceeds uint32`);
  counts.set(match[1], Number(value));
}

const rowLines = lines.filter(line => line.startsWith('timing_length='));
assert.equal(rowLines.length, expectedLengths.length, 'unexpected timing row count');

function parseU64(raw, name) {
  assert(/^(0|[1-9][0-9]*)$/.test(raw), `${name} is not canonical decimal`);
  const value = BigInt(raw);
  assert(value <= maxU64, `${name} exceeds uint64`);
  return value;
}

const ceilDiv = (a, b) => (a + b - 1n) / b;
const clockHz = BigInt(counts.get('clock_hz') ?? 0);
assert(clockHz > 0n && clockHz <= maxU32, 'clock_hz must be a positive uint32');
const targetTicks = ceilDiv(clockHz, 50n);
let expectedCallsPerKernel = BigInt(expectedLengths.length);
let expectedClamps = 0;
const rows = rowLines.map((line, index) => {
  const match = /^timing_length=(0|[1-9][0-9]*) repeats=(0|[1-9][0-9]*) calibration_ticks=(0|[1-9][0-9]*),(0|[1-9][0-9]*) baseline_ticks=((?:0|[1-9][0-9]*)(?:,(?:0|[1-9][0-9]*)){5}) candidate_ticks=((?:0|[1-9][0-9]*)(?:,(?:0|[1-9][0-9]*)){5})$/.exec(line);
  assert(match, `malformed timing row: ${line}`);

  const length = Number(match[1]);
  assert.equal(length, expectedLengths[index], `timing row ${index} length/order`);
  const repeats = BigInt(match[2]);
  assert(repeats >= 1n && repeats <= maxRepeats, `invalid repeats at length ${length}`);
  const calibration = [
    parseU64(match[3], `length ${length} baseline calibration`),
    parseU64(match[4], `length ${length} candidate calibration`),
  ];
  const perCall = calibration[0] > calibration[1] ? calibration[0] : calibration[1];
  let required;
  let clamped = false;
  if (perCall === 0n) {
    required = maxRepeats;
    clamped = true;
  } else {
    required = ceilDiv(targetTicks, perCall);
    if (required < 1n) required = 1n;
    if (required > maxRepeats) {
      required = maxRepeats;
      clamped = true;
    }
  }
  assert.equal(repeats, required, `repeat calibration at length ${length}`);
  if (clamped) expectedClamps++;

  const baseline = match[5].split(',').map((value, sample) =>
    parseU64(value, `length ${length} baseline sample ${sample}`));
  const candidate = match[6].split(',').map((value, sample) =>
    parseU64(value, `length ${length} candidate sample ${sample}`));
  assert.equal(baseline.length, 6, `length ${length} baseline sample count`);
  assert.equal(candidate.length, 6, `length ${length} candidate sample count`);
  assert([...baseline, ...candidate].every(value => value > 0n),
    `zero EClock sample at length ${length}`);

  expectedCallsPerKernel += repeats * 6n;
  return {
    length,
    repeats: Number(repeats),
    calibration_ticks: calibration.map(String),
    baseline_ticks: baseline.map(String),
    candidate_ticks: candidate.map(String),
    candidate_faster_samples: candidate.filter((value, sample) =>
      value < baseline[sample]).length,
    candidate_slower_samples: candidate.filter((value, sample) =>
      value > baseline[sample]).length,
    tied_samples: candidate.filter((value, sample) =>
      value === baseline[sample]).length,
  };
});

const expected = {
  small_expected_cases: 8512,
  small_completed_cases: 8512,
  large_expected_cases: 384,
  large_completed_cases: 384,
  bulk_expected_cases: 12,
  bulk_completed_cases: 12,
  short_timing_mode: 1,
  timing_length_count: expectedLengths.length,
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
  timing_samples: expectedLengths.length * 6,
  timing_order_baseline_first: expectedLengths.length * 3,
  timing_order_candidate_first: expectedLengths.length * 3,
  timing_mismatches: 0,
  baseline_timing_calls: Number(expectedCallsPerKernel),
  candidate_timing_calls: Number(expectedCallsPerKernel),
  baseline_timing_batches: expectedLengths.length * 7,
  candidate_timing_batches: expectedLengths.length * 7,
  failures: 0,
};
expected.timing_repeat_clamps = expectedClamps;

const expectedKeys = Object.keys(expected).concat('clock_hz').sort();
assert.deepEqual([...counts.keys()].sort(), expectedKeys,
  'numeric counter set must be complete and exact');
for (const [name, value] of Object.entries(expected))
  assert.equal(counts.get(name), value, name);

console.log(JSON.stringify({
  qualification: false,
  note: 'Validated report structure only; timings remain raw and unqualified here.',
  counts: Object.fromEntries(counts),
  rows,
}, null, 2));
