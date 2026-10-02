// Evidence packaging only; no benchmark, source, fixture or timing changes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';

const archive = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(archive, '../../../..');
const checkpoint = '3cbe7f1fc6dac6516cd8e6876dce75470893ee45';
const priorInventory = 'docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01/global-assets-review.json';
const indexFile = path.join(archive, 'formatter-packaging.json');
const mode = process.argv[2];
assert([undefined, '--package'].includes(mode));
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const read = name => fs.readFileSync(path.join(repository, name));
function safe(name) {
  assert.match(name, /^[A-Za-z0-9_./-]+$/);
  assert(!path.isAbsolute(name));
  assert(!name.split('/').some(part => part === '..' || part === ''));
  return name;
}
function original(name) {
  return execFileSync('git', ['show', `${checkpoint}:${safe(name)}`],
    {cwd: repository, maxBuffer: 8 * 1024 * 1024});
}
function asciiView(bytes) {
  assert(bytes.includes(0x9b));
  assert([...bytes].filter(value => value >= 128).every(value => value === 0x9b));
  return Buffer.from(bytes.toString('latin1').replaceAll('\u009b', '\u001b['), 'ascii');
}
function encode(bytes) { return Buffer.from(`${bytes.toString('base64')}\n`, 'ascii'); }
function decode(bytes) {
  const text = bytes.toString('ascii');
  assert(Buffer.from(text, 'ascii').equals(bytes));
  assert.match(text, /^[A-Za-z0-9+/=]+\n$/);
  const raw = Buffer.from(text.trimEnd(), 'base64');
  assert(encode(raw).equals(bytes), 'noncanonical base64');
  return raw;
}
function manifestEntries(bytes, manifest) {
  const text = bytes.toString('utf8');
  assert(Buffer.from(text).equals(bytes) && text.endsWith('\n'));
  const entries = text.trimEnd().split('\n').map(line => {
    const match = /^([a-f0-9]{64})  (.+)$/.exec(line);
    assert(match, `invalid manifest line ${manifest}`);
    const name = safe(match[2]);
    const resolved = name.startsWith('docs/qualification/evidence/') ? name :
      path.posix.join(path.posix.dirname(manifest), name);
    return {digest: match[1], name, resolved};
  });
  assert.equal(new Set(entries.map(entry => entry.resolved)).size, entries.length);
  return entries;
}
const priorBytes = read(priorInventory);
assert(priorBytes.equals(original(priorInventory)));
const prior = JSON.parse(priorBytes);
assert.equal(prior.existing_formatter_records, 68);
const names = prior.files.map(entry => safe(entry.name)).sort();
assert.equal(new Set(names).size, 68);
assert(names.every(name => /^docs\/qualification\/evidence\/bfs-[A-Za-z0-9-]+\/.*\/format-pfs3\.txt$/.test(name)));
const manifestNames = [...new Set(names.map(name => `${name.split('/').slice(0, 4).join('/')}/SHA256SUMS`))].sort();
assert.equal(manifestNames.length, 9);

if (mode === '--package') {
  assert(!fs.existsSync(indexFile), 'packaging already exists');
  assert.equal(execFileSync('git', ['rev-parse', 'HEAD'], {cwd: repository, encoding: 'utf8'}).trim(), checkpoint);
  assert.equal(execFileSync('git', ['diff', checkpoint, '--name-only'],
    {cwd: repository, encoding: 'utf8'}), '', 'tracked inputs must be clean before packaging');
  const files = names.map(name => {
    const bytes = read(name);
    assert(bytes.equals(original(name)), `changed raw input ${name}`);
    const view = asciiView(bytes), encoded = encode(bytes);
    assert(!fs.existsSync(path.join(repository, `${name}.b64`)));
    return {name, original_sha256: sha(bytes), ascii_sha256: sha(view),
      encoded_sha256: sha(encoded), original_bytes: bytes.length};
  });
  const manifests = manifestNames.map(name => {
    const bytes = read(name);
    assert(bytes.equals(original(name)), `changed manifest ${name}`);
    const entries = manifestEntries(bytes, name), additions = [];
    for (const file of files.filter(file => file.name.startsWith(`${path.posix.dirname(name)}/`))) {
      const entry = entries.find(entry => entry.resolved === file.name);
      assert(entry && entry.digest === file.original_sha256, `original digest ${file.name}`);
      entry.digest = file.ascii_sha256;
      additions.push({name: `${entry.name}.b64`, digest: file.encoded_sha256});
    }
    const updated = Buffer.from([...entries, ...additions].sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0)
      .map(entry => `${entry.digest}  ${entry.name}\n`).join(''));
    const saved = `original-manifests/${name.split('/')[3]}.sha256.b64`;
    assert(!fs.existsSync(path.join(archive, saved)));
    return {name, original_sha256: sha(bytes), updated_sha256: sha(updated),
      saved, encoded_sha256: sha(encode(bytes)), updated};
  });
  // All preconditions are checked before any existing evidence is changed.
  fs.mkdirSync(path.join(archive, 'original-manifests'), {recursive: false});
  for (const file of files) {
    const bytes = read(file.name);
    fs.writeFileSync(path.join(repository, `${file.name}.b64`), encode(bytes), {flag: 'wx'});
    assert(decode(read(`${file.name}.b64`)).equals(bytes));
    fs.writeFileSync(path.join(repository, file.name), asciiView(bytes));
  }
  for (const manifest of manifests) {
    fs.writeFileSync(path.join(archive, manifest.saved), encode(read(manifest.name)), {flag: 'wx'});
    fs.writeFileSync(path.join(repository, manifest.name), manifest.updated);
    delete manifest.updated;
  }
  fs.writeFileSync(indexFile, `${JSON.stringify({checkpoint, prior_inventory: priorInventory,
    prior_inventory_sha256: sha(priorBytes), view_transform: 'C1 CSI 0x9b to ASCII ESC [',
    files, manifests}, null, 2)}\n`, {flag: 'wx'});
  console.log(`Packaged ${files.length} exact raw formatter records; updated ${manifests.length} manifests`);
}

const index = JSON.parse(fs.readFileSync(indexFile, 'utf8'));
assert.equal(index.checkpoint, checkpoint);
assert.equal(index.prior_inventory, priorInventory);
assert.equal(index.prior_inventory_sha256, sha(priorBytes));
assert.deepEqual(index.files.map(file => file.name), names);
assert.deepEqual(index.manifests.map(manifest => manifest.name), manifestNames);
for (const file of index.files) {
  const encoded = read(`${file.name}.b64`), raw = decode(encoded), view = read(file.name);
  assert.equal(sha(encoded), file.encoded_sha256);
  assert.equal(sha(raw), file.original_sha256);
  assert.equal(raw.length, file.original_bytes);
  assert.equal(sha(view), file.ascii_sha256);
  assert(view.equals(asciiView(raw)));
  assert(raw.equals(original(file.name)), `checkpoint byte identity ${file.name}`);
}
console.log('Original formatter bytes and ASCII views: PASS (68 records, exact checkpoint identities)');
for (const manifest of index.manifests) {
  safe(manifest.name); safe(manifest.saved);
  const encoded = fs.readFileSync(path.join(archive, manifest.saved)), raw = decode(encoded);
  assert.equal(sha(encoded), manifest.encoded_sha256);
  assert.equal(sha(raw), manifest.original_sha256);
  assert(raw.equals(original(manifest.name)));
  const updated = read(manifest.name);
  assert.equal(sha(updated), manifest.updated_sha256);
  const originalEntries = manifestEntries(raw, manifest.name);
  const currentEntries = manifestEntries(updated, manifest.name);
  const additions = index.files.filter(file => file.name.startsWith(`${path.posix.dirname(manifest.name)}/`))
    .map(file => `${file.name}.b64`);
  assert.deepEqual(currentEntries.map(entry => entry.resolved).sort(),
    [...originalEntries.map(entry => entry.resolved), ...additions].sort(), 'manifest membership changed');
  for (const entry of currentEntries)
    assert.equal(sha(read(entry.resolved)), entry.digest, `current manifest hash ${entry.resolved}`);
}
console.log(`Original manifests retained and current manifests verified: PASS (${index.manifests.length} archives)`);
const changed = execFileSync('git', ['diff', checkpoint, '--name-only', '--', '*.tsv',
  'src', 'include', 'tests', 'tools', 'emulator-test', 'Makefile'], {cwd: repository, encoding: 'utf8'});
assert.equal(changed, '', 'packaging must not change TSVs, fixtures or production/build tooling');
const changedAll = execFileSync('git', ['diff', checkpoint, '--name-only', '-z'],
  {cwd: repository, encoding: 'utf8'}).split('\0').filter(Boolean);
const allowed = new Set(index.files.flatMap(file => [file.name, `${file.name}.b64`])
  .concat(index.manifests.map(manifest => manifest.name)));
const packagingPrefix = `${path.relative(repository, archive).split(path.sep).join('/')}/`;
assert(changedAll.every(name => allowed.has(name) || name.startsWith(packagingPrefix)),
  'tracked packaging diff contains unexpected paths, including evidence fixtures');
console.log('Performance TSVs, fixture sources and production/build tooling unchanged: PASS');
