"use strict";
// Expected values from the independent Python oracles in scripts/, computed at
// test time instead of stored. Each oracle pins its own output hash in CTest.
const { execFileSync } = require("node:child_process");
const path = require("node:path");

const repo = path.resolve(__dirname, "../../..");
const python = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");

function oracle(script) {
    return JSON.parse(execFileSync(python, [path.join(repo, "scripts", script), "--print"],
        { encoding: "utf8", maxBuffer: 1 << 28 }));
}

module.exports = { oracle, python };
