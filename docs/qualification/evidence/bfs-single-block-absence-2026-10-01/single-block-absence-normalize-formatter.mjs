// Mechanical evidence formatting only: C1 CSI byte becomes its ASCII ESC[ form.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
const directory = 'docs/qualification/evidence/bfs-single-block-absence-2026-10-01';
const labels = fs.readdirSync(directory).filter(name => name.startsWith('single-block-absence-') && fs.statSync(path.join(directory, name)).isDirectory());
assert.equal(labels.length, 10);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
for (const label of labels) {
  const target = path.join(directory, label, 'system/Results/format-pfs3.txt');
  const original = fs.readFileSync(target);
  assert([...original].filter(value => value >= 128).every(value => value === 0x9b), 'unexpected non-ASCII byte');
  const output = Buffer.from(original.toString('latin1').replaceAll('\u009b', '\u001b['), 'ascii');
  assert.notEqual(original.length, output.length, 'not an unnormalized formatter record');
  fs.writeFileSync(target, output);
  console.log(`${label} original=${sha(original)} ascii-csi=${sha(output)}`);
}
