import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const phases = ['SMALL_CREATE_40', 'LOOKUP_400', 'SMALL_READ_40',
  'SEQ_WRITE_8M', 'SEQ_READ_8M', 'SMALL_DELETE_40'];
const structural = ['BIO_READS', 'BIO_WRITES', 'BIO_UPDATES', 'DATA_READS',
  'DATA_WRITES', 'NODE_WRITES', 'TXN_COMMITS', 'FREESPACE_ALLOCS',
  'FREE_TREE_NODE_WRITES', 'FREE_TREE_ALLOCATION_BODY_NODE_WRITES',
  'FREE_TREE_RESERVE_REFILL_NODE_WRITES', 'FREE_TREE_OTHER_NODE_WRITES',
  'SUPERBLOCK_PUBLICATIONS', 'SEALED_COMMITS', 'SEALED_METADATA_FENCES',
  'NODE_CRC_READ_CALLS', 'NODE_CRC_WRITE_CALLS', 'BTREE_MALLOC_CALLS',
  'BTREE_FREE_CALLS', 'IFACE_ALLOC_CALLS', 'IFACE_FREE_CALLS'];
const scopes = ['PACKET', 'CORE_CREATE', 'CORE_DELETE', 'CORE_FILE_WRITE',
  'CORE_SYNC', 'IFACE_FREE', 'SEAL_COMMIT', 'IFACE_ALLOC', 'FREESPACE_ALLOC'];
const read = (label, drive) => {
  const dir = `build/benchmark/${label}`;
  const verifier = execFileSync('bash', ['emulator-test/verify-bench-results.sh',
    dir, 'deep-compare'], {encoding: 'utf8'}).trimEnd();
  const name = drive === 'DH1:' ? 'bfs' : 'pfs3';
  const rows = fs.readFileSync(`${dir}/system/Results/${name}.deep-compare.tsv`,
    'utf8').trimEnd().split('\n').map(row => row.split('\t'));
  assert.deepEqual(rows[0], ['FS_DEEP_COMPARE', '11']);
  assert.deepEqual(rows[1], ['DRIVE', drive]);
  assert.deepEqual(rows.at(-1), ['PASS', '1']);
  const fields = new Map();
  for (const [key, value, extra] of rows.slice(2, -1)) {
    assert.equal(extra, undefined);
    assert(!fields.has(key));
    assert.match(value, /^\d+$/);
    fields.set(key, value);
  }
  const integer = (key, maximum = 4294967295n) => {
    assert(fields.has(key), `missing ${key}`);
    const value = BigInt(fields.get(key));
    assert(value <= maximum, `out of range ${key}`);
    return value;
  };
  if (drive === 'DH2:') return {label, drive, verifier,
    measurements: Object.fromEntries(phases.map(phase => [phase,
      {elapsed_us: Number(integer(`${phase}_US`))}]))};
  const hz = Number(integer('CLOCK_HZ'));
  assert(hz > 0);
  assert.equal(fields.get('CPU_SAMPLE_STRIDE'), '1');
  const measurements = Object.fromEntries(phases.map(phase => [phase, {
    elapsed_us: Number(integer(`${phase}_US`)),
    all_counters: Object.fromEntries([...fields.keys()].filter(key =>
      key.startsWith(`${phase}_`) && !key.endsWith('_US') &&
      !key.endsWith('_TICKS')).map(key => [key, Number(integer(key))])),
    structural: Object.fromEntries(structural.map(field =>
      [field, Number(integer(`${phase}_${field}`))])),
    inclusive_scopes: Object.fromEntries(scopes.map(scope => {
      const calls = integer(scope === 'FREESPACE_ALLOC'
        ? `${phase}_FREESPACE_ALLOCS` : `${phase}_${scope}_CALLS`);
      const samples = integer(`${phase}_${scope}_SAMPLES`);
      const ticks = integer(`${phase}_${scope}_SAMPLE_TICKS`, 18446744073709551615n);
      assert.equal(calls, samples);
      assert(ticks <= BigInt(Number.MAX_SAFE_INTEGER), 'cannot convert ticks exactly');
      return [scope, {calls: Number(calls), ticks: String(ticks),
        inclusive_us: Number(ticks) * 1e6 / hz}];
    })),
  }]));
  return {label, drive, verifier, hz, measurements};
};
const pairs = ['bfs-first', 'pfs3-first'].map(order => {
  const baseline = read(`single-block-absence-probe-${order}`, 'DH1:');
  const candidate = read(`reserve-pair-validation-probe-${order}`, 'DH1:');
  const pfs3 = read(`reserve-pair-validation-probe-${order}`, 'DH2:');
  const differences = phases.flatMap(phase => structural.flatMap(field => {
    const oldValue = baseline.measurements[phase].structural[field];
    const newValue = candidate.measurements[phase].structural[field];
    return oldValue === newValue ? [] : [{phase, field, baseline: oldValue,
      candidate: newValue, delta: newValue - oldValue}];
  }));
  const allCounterDifferences = phases.flatMap(phase => {
    const before = baseline.measurements[phase].all_counters;
    const after = candidate.measurements[phase].all_counters;
    assert.deepEqual(Object.keys(before), Object.keys(after));
    return Object.keys(before).flatMap(field => before[field] === after[field]
      ? [] : [{field, baseline: before[field], candidate: after[field],
        delta: after[field] - before[field]}]);
  });
  return {order, baseline, candidate, pfs3, structural_differences: differences,
    all_counter_differences: allCounterDifferences};
});
console.log(JSON.stringify({strict_gate: 'PASS', pairs,
  limitation: 'Retained single-block-absence probes are not contemporaneous normal performance controls. Inclusive scope wall intervals overlap; do not sum or subtract them. This pilot changes reserve comparison work, not helper traversal or block I/O. The abstract model and focused equivalence cases do not measure actual runtime comparison counts.'
}, null, 2));
