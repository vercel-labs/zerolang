## Cross-Compilation Guide

Zero target names are explicit and target facts are visible through `zero targets`.

```sh
bin/zero targets
bin/zero check --target linux-musl-x64 examples/memory-package
bin/zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
```

The compiler separates checking from executable linking. Target-neutral code can
check for non-host targets, while hosted APIs such as `std.fs` are rejected when
the selected target or direct backend cannot provide that capability.

```sh
bin/zero check --json --target linux-musl-x64 conformance/packages/target-incompatible-app
```

For agent planning, `check --json` can also report whether a selected direct
artifact is expected to build without emitting it:

```sh
bin/zero check --json --emit obj --target linux-musl-x64 conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.0
```

The top-level result stays about language validity. The nested
`targetReadiness` object reports target buildability and carries structured
backend blockers such as `target`, `objectFormat`, `backend`, `stage`, and
`unsupportedFeature`.

## Direct Artifacts

Supported executable builds use Zero's direct target emitters. Unsupported
targets or language features report diagnostics rather than silently choosing an
external backend.

```sh
bin/zero build --emit exe --target linux-musl-x64 examples/direct-exe-return.0 --out .zero/out/direct-exe-return
bin/zero build --emit obj --target darwin-arm64 examples/direct-call-add.0 --out .zero/out/direct-call-add.o
```

Use JSON modes to inspect target support, required capabilities, selected
emitters, and artifact facts:

```sh
bin/zero build --json --emit exe --target linux-musl-x64 examples/direct-exe-return.0
bin/zero graph --json --target darwin-arm64 examples/memory-package
bin/zero size --json --target linux-musl-x64 examples/direct-exe-return.0
```

## Sysroots And C Boundaries

Zero reports sysroot and C ABI facts in JSON so cross-target builds do not
silently reuse host SDK paths. When a target requires an explicit SDK/sysroot,
use the environment variable named by `zero targets --json`.

C interop is still early. Keep C-facing code small, inspect `zero abi --json`
where applicable, and prefer examples that make target assumptions explicit.

## Current Boundary

The public target set is focused on native executables and object files. Use
`zero targets --json`, `zero check --json --emit <kind>`, and
`zero size --json` to inspect whether a requested target is supported before
writing artifacts. Unsupported target/backend combinations should fail with a
structured diagnostic instead of silently falling back to a different backend.
