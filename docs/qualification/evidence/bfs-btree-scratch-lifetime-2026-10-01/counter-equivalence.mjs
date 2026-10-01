// Input verification PASS alone does not imply diagnostic counter equivalence.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const parsed = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
assert.equal(parsed.strict_gate, 'PASS');
assert.deepEqual(parsed.pairs.map(pair => pair.order), ['bfs-first', 'pfs3-first']);
for (const pair of parsed.pairs) {
  assert.equal(pair.structural_differences.length, 0, `${pair.order}: structural differences`);
  assert.equal(pair.all_counter_differences.length, 0, `${pair.order}: non-time differences`);
}
console.log('PASS: structural and all non-time counters agree in both diagnostic orders');
