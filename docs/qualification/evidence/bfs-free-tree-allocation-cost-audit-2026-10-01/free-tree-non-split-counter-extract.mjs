#!/usr/bin/env node
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const ROOT = path.resolve(process.argv[2] || process.cwd());
const OUTPUT_ARGUMENT = process.argv[3];
const INPUT_ROOT = 'docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01';
const PHASES = [
  'SMALL_CREATE_40',
  'LOOKUP_400',
  'SMALL_READ_40',
  'SEQ_WRITE_8M',
  'SEQ_READ_8M',
  'SMALL_DELETE_40',
];
const FILES = {
  'bfs-first': {
    bfs: {
      path: INPUT_ROOT + '/split-coalesce-attribution-probe-bfs-first/system/Results/bfs.deep-compare.tsv',
      sha256: 'f237d5c68deae7537870227d3c9ee1d12330bd14fb5e9a9b07c804440fc2739f',
      drive: 'DH1:',
    },
    pfs3: {
      path: INPUT_ROOT + '/split-coalesce-attribution-probe-bfs-first/system/Results/pfs3.deep-compare.tsv',
      sha256: '64027726481b46aa84a06228f4705645634622d5486fd1170b6441803b4b9ec2',
      drive: 'DH2:',
    },
  },
  'pfs3-first': {
    bfs: {
      path: INPUT_ROOT + '/split-coalesce-attribution-probe-pfs3-first/system/Results/bfs.deep-compare.tsv',
      sha256: '90749bc2003d55fcdee538c6f5977ad09ce2c4d32e190dc47999e73cefbf439b',
      drive: 'DH1:',
    },
    pfs3: {
      path: INPUT_ROOT + '/split-coalesce-attribution-probe-pfs3-first/system/Results/pfs3.deep-compare.tsv',
      sha256: 'ceec6ce16106508320b74e51c83bf6ea7f56c96ec18d125de520119df06a002e',
      drive: 'DH2:',
    },
  },
};
const CPU_SCOPES = [
  'PACKET', 'PACKET_OPEN', 'PACKET_READ', 'PACKET_WRITE', 'PACKET_END',
  'PACKET_DELETE', 'PACKET_FLUSH', 'PACKET_OTHER', 'CORE_CREATE',
  'CORE_DELETE', 'CORE_FILE_WRITE', 'CORE_SYNC', 'IFACE_FREE', 'SEAL_COMMIT',
];
const ACTIVE_MUTATION_PHASES = ['SMALL_CREATE_40', 'SEQ_WRITE_8M', 'SMALL_DELETE_40'];
const FREE_TREE_BUCKETS = [
  'FREE_TREE_ALLOCATION_BODY_NODE_WRITES',
  'FREE_TREE_RESERVE_REFILL_NODE_WRITES',
  'FREE_TREE_RESERVE_RETURN_NODE_WRITES',
  'FREE_TREE_POST_PUBLISH_PENDING_RECLAIM_NODE_WRITES',
  'FREE_TREE_OTHER_NODE_WRITES',
];
const RESERVE_COUNTERS = [
  'FREE_TREE_RESERVE_RETURN_CALLS',
  'FREE_TREE_RESERVE_RETURN_RUNS',
  'FREE_TREE_RESERVE_RETURN_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_RUNS_1_BLOCK',
  'FREE_TREE_RESERVE_RETURN_RUNS_2_3_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_RUNS_4_7_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_RUNS_8_PLUS_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_MAX_RUN_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_MAX_NODE_WRITES_PER_RUN',
  'FREE_TREE_RESERVE_RETURN_RUN_NODE_WRITES',
  'FREE_TREE_RESERVE_RETURN_BATCH_CALLS',
  'FREE_TREE_RESERVE_RETURN_BATCH_BLOCKS',
  'FREE_TREE_RESERVE_RETURN_BATCH_NODE_WRITES',
  'FREE_TREE_RESERVE_RETURN_SKIP_SHAPE',
  'FREE_TREE_RESERVE_RETURN_SKIP_SMALL',
  'FREE_TREE_RESERVE_RETURN_SKIP_EMERGENCY',
  'FREE_TREE_RESERVE_RETURN_SKIP_CAPACITY',
];
const EXPECTED_FREE_TREE_WRITES = {
  SMALL_CREATE_40: 353,
  LOOKUP_400: 0,
  SMALL_READ_40: 0,
  SEQ_WRITE_8M: 142,
  SEQ_READ_8M: 0,
  SMALL_DELETE_40: 197,
};

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function digest(content) {
  return createHash('sha256').update(content).digest('hex');
}

function parseTsv(content, label) {
  const fields = new Map();
  for (const line of content.trimEnd().split(/\r?\n/)) {
    const separator = line.indexOf('\t');
    assert(separator > 0, label + ': malformed TSV row: ' + line);
    const key = line.slice(0, separator);
    const text = line.slice(separator + 1);
    assert(!fields.has(key), label + ': duplicate field ' + key);
    const value = /^-?\d+$/.test(text) ? Number(text) : text;
    if (typeof value === 'number')
      assert(Number.isSafeInteger(value), label + ': unsafe integer for ' + key);
    fields.set(key, value);
  }
  return fields;
}

function isTimedField(key) {
  return key.endsWith('_US') || key.endsWith('_TICKS');
}

function rawPhase(fields, phase) {
  const prefix = phase + '_';
  const counters = {};
  const tickCounters = {};
  let elapsedUs;
  for (const [key, value] of fields) {
    if (!key.startsWith(prefix)) continue;
    const suffix = key.slice(prefix.length);
    if (suffix === 'US') {
      elapsedUs = value;
    } else if (suffix.endsWith('_TICKS')) {
      tickCounters[suffix] = value;
    } else {
      counters[suffix] = value;
    }
  }
  assert(Number.isSafeInteger(elapsedUs), phase + ': missing elapsed US field');
  return { elapsedUs, counters, tickCounters };
}

function assertCounter(phase, counters, suffix) {
  assert(Number.isSafeInteger(counters[suffix]), phase + ': missing counter ' + suffix);
}

function sumField(phases, suffix, selectedPhases) {
  return selectedPhases.reduce((sum, phase) => sum + phases[phase].counters[suffix], 0);
}

const runs = {};
const artifacts = {};
for (const [order, files] of Object.entries(FILES)) {
  runs[order] = {};
  artifacts[order] = {};
  for (const [filesystem, spec] of Object.entries(files)) {
    const absolutePath = path.join(ROOT, spec.path);
    const content = readFileSync(absolutePath);
    const sha256 = digest(content);
    assert(sha256 === spec.sha256,
      order + '/' + filesystem + ': SHA-256 changed; expected ' + spec.sha256 + ', got ' + sha256);
    const fields = parseTsv(content.toString('utf8'), order + '/' + filesystem);
    assert(fields.get('FS_DEEP_COMPARE') === 12,
      order + '/' + filesystem + ': expected schema 12');
    assert(fields.get('DRIVE') === spec.drive,
      order + '/' + filesystem + ': unexpected drive ' + fields.get('DRIVE'));
    assert(fields.get('PASS') === 1,
      order + '/' + filesystem + ': PASS must equal 1');
    const phases = {};
    for (const phase of PHASES) {
      phases[phase] = rawPhase(fields, phase);
      if (filesystem === 'bfs') {
        for (const suffix of [
          'BIO_READS', 'BIO_WRITES', 'BIO_UPDATES', 'DATA_READS', 'DATA_WRITES',
          'NODE_WRITES', 'TXN_COMMITS', 'FREESPACE_ALLOCS', 'EXTENT_MAPS',
          'FREE_TREE_NODE_WRITES', 'DIR_TREE_NODE_WRITES', 'INODE_TREE_NODE_WRITES',
          'REFCOUNT_TREE_NODE_WRITES', 'OTHER_TREE_NODE_WRITES',
          ...FREE_TREE_BUCKETS,
          ...RESERVE_COUNTERS, 'POST_PUBLISH_RECLAIM_PASSES',
          'MAX_POST_PUBLISH_RECLAIM_PASSES_PER_COMMIT', 'SUPERBLOCK_PUBLICATIONS',
          'SEALED_COMMITS', 'SEALED_METADATA_FENCES',
          'BTREE_MALLOC_CALLS', 'BTREE_MALLOC_SAMPLES',
          'BTREE_FREE_CALLS', 'BTREE_FREE_SAMPLES',
          'IFACE_ALLOC_CALLS', 'IFACE_ALLOC_SAMPLES',
          'FREESPACE_ALLOC_SAMPLES', 'NODE_CRC_READ_CALLS', 'NODE_CRC_WRITE_CALLS',
        ]) assertCounter(phase, phases[phase].counters, suffix);
        for (const scope of CPU_SCOPES) {
          assertCounter(phase, phases[phase].counters, scope + '_CALLS');
          assertCounter(phase, phases[phase].counters, scope + '_SAMPLES');
          assertCounter(phase, phases[phase].tickCounters, scope + '_SAMPLE_TICKS');
          assert(phases[phase].counters[scope + '_CALLS'] ===
            phases[phase].counters[scope + '_SAMPLES'],
          phase + ': CPU scope calls and samples differ for ' + scope);
        }
        for (const [calls, samples] of [
          ['BTREE_MALLOC_CALLS', 'BTREE_MALLOC_SAMPLES'],
          ['BTREE_FREE_CALLS', 'BTREE_FREE_SAMPLES'],
          ['IFACE_ALLOC_CALLS', 'IFACE_ALLOC_SAMPLES'],
          ['FREESPACE_ALLOCS', 'FREESPACE_ALLOC_SAMPLES'],
        ]) assert(phases[phase].counters[calls] === phases[phase].counters[samples],
          phase + ': calls and samples differ for ' + calls);
      }
    }
    const metadata = {};
    for (const [key, value] of fields) {
      if (PHASES.some((phase) => key.startsWith(phase + '_'))) continue;
      metadata[key] = value;
    }
    runs[order][filesystem] = { fields, phases, metadata };
    artifacts[order][filesystem] = { path: spec.path, sha256, bytes: content.length };
  }
}

const agreement = {};
for (const filesystem of ['bfs', 'pfs3']) {
  const left = runs['bfs-first'][filesystem].fields;
  const right = runs['pfs3-first'][filesystem].fields;
  const leftNonTime = [...left.entries()].filter(([key]) => !isTimedField(key)).sort(([a], [b]) => a.localeCompare(b));
  const rightNonTime = [...right.entries()].filter(([key]) => !isTimedField(key)).sort(([a], [b]) => a.localeCompare(b));
  assert(JSON.stringify(leftNonTime) === JSON.stringify(rightNonTime),
    filesystem + ': non-time fields differ between diagnostic orders');
  const metadataFields = leftNonTime
    .filter(([key]) => !PHASES.some((phase) => key.startsWith(phase + '_')))
    .map(([key]) => key);
  agreement[filesystem] = {
    nonTimeFieldsEqual: true,
    nonTimeFieldsCompared: leftNonTime.length,
    nonTimeMetadataFieldCount: metadataFields.length,
    nonTimeMetadataFields: metadataFields,
    nonTimeOperationalCounterFieldCount: leftNonTime.length - metadataFields.length,
    excludedElapsedOrTickFields: [...left.keys()].filter(isTimedField).length,
  };
}

const phases = {};
for (const phase of PHASES) {
  const bfsFirst = runs['bfs-first'].bfs.phases[phase];
  const pfs3First = runs['pfs3-first'].bfs.phases[phase];
  const pfs3BfsFirst = runs['bfs-first'].pfs3.phases[phase];
  const pfs3Pfs3First = runs['pfs3-first'].pfs3.phases[phase];
  const freeTreeBucketSum = FREE_TREE_BUCKETS.reduce((sum, key) => sum + bfsFirst.counters[key], 0);
  assert(freeTreeBucketSum === bfsFirst.counters.FREE_TREE_NODE_WRITES,
    phase + ': five FreeTree buckets do not reconcile to FREE_TREE_NODE_WRITES');
  const treeNodeWriteSum = bfsFirst.counters.FREE_TREE_NODE_WRITES +
    bfsFirst.counters.DIR_TREE_NODE_WRITES + bfsFirst.counters.INODE_TREE_NODE_WRITES +
    bfsFirst.counters.REFCOUNT_TREE_NODE_WRITES + bfsFirst.counters.OTHER_TREE_NODE_WRITES;
  assert(treeNodeWriteSum === bfsFirst.counters.NODE_WRITES,
    phase + ': per-tree node writes do not reconcile to NODE_WRITES');
  assert(JSON.stringify(bfsFirst.counters) === JSON.stringify(pfs3First.counters),
    phase + ': BFS non-time phase counters differ by diagnostic order');
  phases[phase] = {
    elapsedUsByOrder: {
      bfsFirst: { bfs: bfsFirst.elapsedUs, pfs3: pfs3BfsFirst.elapsedUs },
      pfs3First: { bfs: pfs3First.elapsedUs, pfs3: pfs3Pfs3First.elapsedUs },
    },
    probeTicksByOrder: {
      bfsFirst: bfsFirst.tickCounters,
      pfs3First: pfs3First.tickCounters,
    },
    counters: bfsFirst.counters,
    checks: {
      freeTreeBucketsSum: freeTreeBucketSum,
      treeNodeWriteCategorySum: treeNodeWriteSum,
      countersEqualAcrossOrders: true,
    },
  };
}

for (const phase of PHASES) {
  assert(phases[phase].counters.FREE_TREE_NODE_WRITES === EXPECTED_FREE_TREE_WRITES[phase],
    phase + ': unexpected FREE_TREE_NODE_WRITES');
}

const activeTotals = {};
for (const key of [
  'FREE_TREE_NODE_WRITES', ...FREE_TREE_BUCKETS, 'NODE_WRITES', 'BIO_READS',
  'BIO_WRITES', 'BIO_UPDATES', 'DATA_READS', 'DATA_WRITES', 'TXN_COMMITS',
  'FREESPACE_ALLOCS', 'EXTENT_MAPS', 'IFACE_ALLOC_CALLS', 'IFACE_FREE_CALLS',
  'SEALED_COMMITS', 'SEALED_METADATA_FENCES', 'SUPERBLOCK_PUBLICATIONS',
  'FREE_TREE_RESERVE_RETURN_CALLS', 'FREE_TREE_RESERVE_RETURN_RUNS',
  'POST_PUBLISH_RECLAIM_PASSES',
]) {
  activeTotals[key] = sumField(phases, key, ACTIVE_MUTATION_PHASES);
}
assert(activeTotals.FREE_TREE_NODE_WRITES === 692, 'active FreeTree total must equal 692');
assert(activeTotals.FREE_TREE_ALLOCATION_BODY_NODE_WRITES === 486, 'allocation-body writes must equal 486');
assert(activeTotals.FREE_TREE_RESERVE_REFILL_NODE_WRITES === 81, 'reserve-refill writes must equal 81');
assert(activeTotals.FREE_TREE_OTHER_NODE_WRITES === 125, 'other FreeTree writes must equal 125');

const eventCounters = [];
for (const phase of PHASES) {
  for (const [suffix, value] of Object.entries(phases[phase].counters)) {
    if (/^(SPLIT_|COALESCE_)/.test(suffix) && value !== 0)
      eventCounters.push({ phase, counter: suffix, value });
  }
}
const freeTreeSplitCoalesceFields = PHASES.flatMap((phase) =>
  Object.entries(phases[phase].counters)
    .filter(([suffix]) => /^(SPLIT_|COALESCE_)/.test(suffix) && suffix.includes('FREE_TREE'))
    .map(([suffix, value]) => ({ phase, counter: suffix, value })));
assert(freeTreeSplitCoalesceFields.every((entry) => entry.value === 0),
  'unexpected FreeTree split/coalescence activity');

const result = {
  extraction: 'free-tree-non-split-counter-extract-v1',
  source: 'raw schema-12 diagnostic TSVs only; no summary JSON input',
  schemaVersion: 12,
  phaseOrder: PHASES,
  artifacts,
  agreement,
  metadataByOrder: {
    bfsFirst: {
      bfs: runs['bfs-first'].bfs.metadata,
      pfs3: runs['bfs-first'].pfs3.metadata,
    },
    pfs3First: {
      bfs: runs['pfs3-first'].bfs.metadata,
      pfs3: runs['pfs3-first'].pfs3.metadata,
    },
  },
  phases,
  activeMutationPhaseTotals: activeTotals,
  splitCoalescence: {
    freeTreeFieldsAllZero: true,
    nonzeroRawFields: eventCounters,
  },
  interpretationBoundaries: [
    'US wall elapsed, EClock probe ticks, calls/samples, node-write counters, and BIO counters remain separate.',
    'CPU-scope, allocator-interface, B-tree heap, and freespace allocator tick totals may be nested; they are not disjoint elapsed-time components.',
    'No acceptance ratio or latency claim is derived from probe timings.',
  ],
};

const outputJson = JSON.stringify(result, null, 2) + '\n';
if (OUTPUT_ARGUMENT) {
  const outputPath = path.resolve(process.cwd(), OUTPUT_ARGUMENT);
  writeFileSync(outputPath, outputJson);
  process.stderr.write('wrote ' + path.relative(process.cwd(), outputPath) + '\n');
} else {
  process.stdout.write(outputJson);
}
process.stderr.write('SHA-256 verified for 4 raw files; non-time BFS fields: ' +
  agreement.bfs.nonTimeFieldsCompared + ' (' + agreement.bfs.nonTimeOperationalCounterFieldCount +
  ' phase counters, ' + agreement.bfs.nonTimeMetadataFieldCount + ' metadata), PFS3: ' +
  agreement.pfs3.nonTimeFieldsCompared + ' (' + agreement.pfs3.nonTimeMetadataFieldCount +
  ' metadata, ' + agreement.pfs3.nonTimeOperationalCounterFieldCount + ' counters)\n');
process.stderr.write('Active mutation FreeTree node writes: ' + activeTotals.FREE_TREE_NODE_WRITES +
  ' (allocation body ' + activeTotals.FREE_TREE_ALLOCATION_BODY_NODE_WRITES +
  ', reserve refill ' + activeTotals.FREE_TREE_RESERVE_REFILL_NODE_WRITES +
  ', other ' + activeTotals.FREE_TREE_OTHER_NODE_WRITES + ')\n');
