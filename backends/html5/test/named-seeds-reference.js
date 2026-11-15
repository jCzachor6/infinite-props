"use strict";
// Named seeds (spec/named-seeds-v1.1.md): an independent JavaScript model of
// the fork contract. It shares no code with the compiler or the Python script
// that froze tests/fixtures/named-seeds-v1.1.json, and must reproduce it.

const fnv1a32 = bytes => {
    let h = 2166136261;
    for (const byte of bytes) h = Math.imul(h ^ byte, 16777619) >>> 0;
    return h >>> 0;
};
// splitmix32, as pinned for the hash instruction.
const hash32 = value => {
    let x = value >>> 0;
    x = (x ^ (x >>> 16)) >>> 0; x = Math.imul(x, 0x7feb352d) >>> 0;
    x = (x ^ (x >>> 15)) >>> 0; x = Math.imul(x, 0x846ca68b) >>> 0;
    return (x ^ (x >>> 16)) >>> 0;
};
const TEXT_DOMAIN = "iprop.seed.fork/text/v1", INDEX_DOMAIN = "iprop.seed.fork/index/v1";
const TEXT_TAG = fnv1a32(Buffer.from(TEXT_DOMAIN, "utf8")), INDEX_TAG = fnv1a32(Buffer.from(INDEX_DOMAIN, "utf8"));
const forkName = (base, name) => hash32((base ^ hash32(TEXT_TAG ^ fnv1a32(Buffer.from(name, "utf8")))) >>> 0);
const forkIndex = (base, index) => hash32((base ^ hash32((INDEX_TAG ^ index) >>> 0)) >>> 0);
const fork = (base, key) => typeof key === "string" ? forkName(base, key) : forkIndex(base, key);
const hex = value => (value >>> 0).toString(16).padStart(8, "0");

// The inputs of the frozen vectors, listed here independently.
const BASES = [0, 1, 7, 0x7fffffff, 0x80000000, 0xffffffff];
const NAMES = ["leaves", "", "rim", "Leaves", "zażółć", "a\"b\\c\n", "12"];
const INDICES = [0, 1, -1, 12, 0x7fffffff, -0x80000000];
const CHAINS = [[7, ["clump", 3, 7]], [7, ["clump", 7, 3]], [0x1234, ["leaf", "x"]], [0xffffffff, [-1, "", 0]]];

function fixture() {
    const vectors = [];
    for (const base of BASES) {
        for (const name of NAMES) vectors.push({base: hex(base), name, result: hex(forkName(base, name))});
        for (const index of INDICES) vectors.push({base: hex(base), index, result: hex(forkIndex(base, index))});
    }
    const chains = CHAINS.map(([base, keys]) => ({base: hex(base), keys, result: hex(keys.reduce(fork, base))}));
    return {contract: "spec/named-seeds-v1.1.md",
        generator: "independent Python implementation of FNV-1a 32, the domain tags and splitmix32",
        text_domain: TEXT_DOMAIN, text_tag: hex(TEXT_TAG), index_domain: INDEX_DOMAIN, index_tag: hex(INDEX_TAG),
        vectors, chains};
}

module.exports = {fnv1a32, hash32, fork, forkName, forkIndex, fixture, TEXT_TAG, INDEX_TAG};
