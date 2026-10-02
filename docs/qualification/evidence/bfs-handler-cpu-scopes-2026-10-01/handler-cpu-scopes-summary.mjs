import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const root = '/Users/fabian/.codex/worktrees/bfs-append-performance/BFS';
process.chdir(root);
const labels = ['control-bfs-first', 'extended-pfs3-first', 'control-pfs3-first', 'extended-bfs-first'];
const phases = ['SMALL_CREATE_40', 'LOOKUP_400', 'SMALL_READ_40', 'SEQ_WRITE_8M', 'SEQ_READ_8M', 'SMALL_DELETE_40'];
const header = fs.readFileSync('src/amiga/perf_probe.h', 'utf8');
const scopeMacro = header.slice(header.indexOf('#define BFS_PERF_CPU_SCOPES(X)'), header.indexOf('typedef struct bfs_perf_probe_snapshot'));
const scopes = [...scopeMacro.matchAll(/X\(([A-Z_]+), [a-z_]+\)/g)].map(match => match[1]);
assert.equal(scopes.length, 14);
const parse = (filename, drive, version) => {
  const rows = fs.readFileSync(filename, 'utf8').trim().split(/\r?\n/).map(line => line.split('\t'));
  assert.deepEqual(rows[0], ['FS_DEEP_COMPARE', String(version)]);
  assert.deepEqual(rows[1], ['DRIVE', drive]);
  assert.deepEqual(rows.at(-1), ['PASS', '1']);
  const data = {};
  for (const [key, value, extra] of rows.slice(2, -1)) {
    assert.equal(extra, undefined);
    assert.equal(Object.hasOwn(data, key), false);
    assert.match(value, /^\d+$/);
    data[key] = value;
  }
  return data;
};
const number = value => { const n = Number(value); assert(Number.isSafeInteger(n)); return n; };
const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
const runs = {};
for (const label of labels) {
  const dir = `build/benchmark/handler-cpu-scopes-${label}`;
  execFileSync('bash', ['emulator-test/verify-bench-results.sh', dir, 'deep-compare']);
  const version = label.startsWith('control-') ? 10 : 11;
  const bfs = parse(`${dir}/system/Results/bfs.deep-compare.tsv`, 'DH1:', version);
  const pfs3 = parse(`${dir}/system/Results/pfs3.deep-compare.tsv`, 'DH2:', version);
  const hz = number(bfs.CLOCK_HZ);
  assert(hz > 0);
  assert.equal(bfs.CPU_SAMPLE_STRIDE, '1');
  const inclusiveScopes = {};
  if (version === 11) for (const phase of phases) {
    inclusiveScopes[phase] = {};
    for (const scope of scopes) {
      const calls = number(bfs[`${phase}_${scope}_CALLS`]);
      const samples = number(bfs[`${phase}_${scope}_SAMPLES`]);
      const ticks = bfs[`${phase}_${scope}_SAMPLE_TICKS`];
      assert.equal(calls, samples);
      assert(calls <= 4294967295 && BigInt(ticks) <= 18446744073709551615n);
      inclusiveScopes[phase][scope] = {calls, samples, ticks, inclusiveUs: number(ticks) * 1e6 / hz};
    }
    const categories = scopes.filter(scope => scope.startsWith('PACKET_'));
    const sumCalls = categories.reduce((sum, scope) => sum + inclusiveScopes[phase][scope].calls, 0);
    const sumTicks = categories.reduce((sum, scope) => sum + BigInt(inclusiveScopes[phase][scope].ticks), 0n);
    assert.equal(sumCalls, inclusiveScopes[phase].PACKET.calls);
    assert.equal(String(sumTicks), inclusiveScopes[phase].PACKET.ticks);
  }
  runs[label] = {version, bfs, pfs3, hz, inclusiveScopes};
}
const pairs = [['control-bfs-first', 'extended-bfs-first'], ['control-pfs3-first', 'extended-pfs3-first']];
const summaries = phases.map(phase => {
  const controlBfs = pairs.map(([control]) => number(runs[control].bfs[`${phase}_US`]));
  const extendedBfs = pairs.map(([, extended]) => number(runs[extended].bfs[`${phase}_US`]));
  const controlPfs3 = pairs.map(([control]) => number(runs[control].pfs3[`${phase}_US`]));
  const extendedPfs3 = pairs.map(([, extended]) => number(runs[extended].pfs3[`${phase}_US`]));
  const inclusiveScopes = Object.fromEntries(scopes.map(scope => [scope, {
    calls: pairs.map(([, extended]) => runs[extended].inclusiveScopes[phase][scope].calls),
    ticks: pairs.map(([, extended]) => runs[extended].inclusiveScopes[phase][scope].ticks),
    inclusiveUsMean: mean(pairs.map(([, extended]) => runs[extended].inclusiveScopes[phase][scope].inclusiveUs)),
  }]));
  const existingInclusive = {};
  for (const name of ['BTREE_MALLOC', 'BTREE_FREE', 'IFACE_ALLOC', 'FREESPACE_ALLOC']) {
    existingInclusive[name] = {controlUsMean: mean(pairs.map(([control]) => number(runs[control].bfs[`${phase}_${name}_SAMPLE_TICKS`]) * 1e6 / runs[control].hz)), extendedUsMean: mean(pairs.map(([, extended]) => number(runs[extended].bfs[`${phase}_${name}_SAMPLE_TICKS`]) * 1e6 / runs[extended].hz))};
  }
  return {phase, controlBfs, extendedBfs, controlPfs3, extendedPfs3, controlBfsMean: mean(controlBfs), extendedBfsMean: mean(extendedBfs), matchedExtendedChangesPercent: extendedBfs.map((value, i) => 100 * (value / controlBfs[i] - 1)), inclusiveScopes, existingInclusive};
});
console.log(JSON.stringify({labels, pairs, scopes, runs, summaries, limitation: 'Inclusive EClock wall intervals overlap. Probe/control variation is not unbiased overhead or normal-handler performance acceptance.'}, null, 2));
