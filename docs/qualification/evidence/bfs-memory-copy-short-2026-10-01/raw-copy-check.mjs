// Verify persisted raw records without normalizing their bytes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
const destination = 'docs/qualification/evidence/bfs-memory-copy-short-2026-10-01';
const labels = fs.readdirSync(destination).filter(name => name.startsWith('memory-copy-short-') && fs.statSync(path.join(destination, name)).isDirectory());
assert.equal(labels.length, 10);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
let trial = 0, controls = 0;
function compare(source, target, category) {
  const original = fs.readFileSync(source), copied = fs.readFileSync(target);
  assert(original.equals(copied), `raw bytes differ: ${target}`);
  console.log(`${category} ${target} ${sha(original)} byte-identical`);
}
for (const label of labels) {
  const source = path.join('build/benchmark', label, 'system/Results');
  const names = fs.readdirSync(source).filter(name => name.endsWith('.tsv'));
  assert.equal(names.length, 2);
  for (const name of names) {
    compare(path.join(source, name), path.join(destination, label, 'system/Results', name), 'trial');
    trial++;
  }
}
for (const name of ['memory-copy-compare.txt', 'memory-copy-compare.done']) {
  compare(path.join('build/memory-copy-short/run.l3US52/system/Results', name), path.join(destination, 'kernel-run.l3US52/system/Results', name), 'trial');
  trial++;
}
for (const order of ['bfs-first', 'pfs3-first']) {
  const label = `single-block-absence-probe-${order}`;
  const source = path.join('build/benchmark', label, 'system/Results');
  const names = fs.readdirSync(source).filter(name => name.endsWith('.tsv'));
  assert.equal(names.length, 2);
  for (const name of names) {
    compare(path.join(source, name), path.join(destination, 'retained-probe-controls', label, 'system/Results', name), 'retained-control');
    controls++;
  }
}
assert.equal(trial, 22);
assert.equal(controls, 4);
console.log('PASS: 22 new raw records and 4 retained-control TSVs are byte-identical');
