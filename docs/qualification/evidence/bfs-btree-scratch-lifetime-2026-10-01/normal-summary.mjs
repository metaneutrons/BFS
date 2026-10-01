import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';

const root = process.cwd();
const phases = ['SMALL_CREATE_40_US', 'LOOKUP_400_US', 'SMALL_READ_40_US',
  'SEQ_WRITE_8M_US', 'SEQ_READ_8M_US', 'SMALL_DELETE_40_US'];
const suffixes = ['baseline-bfs-first', 'candidate-pfs3-first',
  'baseline-pfs3-first', 'candidate-bfs-first', 'repeat-candidate-bfs-first',
  'repeat-baseline-pfs3-first', 'repeat-candidate-pfs3-first',
  'repeat-baseline-bfs-first'];
const mean = values => values.reduce((a, b) => a + b, 0) / values.length;
const change = (before, after) => (after / before - 1) * 100;
function parse(filename, drive) {
  const lines = fs.readFileSync(filename, 'utf8').trimEnd().split('\n');
  assert.equal(lines.length, 9, filename);
  const entries = new Map();
  for (const line of lines) {
    const cells = line.split('\t');
    assert.equal(cells.length, 2, `malformed ${filename}`);
    assert(!entries.has(cells[0]), `duplicate ${cells[0]}`);
    entries.set(...cells);
  }
  assert.equal(entries.get('FS_COMPARE_BENCH'), '1');
  assert.equal(entries.get('DRIVE'), drive);
  assert.equal(entries.get('PASS'), '1');
  return Object.fromEntries(phases.map(phase => {
    const value = entries.get(phase);
    assert(/^[0-9]+$/.test(value || ''), `missing/malformed ${phase}`);
    const integer = BigInt(value);
    assert(integer > 0n && integer <= 4294967295n, `elapsed range ${phase}`);
    return [phase, Number(integer)];
  }));
}
const runs = suffixes.map(suffix => {
  const label = `btree-scratch-lifetime-normal-${suffix}`;
  const directory = path.join(root, 'build/benchmark', label);
  const verifierOutput = execFileSync(path.join(root, 'emulator-test/verify-bench-results.sh'),
    [directory, 'compare'], {encoding: 'utf8'});
  return {label, suffix, verifier_output: verifierOutput.trimEnd(),
    bfs: parse(path.join(directory, 'system/Results/bfs.tsv'), 'DH1:'),
    pfs3: parse(path.join(directory, 'system/Results/pfs3.tsv'), 'DH2:')};
});
const indexed = new Map(runs.map(run => [run.suffix, run]));
const baseline = runs.filter(run => run.suffix.includes('baseline'));
const candidate = runs.filter(run => run.suffix.includes('candidate'));
assert.equal(baseline.length, 4);
assert.equal(candidate.length, 4);
const pairs = ['bfs-first', 'pfs3-first', 'repeat-bfs-first', 'repeat-pfs3-first']
  .map(label => {
    const prefix = label.startsWith('repeat-') ? 'repeat-' : '';
    const order = label.replace(/^repeat-/, '');
    const before = indexed.get(`${prefix}baseline-${order}`);
    const after = indexed.get(`${prefix}candidate-${order}`);
    assert(before && after, 'explicit order/repeat pair');
    return {label, baseline: before.label, candidate: after.label,
      bfs_change_percent: Object.fromEntries(phases.map(phase =>
        [phase, change(before.bfs[phase], after.bfs[phase])])),
      pfs3_change_percent: Object.fromEntries(phases.map(phase =>
        [phase, change(before.pfs3[phase], after.pfs3[phase])]))};
  });
const summary = phases.map(phase => {
  const oldBfs = mean(baseline.map(run => run.bfs[phase]));
  const newBfs = mean(candidate.map(run => run.bfs[phase]));
  const oldPfs = mean(baseline.map(run => run.pfs3[phase]));
  const newPfs = mean(candidate.map(run => run.pfs3[phase]));
  const ratios = candidate.map(run => ({label: run.label,
    ratio: run.bfs[phase] / run.pfs3[phase]}));
  return {phase, baseline_bfs_mean_us: oldBfs, candidate_bfs_mean_us: newBfs,
    bfs_change_percent: change(oldBfs, newBfs),
    baseline_pfs3_mean_us: oldPfs, candidate_pfs3_mean_us: newPfs,
    pfs3_change_percent: change(oldPfs, newPfs),
    candidate_ratio_of_means: newBfs / newPfs,
    same_run_candidate_pfs3_ratios: ratios,
    same_run_threshold_failures: ratios.filter(row => row.ratio > 5).length,
    adverse_bfs_pairs: pairs.filter(pair => pair.bfs_change_percent[phase] > 0).length};
});
console.log(JSON.stringify({strict_gate: 'PASS', runs, pairs, summary,
  adverse_bfs_pairs: summary.reduce((sum, row) => sum + row.adverse_bfs_pairs, 0),
  same_run_threshold_failures: summary.reduce((sum, row) => sum + row.same_run_threshold_failures, 0),
  limit: 'Four normal runs per revision, balanced order/repeat. No scheduling control or causal precision; probe and microtimes are separate evidence.'
}, null, 2));
