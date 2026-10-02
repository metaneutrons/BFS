import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const directory = path.dirname(fileURLToPath(import.meta.url));
const records = JSON.parse(fs.readFileSync(path.join(directory, 'source-identities.json'), 'utf8'));
assert.deepEqual(records.map(record => record.name), ['baseline.c', 'candidate.c', 'negative-core.c', 'candidate.patch']);
for (const record of records) {
  assert.equal(record.encoded_file, `${record.name}.b64`);
  const bytes = Buffer.from(fs.readFileSync(path.join(directory, record.encoded_file), 'utf8'), 'base64');
  assert.equal(bytes.length, record.bytes);
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), record.sha256);
}
assert.equal(records[0].sha256, '5095d6dd4e9e2984eb88a68779d1f5ea3fbb7e39c3c10a22f32de0479d2cb7da');
assert.equal(records[1].sha256, '1069dea2be34004de74a58c8c56ebccbd75ff3ea1bf2f17f75892172aa0bcdc9');
console.log('PASS: decoded exact source/patch bytes match frozen identities');
