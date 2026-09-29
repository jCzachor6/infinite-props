// conformance.js — the shared in-browser/Node conformance suite: runs the
// WASM interpreter over the golden seeds and compares FNV-1a hashes of the
// RGBA output and the structured outputs against pinned desktop values.
//
// Usable from Node (module.exports) and from a browser page (window).

(function (root, factory) {
    if (typeof module === "object" && module.exports) {
        module.exports = factory();
    } else {
        root.Conformance = factory();
    }
})(typeof self !== "undefined" ? self : this, function () {

    // Pinned by the desktop reference (ipgen + a hash over the RGBA).
    const GOLDEN = [
        { file: "portrait.iprop", seed: 0, hash: 2250220605 },
        { file: "portrait.iprop", seed: 1, hash: 2936451835 },
        { file: "portrait.iprop", seed: 2, hash: 518808934 },
        { file: "portrait.iprop", seed: 7, hash: 1856080556 },
        { file: "portrait.iprop", seed: 26, hash: 1022392143 },
        { file: "noise.iprop", seed: 0, hash: 2689969727 },
        { file: "shapes.iprop", seed: 0, hash: 1401765847 },
    ];

    function fnv1a(bytes) {
        let hash = 0x811c9dc5;
        for (let i = 0; i < bytes.length; ++i) {
            hash ^= bytes[i];
            hash = Math.imul(hash, 0x01000193);
        }
        return hash >>> 0;
    }

    // Runs the suite against a generated `iprop` handle (see js/api.js).
    // `fetchBytes` loads an asset file into a Uint8Array. Returns a promise
    // of { passed, failures: [message...] }.
    async function run(iprop, fetchBytes) {
        const failures = [];
        for (const entry of GOLDEN) {
            const bytes = new Uint8Array(await fetchBytes(entry.file));
            const result = iprop.generate(bytes, { seed: entry.seed });
            const hash = fnv1a(result.data);
            if (hash !== entry.hash) {
                failures.push(entry.file + " seed " + entry.seed +
                              ": hash " + hash.toString(16) +
                              " != golden " + entry.hash.toString(16));
            }
            // The portrait's pupil anchor must be (27, 28).
            if (entry.file === "portrait.iprop") {
                const pupil = result.outputs.find(function (o) {
                    return o.name === "pupilCenter";
                });
                if (!pupil || pupil.points.length !== 1 ||
                    pupil.points[0].x !== 27 || pupil.points[0].y !== 28) {
                    failures.push(entry.file + " seed " + entry.seed +
                                  ": pupilCenter mismatch");
                }
            }
        }
        return { passed: failures.length === 0, failures: failures };
    }

    return { GOLDEN: GOLDEN, fnv1a: fnv1a, run: run };
});
