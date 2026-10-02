import fs from 'node:fs';
import assert from 'node:assert/strict';
const raw = JSON.parse(fs.readFileSync('build/memory-copy-short-micro-raw-summary.json', 'utf8'));
const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
const exactNumber = value => {
  const integer = BigInt(value);
  assert(integer >= 0n && integer <= BigInt(Number.MAX_SAFE_INTEGER));
  return Number(integer);
};
const rows = raw.rows.map(row => {
  const before = row.baseline_ticks.map(exactNumber);
  const after = row.candidate_ticks.map(exactNumber);
  assert.equal(before.length, 6);
  assert.equal(after.length, 6);
  return {...row,
    baseline_total_ticks: before.reduce((sum, value) => sum + value, 0),
    candidate_total_ticks: after.reduce((sum, value) => sum + value, 0),
    baseline_mean_us_per_call: mean(before) * 1e6 / raw.counts.clock_hz / row.repeats,
    candidate_mean_us_per_call: mean(after) * 1e6 / raw.counts.clock_hz / row.repeats,
    change_percent: (mean(after) / mean(before) - 1) * 100};
});
console.log(JSON.stringify({counts: raw.counts, rows,
  faster_samples: rows.reduce((sum, row) => sum + row.candidate_faster_samples, 0),
  slower_samples: rows.reduce((sum, row) => sum + row.candidate_slower_samples, 0),
  tied_samples: rows.reduce((sum, row) => sum + row.tied_samples, 0),
  limitation: 'One isolated guest run; indirect-call/loop/checksum/scheduling time, not exclusive kernel latency or BFS speedup. All six observations retained.'}, null, 2));
