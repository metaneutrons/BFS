// Diagnostic input validation does not itself prove retained-counter equivalence.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const root = process.argv[2] || 'build/benchmark';
const verifier = process.argv[3] || 'emulator-test/verify-bench-results.sh';
const controlsRoot = process.argv[4] || root;
function fields(directory, schema) {
  const rows = fs.readFileSync(`${directory}/system/Results/bfs.deep-compare.tsv`,
    'utf8').trimEnd().split('\n').map(row => row.split('\t'));
  assert.deepEqual(rows[0], ['FS_DEEP_COMPARE', String(schema)]);
  assert.deepEqual(rows[1], ['DRIVE', 'DH1:']);
  assert.deepEqual(rows.at(-1), ['PASS', '1']);
  const result = new Map();
  for (const row of rows.slice(2, -1)) {
    assert.equal(row.length, 2);
    assert(!result.has(row[0]), `duplicate ${row[0]}`);
    assert.match(row[1], /^\d+$/);
    result.set(row[0], row[1]);
  }
  return result;
}
const pairs = ['bfs-first', 'pfs3-first'].map(order => {
  const control = `${controlsRoot}/single-block-absence-probe-${order}`;
  const diagnostic = `${root}/split-coalesce-attribution-probe-${order}`;
  const retainedVerifier = execFileSync('bash', [verifier, control,
    'deep-compare'], {encoding: 'utf8'}).trimEnd();
  const before = fields(control, 11);
  const after = fields(diagnostic, 12);
  const retainedKeys = [...before.keys()].filter(key =>
    !key.endsWith('_US') && !key.endsWith('_TICKS'));
  const differences = retainedKeys.flatMap(key => {
    assert(after.has(key), `missing retained field ${key}`);
    return before.get(key) === after.get(key) ? [] : [{key,
      retained: before.get(key), diagnostic: after.get(key)}];
  });
  return {order, retained_verifier: retainedVerifier,
    retained_non_time_fields_compared: retainedKeys.length,
    compared_keys: retainedKeys, differences};
});
const pass = pairs.every(pair => pair.differences.length === 0);
console.log(JSON.stringify({gate: pass ? 'PASS' : 'FAIL', pairs,
  limitation: 'Only pre-existing non-time fields are compared. Retained probes are not contemporaneous normal elapsed controls; new split/coalescence counters are separately validated. No latency or <=5x acceptance follows.'
}, null, 2));
if (!pass) process.exitCode = 1;
