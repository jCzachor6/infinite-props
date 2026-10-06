These two responses are authored harness fixtures, not outputs from a model.
`initial-failure.json` calls the nonexistent `unknown_draw`; `one-repair.json`
replaces that call with `rect` and satisfies the public asset-01 pixel/output
predicates. Both preserve CRLF source text. Tests label their session
`authored_witness`, retain the first failure, and import at most one repair.
