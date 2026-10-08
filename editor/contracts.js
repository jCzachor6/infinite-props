"use strict";

// Protocol-specific validation; unknown additive fields are ignored.
const DIAGNOSTICS_SCHEMA_VERSION = 1;
const exits = { ok: 0, invalid_source: 1, cli_error: 2, internal_error: 3 };
const sourceIdPattern = /^sha256:[0-9a-f]{64}$/;
const diagnosticCodePattern = /^IPS-[A-Z]+-[0-9]{3}$/;
const stages = new Set(["lex", "parse", "import", "typing", "planning", "validation", "io", "cli", "internal"]);
const integer = value => Number.isSafeInteger(value) && value >= 0;
const nullableString = value => value === null || typeof value === "string";
const sourceId = value => value === null || (typeof value === "string" && sourceIdPattern.test(value));
function requireField(condition) {
    if (!condition) throw new Error("invalid compiler diagnostic JSON (schema 1)");
}
function position(value) {
    requireField(value && integer(value.byte_offset) && integer(value.line) && value.line > 0 &&
        integer(value.column) && value.column > 0);
}
function validateRange(value) {
    requireField(value && typeof value === "object");
    position(value.start); position(value.end);
    requireField(value.start.byte_offset <= value.end.byte_offset && value.start.line <= value.end.line &&
        (value.start.line !== value.end.line || value.start.column <= value.end.column));
}

function readDiagnostics(result, exitCode) {
    requireField(result && result.schema_version === DIAGNOSTICS_SCHEMA_VERSION && typeof result.product_version === "string" &&
        result.language_version && integer(result.language_version.major) && integer(result.language_version.minor) &&
        Object.hasOwn(exits, result.status) && exits[result.status] === exitCode && Array.isArray(result.diagnostics));
    for (const d of result.diagnostics) {
        requireField(d && typeof d.code === "string" && diagnosticCodePattern.test(d.code) && stages.has(d.stage) && typeof d.message === "string" &&
            ["error", "warning", "note"].includes(d.severity) && nullableString(d.file) && sourceId(d.source_id) &&
            nullableString(d.expected_type) && nullableString(d.actual_type) && Array.isArray(d.suggestions) &&
            d.suggestions.every(s => typeof s === "string") && Array.isArray(d.related_locations) && Array.isArray(d.fix_its));
        requireField(d.location === null || (d.location && integer(d.location.line) && d.location.line > 0 &&
            integer(d.location.column) && d.location.column > 0));
        if (d.range !== null) validateRange(d.range);
        for (const related of d.related_locations) {
            requireField(related && typeof related.file === "string" && typeof related.message === "string");
            validateRange(related.range);
        }
        for (const fix of d.fix_its) {
            requireField(fix && typeof fix.replacement === "string" && typeof fix.source_id === "string" && sourceIdPattern.test(fix.source_id));
            validateRange(fix.range);
        }
    }
    requireField(result.status !== "ok" || result.diagnostics.every(d => d.severity !== "error"));
    return result;
}

module.exports = { DIAGNOSTICS_SCHEMA_VERSION, readDiagnostics };
