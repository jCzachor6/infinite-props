# CI artifacts and reports

Every branch push and pull request runs the native matrix, native/WASM
conformance, SFML/Godot integration checks and sanitizers. The same workflow
can also be called by another workflow without duplicating the checks.

Each job retains a `reports-*` artifact for 14 days, including on failure:

- `environment.json`: exact source commit, product version, platform,
  Python/Node/CMake versions and GitHub run identifiers.
- `*.log` and `*.step.json`: combined stdout/stderr, command, duration and
  original exit status for each build or validation step.
- `ctest.xml` or `node.xml`: JUnit test results.
- `summary.json` and `summary.md`: check results, also shown in the Actions
  job summary. A failed command or test report remains a failed job.
- WASM `conformance/conformance.json`: golden, pixel/output, trap, vector,
  color and compression comparison results. Failed image comparisons save
  `actual.png`, `expected.png`, `diff.png` and `comparison.json`; opaque
  magenta marks unequal pixels, including RGB under transparent pixels.

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

The current 0.4 candidate metadata still needs its final changelog date and
frozen specification status before that tag. Ordinary `develop` pushes retain
and validate candidate packages without publishing a GitHub Release.

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
