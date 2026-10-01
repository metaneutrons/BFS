// Replay this audit without an emulator, network or production-source writes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {execFileSync, spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const archive = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(process.argv[2] || path.join(archive, '../../../..'));
assert([undefined, '--allow-unmanifested'].includes(process.argv[3]));
function inventory(directory = archive, prefix = '') {
  return fs.readdirSync(directory).flatMap(name => {
    const relative = `${prefix}${name}`;
    const status = fs.lstatSync(path.join(directory, name));
    assert(!status.isSymbolicLink());
    if (status.isDirectory()) return inventory(path.join(directory, name), `${relative}/`);
    assert(status.isFile());
    return [relative];
  }).sort();
}
if (process.argv[3] !== '--allow-unmanifested') {
  const manifest = fs.readFileSync(path.join(archive, 'SHA256SUMS'), 'utf8');
  assert(manifest.endsWith('\n'));
  const entries = manifest.trimEnd().split('\n').map(line => {
    const match = /^([a-f0-9]{64})  ([A-Za-z0-9_./-]+)$/.exec(line);
    assert(match);
    assert(!path.isAbsolute(match[2]) && !match[2].split('/').some(part => part === '..' || part === '.'));
    return {digest: match[1], name: match[2]};
  });
  assert.deepEqual(entries.map(entry => entry.name), inventory().filter(name => name !== 'SHA256SUMS'));
  for (const {digest, name} of entries)
    assert.equal(createHash('sha256').update(fs.readFileSync(path.join(archive, name))).digest('hex'), digest);
  console.log(`Exact evidence inventory and SHA-256: PASS (${entries.length} files)`);
}
const script = path.join(archive, 'free-tree-non-split-counter-extract.mjs');
const expected = fs.readFileSync(path.join(archive, 'free-tree-non-split-counter-extract.json'));
const extract = root => execFileSync(process.execPath, [script, root], {
  cwd: os.tmpdir(), maxBuffer: 4 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'],
});
assert(extract(repository).equals(expected), 'exact JSON replay failed');
console.log('Raw-identity and exact JSON replay from outside the repository: PASS');
const json = JSON.parse(expected);
const table = {
  SMALL_CREATE_40: [353, 233, 40, 80, 233, 193, 40],
  LOOKUP_400: [0, 0, 0, 0, 0, 0, 0],
  SMALL_READ_40: [0, 0, 0, 0, 0, 0, 0],
  SEQ_WRITE_8M: [142, 137, 1, 4, 137, 259, 1],
  SEQ_READ_8M: [0, 0, 0, 0, 0, 0, 0],
  SMALL_DELETE_40: [197, 116, 40, 41, 116, 116, 40],
};
const keys = ['FREE_TREE_NODE_WRITES', 'FREE_TREE_ALLOCATION_BODY_NODE_WRITES',
  'FREE_TREE_RESERVE_REFILL_NODE_WRITES', 'FREE_TREE_OTHER_NODE_WRITES',
  'FREESPACE_ALLOCS', 'IFACE_ALLOC_CALLS', 'SEALED_COMMITS'];
for (const [phase, values] of Object.entries(table)) {
  assert.deepEqual(keys.map(key => json.phases[phase].counters[key]), values);
  for (const order of Object.values(json.artifacts)) {
    const fields = new Map(fs.readFileSync(path.join(repository, order.bfs.path), 'utf8')
      .trimEnd().split('\n').map(line => line.split('\t')));
    assert.deepEqual(keys.map(key => Number(fields.get(`${phase}_${key}`))), values);
    for (const key of ['FREE_TREE_RESERVE_RETURN_NODE_WRITES',
      'FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES'])
      assert.equal(fields.get(`${phase}_${key}`), '0');
  }
}
assert.equal(json.agreement.bfs.nonTimeOperationalCounterFieldCount, 1320);
assert.equal(json.agreement.bfs.nonTimeMetadataFieldCount, 6);
assert.equal(json.agreement.pfs3.nonTimeOperationalCounterFieldCount, 0);
console.log('Primary six-phase table and metadata/counter distinction: PASS');

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'bfs-free-tree-audit-'));
try {
  for (const order of Object.values(json.artifacts)) for (const input of Object.values(order)) {
    const target = path.join(temporary, input.path);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(repository, input.path), target, fs.constants.COPYFILE_EXCL);
  }
  assert(extract(temporary).equals(expected), 'isolated raw-input replay failed');
  console.log('Isolated copy of the four pinned raw inputs: PASS');
  const input = json.artifacts['bfs-first'].bfs.path;
  const mutated = path.join(temporary, input);
  const bytes = fs.readFileSync(mutated);
  bytes[0] ^= 1;
  fs.writeFileSync(mutated, bytes);
  const failure = spawnSync(process.execPath, [script, temporary], {encoding: 'utf8'});
  assert.equal(failure.status, 1);
  assert.match(failure.stderr, /SHA-256 changed/);
  assert.equal(failure.stdout, '');
  console.log('Owned temporary raw-byte mutation rejects before JSON emission: PASS (expected exit 1)');
} finally {
  // Remove only the fresh, uniquely owned copy, never the recorded evidence.
  fs.rmSync(temporary, {recursive: true});
}
console.log('Audit verification completed; no new performance or logical qualification claimed');
