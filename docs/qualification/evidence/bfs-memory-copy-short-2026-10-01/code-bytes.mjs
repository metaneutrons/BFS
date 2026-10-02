import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
function decode(file) {
  const bytes = [];
  const symbols = new Map();
  for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
    const symbol = /^([0-9a-f]+) [0-9a-f]+ (_memcpy|_memset):$/.exec(line);
    if (symbol) symbols.set(symbol[2], parseInt(symbol[1], 16));
    if (!/^\s*[0-9a-f]+:/.test(line)) continue;
    const cells = line.split('\t');
    assert(cells.length >= 3);
    const words = cells[1].trim().split(/\s+/);
    assert(words.every(word => /^[0-9a-f]{4}$/.test(word)));
    bytes.push(...Buffer.from(words.join(''), 'hex'));
  }
  assert.equal(symbols.get('_memcpy'), 0);
  assert(symbols.has('_memset'));
  return {bytes: Buffer.from(bytes), symbols};
}
const old = decode('build/memory-copy-short-baseline-disassembly.s');
const candidate = decode('build/memory-copy-short-candidate-disassembly.s');
const oldSet = old.symbols.get('_memset');
const newSet = candidate.symbols.get('_memset');
const inserted = newSet - oldSet;
assert.equal(inserted, 56);
const oldCopy = old.bytes.subarray(0, oldSet);
const newLarge = candidate.bytes.subarray(inserted, newSet);
const oldMemset = old.bytes.subarray(oldSet);
const newMemset = candidate.bytes.subarray(newSet);
assert.deepEqual(newLarge, oldCopy);
assert.deepEqual(newMemset, oldMemset);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
console.log(JSON.stringify({gate: 'PASS', inserted_bytes: inserted,
  retained_copy_body_bytes: oldCopy.length, retained_copy_body_sha256: sha(oldCopy),
  unchanged_memset_bytes: oldMemset.length, unchanged_memset_sha256: sha(oldMemset),
  limitation: 'Decoded actual assembler opcodes prove retained body bytes, not equal runtime latency or complete target correctness.'}, null, 2));
