"use strict";
// Invoked by the native export checker with real compiler diagnostics mutated
// by the same fixtures used by the Python reader and the formal JSON schema.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const { readDiagnostics, DIAGNOSTICS_SCHEMA_VERSION } = require("../contracts");
assert.equal(DIAGNOSTICS_SCHEMA_VERSION, 1);
for (const sample of JSON.parse(fs.readFileSync(process.argv[2], "utf8"))) {
    if (sample.accept) assert.equal(readDiagnostics(sample.document, sample.exit), sample.document, sample.id);
    else assert.throws(() => readDiagnostics(sample.document, sample.exit), /diagnostic JSON/, sample.id);
}
