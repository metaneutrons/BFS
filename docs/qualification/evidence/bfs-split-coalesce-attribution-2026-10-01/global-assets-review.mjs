// Read-only attribution of the existing whole-repository asset-audit failure.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {execFileSync} from 'node:child_process';
const checkpoint = 'bdb3ae0e9dfb8482a0deb8564827bec5212ec804';
const lines = fs.readFileSync('build/split-coalesce-attribution-global-assets.log', 'utf8').split('\n');
const files = lines.filter(line => line.startsWith('ERROR: tracked binary artifact: ')).map(line => {
  const match = /^ERROR: tracked binary artifact: (.+) \(([^,]+), ([^)]+)\)$/.exec(line);
  assert(match);
  const [, name, mime, encoding] = match;
  assert(name.endsWith('/format-pfs3.txt'));
  assert(!name.includes('bfs-split-coalesce-attribution-2026-10-01/'));
  const bytes = fs.readFileSync(name);
  assert(bytes.equals(execFileSync('git', ['show', `${checkpoint}:${name}`], {maxBuffer: 8 * 1024 * 1024})));
  const c1 = [...new Set([...bytes].filter(value => value >= 128 && value <= 159))].sort();
  assert(c1.length > 0, `missing formatter C1 controls ${name}`);
  return {name, mime, encoding, c1_byte_values: c1, unchanged_from_checkpoint: true};
});
assert.equal(files.length, 68);
assert.equal(new Set(files.map(file => file.name)).size, 68);
console.log(JSON.stringify({checkpoint, whole_repository_audit_exit: 1,
  existing_formatter_records: files.length, all_bytes_unchanged: true,
  new_evidence_errors: 0, limitation: 'This attributes the existing failure; it does not fix it or make the whole-repository asset audit pass.', files}, null, 2));
