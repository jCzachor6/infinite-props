"use strict";

// These archived source snapshots accompany independent image/output pins.
// Re-author their pure selectors for the current language before compiling;
// the runtime rejects their versioned binaries and has no migration adapter.
function renderSource(source) {
    return source.replace(/\bvariants\s+\d+/g, "input i32 v=0")
        .replace(/\barchetypes\s+\d+/g, "")
        .replace(/\.draw\((seed|\d+),0,0,/g, ".draw($1,");
}

function renderArguments(options = {}) {
    const {variant, archetype, ...current} = options;
    if (variant !== undefined || archetype !== undefined) {
        current.uniforms = {...current.uniforms};
        if (variant !== undefined) current.uniforms.v = variant;
        if (archetype !== undefined) current.uniforms.a = archetype;
    }
    return current;
}

module.exports = {renderSource, renderArguments};
