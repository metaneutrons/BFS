// Mechanical SHA-256 inventory and exact scoped staging verification.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const directory = 'docs/qualification/evidence/bfs-btree-scratch-lifetime-2026-10-01';
function inventory(base, prefix = '') {
  return fs.readdirSync(base).flatMap(name => {
    const relative = path.join(prefix, name), absolute = path.join(base, name);
    const info = fs.lstatSync(absolute);
    assert(!info.isSymbolicLink(), `unexpected evidence symlink: ${relative}`);
    return info.isDirectory() ? inventory(absolute, relative) : [relative];
  }).sort();
}
const names = inventory(directory).filter(name => name !== 'SHA256SUMS');
const digest = name => crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, name))).digest('hex');
const expected = names.map(name => `${digest(name)}  ./${name}`).join('\n') + '\n';
const manifest = path.join(directory, 'SHA256SUMS');
if (process.argv[2] === 'create') fs.writeFileSync(manifest, expected);
else assert.equal(fs.readFileSync(manifest, 'utf8'), expected, 'manifest differs from exact inventory/bytes');
if (process.argv[2] === 'staged') {
  const decision = process.argv[3];
  assert(['retained', 'rejected'].includes(decision), 'explicit adoption decision required');
  const staged = execFileSync('git', ['diff', '--cached', '--name-only', '-z'], {encoding: 'utf8'}).split('\0').filter(Boolean).sort();
  const owned = [...names, 'SHA256SUMS'].map(name => path.join(directory, name));
  owned.push('tests/test_btree_scratch_lifetime.c', 'docs/plans/bfs-btree-scratch-lifetime-v1.md', 'docs/qualification/bfs-btree-scratch-lifetime-performance-2026-10-01.md');
  if (decision === 'retained') owned.push('src/core/btree.c');
  assert.deepEqual(staged, owned.sort(), 'staged scope differs from exact inventory');
  assert.equal(names.filter(name => name.endsWith('.fs-uae')).length, 10);
  console.log(`PASS: ${staged.length} staged files match scope; ten configs; product ${decision}`);
}
console.log(`PASS: ${names.length} SHA-256 entries match ${names.length + 1} evidence files including manifest`);
