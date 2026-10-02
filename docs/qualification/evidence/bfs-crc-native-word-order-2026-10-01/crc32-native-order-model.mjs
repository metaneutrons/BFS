#!/usr/bin/env node
/*
 * Independent CRC32 table and native-order recurrence model.
 *
 * This validates the stated algorithm against a bitwise IEEE CRC32 oracle and
 * checks the existing assembly table constants. It does not execute 68k code
 * or qualify the 68k ABI, loads, alignment behavior, or target runtime.
 */

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const asmPath = path.resolve(root, process.env.BFS_CRC_MODEL_ASM || "src/amiga/crc32_68k.s");
const corePath = path.join(root, "src/core/crc32.c");
const asmSource = fs.readFileSync(asmPath, "utf8");
const coreSource = fs.readFileSync(corePath, "utf8");
const POLY = 0xedb88320;
const MASK = 0xffffffff;
const seeds = [0x00000000, 0xffffffff, 0x12345678, 0xa5c39e12];
const tableLabels = [
    "crc32_table_68k:",
    "crc32_table_68k_t1:",
    "crc32_table_68k_t2:",
    "crc32_table_68k_t3:",
];

function byteStep(crc, byte) {
    let value = (crc ^ byte) >>> 0;
    for (let bit = 0; bit < 8; bit++) {
        value = value & 1 ? ((value >>> 1) ^ POLY) >>> 0 : value >>> 1;
    }
    return value >>> 0;
}

function makeTables() {
    const tables = [[], [], [], []];
    for (let index = 0; index < 256; index++)
        tables[0][index] = byteStep(0, index);
    for (let table = 1; table < 4; table++) {
        for (let index = 0; index < 256; index++)
            tables[table][index] = byteStep(tables[table - 1][index], 0);
    }
    return tables;
}

function parseAsmTable(source, label, nextLabel) {
    const start = source.indexOf(label);
    assert.notEqual(start, -1, `missing assembly table label ${label}`);
    const contentStart = start + label.length;
    const end = nextLabel ? source.indexOf(nextLabel, contentStart) : source.length;
    assert.notEqual(end, -1, `missing following assembly table label ${nextLabel}`);
    const values = [];
    for (const line of source.slice(contentStart, end).split(/\r?\n/)) {
        const match = line.match(/^\s*\.long\s+([^|]+?)\s*$/);
        if (!match) continue;
        for (const token of match[1].split(",")) {
            const value = token.trim();
            assert.match(value, /^0x[0-9a-fA-F]{8}$/,
                `unexpected assembly table value ${value}`);
            values.push(Number.parseInt(value.slice(2), 16) >>> 0);
        }
    }
    assert.equal(values.length, 256, `${label} must contain 256 entries`);
    return values;
}

function parseCoreTable(source) {
    const start = source.indexOf("static const uint32_t crc32_table[256] = {");
    assert.notEqual(start, -1, "missing core CRC32 table initializer");
    const end = source.indexOf("};", start);
    assert.notEqual(end, -1, "unterminated core CRC32 table initializer");
    const values = [...source.slice(start, end).matchAll(/0x([0-9a-fA-F]{8})/g)]
        .map((match) => Number.parseInt(match[1], 16) >>> 0);
    assert.equal(values.length, 256, "core CRC32 table must contain 256 entries");
    return values;
}

function bswap(value) {
    return (((value & 0xff) << 24) |
        (((value >>> 8) & 0xff) << 16) |
        (((value >>> 16) & 0xff) << 8) |
        (value >>> 24)) >>> 0;
}

function bitwiseOracle(seed, data, offset, length) {
    let crc = (~seed) >>> 0;
    for (let index = 0; index < length; index++)
        crc = byteStep(crc, data[offset + index]);
    return (~crc) >>> 0;
}

function nativeOrderModel(seed, data, offset, length, tables) {
    if (length === 0) return seed >>> 0;

    let state = bswap((~seed) >>> 0);
    let index = 0;
    while (index + 4 <= length) {
        const wordBE = ((data[offset + index] << 24) |
            (data[offset + index + 1] << 16) |
            (data[offset + index + 2] << 8) |
            data[offset + index + 3]) >>> 0;
        const difference = (wordBE ^ state) >>> 0;
        state = (bswap(tables[0][difference & 0xff]) ^
            bswap(tables[1][(difference >>> 8) & 0xff]) ^
            bswap(tables[2][(difference >>> 16) & 0xff]) ^
            bswap(tables[3][difference >>> 24])) >>> 0;
        index += 4;
    }
    while (index < length) {
        const tableIndex = ((state >>> 24) ^ data[offset + index]) & 0xff;
        state = (bswap(tables[0][tableIndex]) ^ ((state << 8) >>> 0)) >>> 0;
        index++;
    }
    return (~bswap(state)) >>> 0;
}

function makePattern(length, salt) {
    const bytes = new Uint8Array(length);
    for (let index = 0; index < length; index++)
        bytes[index] = ((index * 73 + (index >>> 3) * 19 + salt * 29) ^
            (index >>> 5)) & 0xff;
    return bytes;
}

const expectedTables = makeTables();
const asmTables = tableLabels.map((label, index) =>
    parseAsmTable(asmSource, label, tableLabels[index + 1]));
let asmConstantsChecked = 0;
for (let table = 0; table < 4; table++) {
    for (let index = 0; index < 256; index++) {
        assert.equal(asmTables[table][index], expectedTables[table][index],
            `assembly T${table}[${index}] differs from polynomial-derived table`);
        asmConstantsChecked++;
    }
}

const coreTable = parseCoreTable(coreSource);
for (let index = 0; index < 256; index++)
    assert.equal(coreTable[index], expectedTables[0][index],
        `core T0[${index}] differs from polynomial-derived table`);
for (const fragment of [
    "uint32_t crc = ~initial;",
    "crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);",
    "return ~crc;",
])
    assert.ok(coreSource.includes(fragment), `core API recurrence changed: ${fragment}`);

const zeroGuard = asmSource.search(/tst\.l\s+d2\s*\n\s*beq\s+\.Ldone/);
const firstDataRead = asmSource.indexOf("move.l  (a0)+,d1");
const firstTailRead = asmSource.indexOf("move.b  (a0)+,d1");
assert.notEqual(zeroGuard, -1, "assembly has no zero-length guard");
assert.ok(firstDataRead > zeroGuard && firstTailRead > zeroGuard,
    "assembly data reads must follow the zero-length guard");

const counts = {
    short: 0,
    boundaries: 0,
    random: 0,
    seedBasis: 0,
    chaining: 0,
    nullZero: 0,
};
function compare(seed, data, offset, length, label) {
    const expected = bitwiseOracle(seed, data, offset, length);
    const actual = nativeOrderModel(seed, data, offset, length, expectedTables);
    assert.equal(actual, expected,
        `${label}: seed=0x${(seed >>> 0).toString(16)} offset=${offset} length=${length}`);
}

const shortBytes = makePattern(8 + 132, 7);
for (const seed of seeds) {
    for (let offset = 0; offset < 8; offset++) {
        for (let length = 0; length <= 132; length++) {
            compare(seed, shortBytes, offset, length, "short/unaligned");
            counts.short++;
        }
    }
}

const boundaryLengths = [4095, 4096, 4097, 65535, 65536, 65537];
for (let offset = 0; offset < 8; offset++) {
    const boundaryBytes = makePattern(offset + 65537, 31 + offset);
    for (const seed of seeds) {
        for (const length of boundaryLengths) {
            compare(seed, boundaryBytes, offset, length, "4K/64K boundary");
            counts.boundaries++;
        }
    }
}

let randomState = 0x6d2b79f5;
function nextRandom() {
    randomState ^= randomState << 13;
    randomState ^= randomState >>> 17;
    randomState ^= randomState << 5;
    randomState >>>= 0;
    return randomState;
}
for (let test = 0; test < 512; test++) {
    const offset = nextRandom() & 7;
    const length = nextRandom() % 16385;
    const seed = nextRandom();
    const bytes = new Uint8Array(offset + length);
    for (let index = 0; index < bytes.length; index++)
        bytes[index] = nextRandom() & 0xff;
    compare(seed, bytes, offset, length, "deterministic random");
    counts.random++;
}

for (let bit = 0; bit < 32; bit++) {
    const seed = (2 ** bit) >>> 0;
    for (let byte = 0; byte < 256; byte++) {
        const oneByte = Uint8Array.of(byte);
        compare(seed, oneByte, 0, 1, "seed basis/one-byte");
        counts.seedBasis++;
        for (let position = 0; position < 4; position++) {
            const word = new Uint8Array(4);
            word[position] = byte;
            compare(seed, word, 0, 4, "seed basis/four-byte position");
            counts.seedBasis++;
        }
    }
}

const chainLengths = [0, 1, 3, 4, 5, 127, 128, 129, 4095, 4096,
    4097, 65535, 65536, 65537];
for (let test = 0; test < 128; test++) {
    const offset = test & 7;
    const length = chainLengths[test % chainLengths.length];
    const seed = seeds[test % seeds.length];
    const split = length === 0 ? 0 : (test * 37) % (length + 1);
    const bytes = makePattern(offset + length, 101 + test);
    const fullModel = nativeOrderModel(seed, bytes, offset, length, expectedTables);
    const fullOracle = bitwiseOracle(seed, bytes, offset, length);
    const firstModel = nativeOrderModel(seed, bytes, offset, split, expectedTables);
    const firstOracle = bitwiseOracle(seed, bytes, offset, split);
    const chainedModel = nativeOrderModel(firstModel, bytes, offset + split,
        length - split, expectedTables);
    const chainedOracle = bitwiseOracle(firstOracle, bytes, offset + split,
        length - split);
    assert.equal(fullModel, fullOracle, `full chaining oracle case ${test}`);
    assert.equal(chainedModel, chainedOracle, `chained oracle case ${test}`);
    assert.equal(chainedModel, fullModel, `CRC chaining case ${test}`);
    counts.chaining++;
}

const noRead = new Proxy(Object.create(null), {
    get(_target, property) {
        throw new Error(`unexpected zero-length data read of ${String(property)}`);
    },
});
for (const seed of seeds) {
    assert.equal(nativeOrderModel(seed, null, 0, 0, expectedTables), seed >>> 0);
    assert.equal(bitwiseOracle(seed, null, 0, 0), seed >>> 0);
    assert.equal(nativeOrderModel(seed, noRead, 0, 0, expectedTables), seed >>> 0);
    assert.equal(bitwiseOracle(seed, noRead, 0, 0), seed >>> 0);
    counts.nullZero += 4;
}

const equivalenceCases = counts.short + counts.boundaries + counts.random +
    counts.seedBasis + counts.chaining;
console.log("PASS: CRC32 native-order algorithm model");
console.log(`ASM table constants checked: ${asmConstantsChecked}/1024`);
console.log("Core T0 constants checked: 256/256; API complement/byte recurrence matches oracle");
console.log(`Bitwise-oracle equivalence cases: ${equivalenceCases}`);
console.log(`  lengths 0..132, offsets 0..7, four seeds: ${counts.short}`);
console.log(`  4 KiB/64 KiB boundaries, offsets 0..7, four seeds: ${counts.boundaries}`);
console.log(`  deterministic random cases: ${counts.random}`);
console.log(`  seed basis vectors (1-byte + four byte positions): ${counts.seedBasis}`);
console.log(`  chaining cases: ${counts.chaining}`);
console.log(`Zero/NULL conceptual no-read checks: ${counts.nullZero}`);
console.log("LIMIT: JS model only; no 68k assembly execution, ABI, alignment-load, or target-runtime qualification.");
