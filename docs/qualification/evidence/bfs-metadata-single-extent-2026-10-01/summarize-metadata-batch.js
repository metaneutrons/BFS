const fs = require('node:fs');
const assert = require('node:assert/strict');
const metrics = ['SMALL_CREATE_40_US', 'LOOKUP_400_US', 'SMALL_READ_40_US', 'SEQ_WRITE_8M_US', 'SEQ_READ_8M_US', 'SMALL_DELETE_40_US'];
const pairs = [
  ['baseline-bfs-first', 'candidate-bfs-first'],
  ['baseline-pfs3-first', 'candidate-pfs3-first'],
  ['repeat-baseline-pfs3-first', 'repeat-candidate-pfs3-first'],
  ['repeat-baseline-bfs-first', 'repeat-candidate-bfs-first'],
];
function parse(input, drive) {
  const rows = input.trim().split(/\r?\n/).map(line => line.split('\t'));
  assert.equal(rows.length, 9);
  assert.deepEqual(rows.map(row => row[0]), ['FS_COMPARE_BENCH', 'DRIVE', ...metrics, 'PASS']);
  for (const row of rows) assert.equal(row.length, 2);
  assert.equal(rows[0][1], '1');
  assert.equal(rows[1][1], drive);
  assert.equal(rows[8][1], '1');
  const result = {};
  for (let i = 0; i < metrics.length; ++i) {
    const value = rows[i + 2][1];
    assert.match(value, /^\d+$/);
    assert(Number.isSafeInteger(Number(value)) && Number(value) > 0);
    result[metrics[i]] = Number(value);
  }
  return result;
}
if (process.argv[2] === '--self-test') {
  const good = ['FS_COMPARE_BENCH\t1', 'DRIVE\tDH1:', ...metrics.map(key => `${key}\t10`), 'PASS\t1'].join('\n');
  assert.equal(parse(good, 'DH1:').SEQ_WRITE_8M_US, 10);
  for (const bad of [good.replace('PASS\t1', 'PASS\t0'), good.replace('DH1:', 'DH2:'), good.replace('SEQ_WRITE_8M_US\t10', 'SEQ_WRITE_8M_US\t-1'), `${good}\nPASS\t1`]) {
    assert.throws(() => parse(bad, 'DH1:'));
  }
  assert.equal(20 / 10, 2);
  assert.equal(60 / 10, 6);
  console.log('Parser positive and four counter-probes passed.');
  process.exit(0);
}
const prefix = process.argv[2] || 'metadata-batch';
assert.match(prefix, /^[a-z0-9-]+$/);
const data = {};
for (const suffix of pairs.flat()) {
  data[suffix] = {};
  for (const filesystem of ['bfs', 'pfs3']) {
    const filename = `build/benchmark/${prefix}-normal-${suffix}/system/Results/${filesystem}.tsv`;
    data[suffix][filesystem] = parse(fs.readFileSync(filename, 'utf8'), filesystem === 'bfs' ? 'DH1:' : 'DH2:');
  }
}
const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
const summary = metrics.map(metric => {
  const baselineBfs = pairs.map(([baseline]) => data[baseline].bfs[metric]);
  const candidateBfs = pairs.map(([, candidate]) => data[candidate].bfs[metric]);
  const baselinePfs3 = pairs.map(([baseline]) => data[baseline].pfs3[metric]);
  const candidatePfs3 = pairs.map(([, candidate]) => data[candidate].pfs3[metric]);
  const ratios = candidateBfs.map((value, i) => value / candidatePfs3[i]);
  return {
    metric, baselineBfs, candidateBfs, baselinePfs3, candidatePfs3,
    baselineBfsMean: mean(baselineBfs), candidateBfsMean: mean(candidateBfs),
    baselinePfs3Mean: mean(baselinePfs3), candidatePfs3Mean: mean(candidatePfs3),
    bfsMeanChangePercent: 100 * (mean(candidateBfs) / mean(baselineBfs) - 1),
    candidateRatioOfMeans: mean(candidateBfs) / mean(candidatePfs3),
    candidateSameRunRatios: ratios,
    candidateThresholdFailures: ratios.filter(value => value > 5).length,
    matchedBfsChangesPercent: candidateBfs.map((value, i) => 100 * (value / baselineBfs[i] - 1)),
    matchedBfsAdverse: candidateBfs.filter((value, i) => value > baselineBfs[i]).length,
    matchedPfs3Adverse: candidatePfs3.filter((value, i) => value > baselinePfs3[i]).length,
  };
});
console.log(JSON.stringify({ prefix, pairs, data, summary }, null, 2));
