---
name: builds
description: Build, run, ship, target, and profile Zero programs.
---

# Zero Builds

Use this when an agent needs to run, build, cross-build, inspect artifacts, or explain target support for a Zero program.

## Inputs

Most build commands accept one of these:

- a single `.0` file
- a package directory containing `zero.toml` or `zero.json`
- a direct path to `zero.toml` or `zero.json`

When both manifests are present in the same package root, Zero uses
`zero.toml`. Prefer one checked-in manifest unless testing precedence.

For packages with `repositoryGraph.compilerInput: true`, normal check, build,
run, test, size, ship, and mem commands compile from the checked-in
`zero.graph` store. Source projections may be clean, missing, stale, or in
conflict; commands report that state and do not rewrite `.0` files. Use
`zero verify-sync` when CI or review needs graph/source drift to fail, and
`zero sync --from-graph` to regenerate projections from the store. Other
packages compile from `.0` source text.

## Run

Use `zero run` for the host development loop:

```sh
zero run examples/hello.0
zero run examples/cli-file.0 -- input.txt
```

Arguments after `--` are passed to the Zero program.

## Build

Use direct emitters. The removed generated-C backend is not a fallback path.

```sh
zero build --emit exe examples/hello.0 --out .zero/out/hello
zero build --emit obj examples/hello.0 --out .zero/out/hello.o
```

Use LLVM only when the request is explicit. LLVM is experimental: it is not the
default backend, not release eligible, and not accepted by `zero ship`. Textual
IR is inspectable with `--emit llvm-ir`; host executable builds require a ready
clang toolchain. LLVM currently lowers scalar code, direct calls, branches,
loops, primitive fixed arrays, byte views, readonly strings, and primitive
`std.mem` helpers:

```sh
zero build --backend llvm --emit llvm-ir examples/hello.0 --out .zero/out/hello.ll
zero build --backend llvm --emit exe examples/hello.0 --out .zero/out/hello-llvm
zero run --backend llvm examples/hello.0
```

Use `--json` when a tool will read exact build fields:

```sh
zero build --json --target linux-musl-x64 examples/memory-package
```

Useful JSON fields include `artifact`, `sizeBytes`, `toolchain`, `releaseTargetContract`, selected target facts, linker flavor, and sysroot status.

## Graph Inputs

When an agent is authoring an opted-in repository graph package, patch the
package graph and use normal build/run commands. They compile from `zero.graph`
and do not require `.0` projections to exist:

```sh
zero patch --op 'addMain'
zero check .
zero run .
zero build . --out .zero/out/app
```

Use `zero sync --from-graph <package>` when humans need checked-in `.0`
projections. If a human edits a projection, run
`zero sync --from-source <package>` before the next graph-store compile.
Normal build and run commands read binary `zero.graph` stores directly by
default; `zero status <package>` reports the active store format.

Repository graph build/run/test/size/ship/mem commands and standalone
`.program-graph` build, run, and size artifact commands also maintain a final
MIR cache under
`.zero/cache/native/mir-*.zmir`. The cache is keyed by graph hash, compiler
version, target, emit kind, and backend request. On a miss, the compiler lowers
the graph once, writes compact final MIR, memory-maps and verifies that cache,
borrows stable string and readonly-data storage from the mapped file, then sends
the verified MIR to codegen. On warm repository `zero.graph` build/run hits and
warm standalone `.program-graph` build/run hits, the mapped final MIR is the
immediate codegen input: the compiler skips graph-to-MIR lowering and checked
Program reconstruction. Graph-backed `zero size` derives helper and capability
summaries from graph/IR facts instead of reconstructing checked Program facts.
JSON outputs report graph `lowering: "mapped-final-mir"` when this path is used
and include a `mappedFinalMir` compiler cache row with `path`, `hit`,
`written`, `memoryMapped`, `borrowedStorage`, `byteLength`,
`codegenImmediate`, and `programReconstructed` facts. Do not assume standalone
artifact commands outside build/run/size use the mapped MIR path unless their
JSON output reports it.

If another tool hands you a standalone `.program-graph`, normal `zero build`
and `zero run` can validate it as an interchange artifact. Do not create a
standalone graph artifact for the ordinary package loop; use the package path so
the compiler reads `zero.graph` directly.

## Targets

Inspect target names and capability facts before cross-building:

```sh
zero targets
zero check --target linux-musl-x64 examples/memory-package
zero inspect --target linux-musl-x64 examples/memory-package
```

Hosted APIs such as process args, environment, filesystem, net, and proc are target-gated. A non-host target may reject code that checks on the host.

## Profiles

Common profile names are `debug`, `dev`, `release-fast`, `release-small`, `tiny`, and `audit`.

```sh
zero build --profile release-small examples/hello.0
zero size --profile tiny examples/hello.0
```

Use `zero size` to explain retained functions, sections, literals, runtime shims, imports, debug metadata, and optimization hints. Add `--json` when a tool needs exact fields.
Use `zero size --backend llvm` when the question is specifically about the explicit LLVM backend; the report includes LLVM target triple, optimization level, retained runtime/helper facts, toolchain readiness, and direct-vs-LLVM comparison rows.

## Ship

`zero ship` produces a release preview:

```sh
zero ship --target linux-musl-x64 examples/hello.0 \
  --out .zero/ship/hello
```

The preview includes artifact names, sizes, hashes, checksum file metadata, size report data, debug-symbol metadata, and target contract facts.

## Troubleshooting

- `zero doctor` checks host and target readiness.
- `zero doctor --json` reports `llvmToolchain` readiness for explicit LLVM host builds.
- LLVM JSON facts include `backendLifecycle` so tools can distinguish explicit experimental readiness from release support.
- `BLD003` means an old backend flag was requested; remove it.
- `BLD004` with `backendBlocker.backend: "llvm"` means the selected LLVM artifact, target, command, MIR subset, or clang toolchain is not ready.
- Missing sysroot facts identify the required `ZERO_SYSROOT_*` variable.
- Unsupported targets fail explicitly instead of silently choosing another backend.
