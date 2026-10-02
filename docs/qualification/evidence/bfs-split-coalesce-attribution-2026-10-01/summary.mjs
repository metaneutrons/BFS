import fs from 'node:fs';
import assert from 'node:assert/strict';

const prefix = process.argv[2] || 'build/split-coalesce-attribution';
const inputs = ['bfs-first', 'pfs3-first'].map(order => ({order,
  parsed: JSON.parse(fs.readFileSync(`${prefix}-probe-${order}-consumed.json`, 'utf8'))}));
assert.deepEqual(inputs[0].parsed.path_matrix_by_phase,
  inputs[1].parsed.path_matrix_by_phase);
const phases = inputs[0].parsed.phases;
const integer = value => {
  const n = BigInt(value);
  assert(n >= 0n && n <= BigInt(Number.MAX_SAFE_INTEGER));
  return Number(n);
};
const counters = ['NODE_WRITES', 'FREE_TREE_NODE_WRITES', 'DIR_TREE_NODE_WRITES',
  'INODE_TREE_NODE_WRITES', 'OTHER_TREE_NODE_WRITES', 'BIO_READS', 'BIO_WRITES',
  'NODE_CRC_READ_CALLS', 'NODE_CRC_WRITE_CALLS', 'FREESPACE_ALLOCS'];
const pairs = inputs.map(({order, parsed}) => ({order,
  raw_sha256: parsed.input_sha256,
  schema_version: parsed.schema_version,
  phases: phases.map(phase => {
    const totals = parsed.path_totals_by_phase[phase];
    const nonzero = [];
    for (const [family, kinds] of Object.entries(parsed.path_matrix_by_phase[phase]))
      for (const [kind, roles] of Object.entries(kinds))
        for (const [role, shapes] of Object.entries(roles))
          for (const [shape, events] of Object.entries(shapes))
            if (Object.values(events).some(value => BigInt(value) !== 0n))
              nonzero.push({family, kind, role, shape,
                events: Object.fromEntries(Object.entries(events).map(([key, value]) =>
                  [key, integer(value)]))});
    return {phase, nonzero,
      split: Object.fromEntries(Object.entries(totals.split.by_event).map(([key, value]) =>
        [key, integer(value)])),
      coalesce: Object.fromEntries(Object.entries(totals.coalesce.by_event).map(([key, value]) =>
        [key, integer(value)])),
      retained_counters: Object.fromEntries(counters.map(key =>
        [key, integer(parsed.bfs_fields[`${phase}_${key}`])]))};
  })}));
const create = pairs[0].phases[0];
assert.equal(create.phase, 'SMALL_CREATE_40');
assert.equal(create.split.ATTEMPT, 4);
assert.equal(create.split.INITIAL_WRITE, 4);
assert.equal(create.split.RIGHT_SELECT, 2);
assert.equal(create.split.RIGHT_READ, 2);
assert.equal(create.split.RIGHT_WRITE, 2);
for (const pair of pairs) for (const phase of pair.phases) {
  assert.equal(phase.coalesce.ATTEMPT, 0);
  assert.equal(phase.coalesce.COMPLETE, 0);
  if (phase.phase !== 'SMALL_CREATE_40') assert.equal(phase.split.ATTEMPT, 0);
}
console.log(JSON.stringify({pairs, matrices_equal: true,
  upper_bounds: {
    create_removable_split_node_read_calls: 2,
    create_removable_split_node_write_calls: 2,
    create_removable_split_write_crc_calls: 2,
    create_total_node_writes: create.retained_counters.NODE_WRITES,
    create_directory_node_writes: create.retained_counters.DIR_TREE_NODE_WRITES,
    create_split_write_bound_percent_of_all_node_writes:
      200 / create.retained_counters.NODE_WRITES,
    coalesce_shape_eligible_hits_all_phases: 0,
  },
  conclusion: 'Neither measured path provides a substantial work bound for the six checked workloads. No optimization is accepted or implemented.',
  limits: 'Call-count upper bounds, not elapsed savings or physical I/O. Split buffer reuse needs explicit validation/durability design. Diagnostic times are excluded from <=5x acceptance; retained controls are not contemporaneous normal timing controls.'
}, null, 2));
