# TODO

- [ ] Restore Unity Editor pixel conformance in GitHub CI after configuring a
  usable CI license. Build the production native plugin for the Editor runner,
  consume the complete shared `pixel-corpus` artifact in both Gamma and Linear
  color spaces, and require exact RGBA, texture upload and unblended GPU readback.
  Retain both test reports and reconnect the Editor job to `release-assets.needs`.
  Keep the standalone C# corpus checks enabled while Editor automation is deferred.

The [local Unity verification workflow](backends/unity/README.md#verify) is
already available. The 2026-10-25 baseline passed all 16,177 cases per color
space in Unity 2022.3.62f1 with Metal GPU readback.
