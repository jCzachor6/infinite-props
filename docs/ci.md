# CI artifacts and reports

Every branch push and pull request runs the native matrix, native/WASM
conformance, SFML/Godot integration checks and sanitizers. The same workflow
can also be called by another workflow without duplicating the checks.

Each job retains a `reports-*` artifact for 14 days, including on failure:

- `environment.json`: exact source commit, product version, platform,
  Python/Node/CMake versions and GitHub run identifiers.
- `*.log` and `*.step.json`: combined stdout/stderr, command, duration and
  original exit status for each build or validation step.
- `ctest.xml`, `node.xml` or `docs-render.xml`: JUnit test results.
- `summary.json` and `summary.md`: check results, also shown in the Actions
  job summary. A failed command or test report remains a failed job.
- WASM `conformance/conformance.json`: golden, pixel/output, trap, vector,
  color and compression comparison results. Failed image comparisons save
  `actual.png`, `expected.png`, `diff.png` and `comparison.json`; opaque
  magenta marks unequal pixels, including RGB under transparent pixels.
- `language/language-docs.json`: compiler-derived builtin coverage and results
  for the handbook's successful examples, compile rejections and runtime traps.

Successful jobs also retain downloadable builds for 14 days:

- `sdk-macOS-*` and `sdk-Windows-*`: native toolchain/C API SDK archive,
  manifest and SHA256 checksums.
- `sdk-Linux-*`: the native SDK also includes the tested SFML and Godot
  integrations. macOS/Windows CI packages contain the toolchain and C API;
  included backends are explicit in each SDK's `release.json`. Linux also
  produces the single source archive used for the release.
- `web-build`: separate WASM and website archives, containing their manifests
  and licenses, plus extraction-validation results and SHA256 checksums.

Packaging scripts unpack their completed archives into a separate directory
and rerun installed SDK consumers, included native integrations and web/WASM
smoke tests there. `*-validation.json` binds those results to the source commit
and the actual archive digests. Archive timestamps/ownership are normalized;
executable permissions and relative library symlinks are preserved.

Local packaging, including `release.py --check`, rejects untracked numbered
siblings of tracked sources (for example `composition 2.md`) before running
validation. It reports their paths and never deletes or ignores them. Preserve
any separate edits outside the checkout before removing a copy; Git history
can establish whether its contents are an older version. These names can also
be created by tools outside the packaging scripts. Archive extraction requires
a new or empty directory and refuses to merge a package into an existing
source checkout or previously extracted SDK.

If numbered copies return without packaging being run, inspect the checkout's
sync provider before attributing them to the release scripts. In the macOS
checkout used during 0.5 development, scoped File Provider logs matched all
four returning files by item ID and inode: iCloud Drive created them locally
with `why:itemChangedRemotely`. This establishes remote synchronization as
the cause of that recurrence, but does not identify which device or tool
originally created the numbered remote items. Removing a local copy cannot
guarantee that a later remote change will not restore it. Preserve and compare
its contents, remove only confirmed duplicates, and keep the packaging guard
enabled; changing cloud settings is a separate checkout-management decision.

The generated handbook directory is configurable independently of the native
build directory. In a synchronized checkout, set
`-DIPROP_LANGUAGE_DOCS_DIR=/absolute/path/outside/cloud/language-docs` when
configuring CMake. Builds and SDK installation use that exact directory,
keeping generated manifest ownership checks active. This avoids synchronization
conflicts in generated documentation; source copies still require the checks
above.
The website generator accepts the same absolute directory through the
`IPROP_LANGUAGE_DOCS_DIR` environment variable. Set it when running website
builds from a synchronized checkout; its default remains `build/language-docs`.

Native builds with Python generate the complete handbook from `docs/language/`
and `ipc --describe-language`. CTest compiles and executes all registered
language examples, including expected failures, and requires successful
examples for every builtin. The WASM job runs the same documentation check,
tests the Markdown renderer and builds the website from the generated book.
Broken chapter/example links, missing or duplicate builtin explanations,
incorrect signatures, failed examples and invalid search anchors fail CI.

Every SDK ships the book under `share/iprop/docs/`; WASM and website archives
use `docs/` at their root. `language-manifest.json` records the product version,
source commit, development/release status, compiler catalog and file digests.
Installed SDK checks compare it with the installed compiler. Archive collection
also requires the complete documentation payload, unchanged authored inputs,
the same commit/version and a clean source checkout for generated documentation.
Historical release documentation stays tied to its original Git tag and archives.

These are build artifacts for the recorded commit; uploading them does not
create a GitHub Release or release tag. Caches are not used as release inputs.

The `release-assets` job waits for all six check jobs, downloads only this
run's artifacts, verifies every tracked source file against Git, verifies
platform/archive manifests and extraction reports, and requires successful
JUnit results from every check job. Its artifact contains 13 files: the source,
three native SDKs, WASM and website archives, four extraction reports, combined
CI validation, a release manifest and one complete SHA256 checksum list.
All archive/report identities must match the checkout's product version and
commit. Every ordinary CI run exercises this collection gate as well.

## Automatic releases

`.github/workflows/release.yml` runs on a pushed `vX.Y.Z` tag. It first requires:

- An existing tag identifying the checked-out commit, present in `main` history.
- Matching product, editor, website and lockfile versions.
- A dated changelog heading: `## vX.Y.Z — YYYY-MM-DD`.
- A format specification whose status is frozen rather than a draft/candidate.

It then calls the complete CI workflow on that exact tagged commit. After
the build, tests, archive extraction and collection succeed, the publish job
downloads `release-assets` from the same run and repeats verification. The
publisher creates a draft, uploads the 13 assets, checks GitHub's SHA256
digests/sizes, rechecks the remote tag and publishes only the complete draft.
It reads back the published release and assets before reporting success.
There is no merge, version bump or tag creation step in this workflow.

Only the publishing job receives `contents: write`; other jobs have read-only
repository access. The built-in `GITHUB_TOKEN` supplies release permissions,
so no personal token is needed. Failed upload attempts leave a draft; retrying
can replace matching named assets in that draft. Already published assets are
never replaced, and an identical published release makes a retry succeed
without further writes. Concurrent publication for one tag is serialized.

To release after the reviewed changes are merged and the metadata is final:

```sh
git tag -a v0.4.0 -m "Release 0.4.0"
git push origin v0.4.0
```

Before creating a release tag, finalize the changelog date and freeze the
matching specification. Ordinary `develop` pushes retain and validate candidate
packages without publishing a GitHub Release.

Local diagnostic commands:

```sh
python3 scripts/ci_report.py run test ctest --test-dir build --output-on-failure --output-junit ci-reports/ctest.xml
python3 scripts/ci_report.py summary
IPROP_CONFORMANCE_REPORT_DIR="$PWD/build/ci-reports/conformance" node backends/html5/test/run-tests.js build/wasm
```

Packaging requires Python 3.12 or newer. To use a separate output directory:

```sh
python3 scripts/release.py --output-dir build/release
python3 scripts/package_web.py --output-dir build/release
```
