// Lossless textual packaging: preserve original trailing-space source/patch bytes.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
const directory = 'docs/qualification/evidence/bfs-btree-scratch-lifetime-2026-10-01';
const names = ['baseline.c', 'candidate.c', 'negative-core.c', 'candidate.patch'];
const expected = new Map([
  ['baseline.c', '5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da'],
  ['candidate.c', '1069dea2be34004de74a58c8c56ebccbd75ff3ea1bf2f17f75892172aa0bcdc9'],
]);
const records = names.map(name => {
  const bytes = fs.readFileSync(`${directory}/${name}`);
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  if (expected.has(name)) assert.equal(sha256, expected.get(name));
  const encoded = bytes.toString('base64').match(/.{1,76}/g).join('\n') + '\n';
  assert.deepEqual(Buffer.from(encoded, 'base64'), bytes);
  fs.writeFileSync(`${directory}/${name}.b64`, encoded);
  return {name, encoded_file: `${name}.b64`, bytes: bytes.length, sha256};
});
fs.writeFileSync(`${directory}/source-identities.json`, JSON.stringify(records, null, 2) + '\n');
console.log('PASS: exact three C source snapshots and full-context patch losslessly base64-encoded');
