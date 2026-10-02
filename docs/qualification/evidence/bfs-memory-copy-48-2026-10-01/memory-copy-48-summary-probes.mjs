import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const source = 'build/memory-copy-48/run.TG9eTx/system/Results';
const original = fs.readFileSync(`${source}/memory-copy-compare.txt`, 'utf8');
const done = fs.readFileSync(`${source}/memory-copy-compare.done`, 'utf8');
const root = fs.mkdtempSync('build/memory-copy-48/summary-probes.');
const cases = [
  ['valid', text => text, done, true],
  ['duplicate-count', text => `${text}candidate_checked_calls=8908\n`, done, false],
  ['wrong-samples', text => text.replace('timing_samples=48', 'timing_samples=49'), done, false],
  ['wrong-calls', text => text.replace('candidate_timing_calls=567398', 'candidate_timing_calls=567399'), done, false],
  ['wrong-repeats', text => text.replace('timing_length=44 repeats=20000', 'timing_length=44 repeats=20001'), done, false],
  ['uint64-overflow', text => text.replace('baseline_ticks=5811,', 'baseline_ticks=18446744073709551616,'), done, false],
  ['missing-row', text => text.replace(/^timing_length=512 .*\n/m, ''), done, false],
  ['zero-batch', text => text.replace('baseline_ticks=5811,', 'baseline_ticks=0,'), done, false],
  ['duplicate-pass', text => `${text}MEMORY-COPY-COMPARE-PASS\n`, done, false],
  ['bad-done', text => text, 'MEMORY-COPY-COMPARE-FAIL\n', false],
  ['abi-failure', text => text.replace('candidate_abi_failures=0', 'candidate_abi_failures=4'), done, false],
  ['uint32-overflow', text => text.replace('clock_hz=709379', 'clock_hz=4294967296'), done, false],
];
for (const [name, transform, marker, expected] of cases) {
  const directory = path.join(root, name);
  fs.mkdirSync(`${directory}/system/Results`, {recursive:true});
  fs.writeFileSync(`${directory}/system/Results/memory-copy-compare.txt`, transform(original));
  fs.writeFileSync(`${directory}/system/Results/memory-copy-compare.done`, marker);
  const result = spawnSync(process.execPath, ['build/memory-copy-48-summary.mjs', directory], {encoding:'utf8'});
  assert.equal(result.status === 0, expected, `${name}: ${result.stderr}`);
  if (!expected) assert.match(result.stderr, /AssertionError/);
  console.log(`${name}: expected ${expected ? 'PASS' : 'REJECT'}, observed exit=${result.status}`);
}
console.log('Summary verifier: 1 positive +11 isolated counter-probes PASS');
