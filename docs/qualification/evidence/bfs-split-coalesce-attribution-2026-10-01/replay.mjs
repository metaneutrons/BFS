// Offline text-evidence replay; never starts an emulator or rebuilds a handler.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';

const root = path.dirname(fileURLToPath(import.meta.url));
const option = process.argv[2];
assert([undefined, '--allow-unmanifested', '--write-manifest'].includes(option));
const manifestName = 'SHA256SUMS';
const prefix = 'docs/qualification/evidence/bfs-split-coalesce-attribution-2026-10-01/';
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
function safeRelative(name) {
  assert.match(name, /^[A-Za-z0-9_./-]+$/);
  assert(!path.isAbsolute(name) && !name.split('/').some(part => part === '..' || part === '.'));
  assert(name.length > 0 && !name.endsWith('/'));
  return name;
}
function inventory(directory = root, relative = '') {
  return fs.readdirSync(directory).flatMap(name => {
    const entry = relative ? `${relative}/${name}` : name;
    safeRelative(entry);
    const status = fs.lstatSync(path.join(root, entry));
    assert(!status.isSymbolicLink(), `unexpected symlink ${entry}`);
    if (status.isDirectory()) return inventory(path.join(root, entry), entry);
    assert(status.isFile(), `unexpected object ${entry}`);
    return [entry];
  }).sort();
}
function records(text) {
  assert(text.endsWith('\n'));
  return text.trimEnd().split('\n').map(line => {
    const match = /^([a-f0-9]{64})  (.+)$/.exec(line);
    assert(match, `invalid identity record ${line}`);
    return {digest: match[1], name: safeRelative(match[2])};
  });
}
const files = inventory().filter(name => name !== manifestName);
if (option === '--write-manifest') {
  const manifest = files.map(name => `${hash(fs.readFileSync(path.join(root, name)))}  ${name}\n`).join('');
  fs.writeFileSync(path.join(root, manifestName), manifest, {flag: 'wx'});
  console.log(`Created exact inventory manifest: ${files.length} files`);
  process.exit(0);
}
if (option !== '--allow-unmanifested') {
  const entries = records(fs.readFileSync(path.join(root, manifestName), 'utf8'));
  assert.deepEqual(entries.map(entry => entry.name), files, 'manifest inventory mismatch');
  for (const {digest, name} of entries)
    assert.equal(hash(fs.readFileSync(path.join(root, name))), digest, `manifest hash ${name}`);
  console.log(`Exact manifest inventory and SHA-256: PASS (${entries.length} files)`);
} else {
  console.log('Pre-manifest replay: integrity manifest not checked');
}

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'bfs-split-coalesce-replay-'));
try {
  const decoded = records(fs.readFileSync(path.join(root, 'decoded-identities.sha256'), 'utf8'));
  const encodedNames = [];
  for (const {digest, name} of decoded) {
    assert(name.startsWith(prefix));
    const relative = safeRelative(name.slice(prefix.length));
    const encodedName = `${relative}.b64`;
    assert(!encodedNames.includes(encodedName), `duplicate decoded entry ${relative}`);
    encodedNames.push(encodedName);
    const encoded = fs.readFileSync(path.join(root, encodedName), 'ascii');
    assert.match(encoded, /^[A-Za-z0-9+/=\r\n]*$/);
    const compact = encoded.replace(/[\r\n]/g, '');
    const bytes = Buffer.from(compact, 'base64');
    assert.equal(bytes.toString('base64'), compact, `noncanonical base64 ${relative}`);
    assert.equal(hash(bytes), digest, `decoded identity ${relative}`);
    const target = path.join(temporary, relative);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.writeFileSync(target, bytes, {flag: 'wx'});
  }
  assert.deepEqual(encodedNames.sort(), files.filter(name => name.endsWith('.b64')));
  console.log(`Lossless source/log/patch decoding: PASS (${decoded.length} files)`);

  const verifier = path.join(root, 'verify-bench-results.sh');
  const header = path.join(temporary, 'diagnostic-source/src/amiga/perf_probe.h');
  for (const order of ['bfs-first', 'pfs3-first']) {
    const output = execFileSync('python3', [path.join(root, 'consumer/consume.py'),
      path.join(root, `split-coalesce-attribution-probe-${order}`),
      '--header', header, '--verifier', verifier], {maxBuffer: 8 * 1024 * 1024});
    assert(output.equals(fs.readFileSync(path.join(root, `probe-${order}-consumed.json`))),
      `consumer output bytes differ for ${order}`);
    fs.writeFileSync(path.join(temporary, `attribution-probe-${order}-consumed.json`), output, {flag: 'wx'});
    console.log(`Strict schema-12 consumer and exact JSON replay (${order}): PASS`);
  }
  const equivalence = execFileSync(process.execPath, [path.join(root, 'counter-equivalence.mjs'),
    root, verifier, path.join(root, 'retained-probe-controls')], {maxBuffer: 8 * 1024 * 1024});
  assert(equivalence.equals(fs.readFileSync(path.join(root, 'counter-equivalence.json'))));
  console.log('Retained 483 non-time fields per order and exact equivalence JSON: PASS');
  const summary = execFileSync(process.execPath, [path.join(root, 'summary.mjs'),
    path.join(temporary, 'attribution')], {maxBuffer: 8 * 1024 * 1024});
  assert(summary.equals(fs.readFileSync(path.join(root, 'summary.json'))));
  console.log('Opposite-order path matrix and exact primary summary JSON: PASS');
  console.log('Offline replay completed; no emulator, network, binary build or normal timing acceptance');
} finally {
  // Only remove the fresh, uniquely owned replay directory, never source evidence.
  fs.rmSync(temporary, {recursive: true});
}
