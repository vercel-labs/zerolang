---
name: builds
description: Build, run, target, and profile Zero programs.
---

# Zero Builds

Use this when an agent needs to run, build, cross-build, inspect artifacts, or explain target support for a Zero program.

## Inputs

Most build commands accept one of these graph-backed inputs:

- a direct `.graph` or `.program-graph` artifact
- a package directory containing `zero.toml` or `zero.json`
- a direct path to `zero.toml` or `zero.json`

When both manifests are present in the same package root, Zero uses
`zero.toml`. Prefer one checked-in manifest unless testing precedence.

For packages, normal check, build, run, test, size, and mem commands compile from the checked-in `zero.graph` store. When the `.0` source projection was edited, those commands refresh the stale store from source first and note it on stderr; set `ZERO_STALE=fail` to make staleness an error (RGP008) instead. They never rewrite `.0` files. Use `zero verify-projection` when CI or review needs projection drift to fail, and `zero export` only when a human-readable projection needs regeneration. When already inside a package, omit the input and commands default to the current directory.

## Run

Use `zero run` for the host development loop:

```sh
zero run
zero run -- input.txt
zero run examples/hello.graph
zero run examples/cli-file.graph -- input.txt
```

Arguments after `--` are passed to the Zero program.

## Build

Use `zero build` when the user asks for an artifact. It is the normal command
for executables, object files, LLVM IR, cross-target artifacts, and CI outputs.
Use direct emitters. The removed generated-C backend is not a fallback path.

```sh
zero build --emit exe --out .zero/out/app
zero build --emit obj --out .zero/out/app.o
zero build --emit exe examples/hello.graph --out .zero/out/hello
zero build --emit obj examples/hello.graph --out .zero/out/hello.o
```

Use LLVM only when the request is explicit. LLVM is experimental: it is not the
default backend and not release eligible. Textual IR is inspectable with
`--emit llvm-ir`; host executable builds require a ready clang toolchain. LLVM
currently lowers scalar code, direct calls, branches, loops, primitive fixed
arrays, byte views, readonly strings, and primitive `std.mem` helpers:

```sh
zero build --backend llvm --emit llvm-ir examples/hello.graph --out .zero/out/hello.ll
zero build --backend llvm --emit exe examples/hello.graph --out .zero/out/hello-llvm
zero run --backend llvm examples/hello.graph
```

Use `--json` when a tool will read exact build fields:

```sh
zero build --json --target linux-musl-x64 examples/memory-package
```

Useful JSON fields include `artifact`, `sizeBytes`, `toolchain`, `releaseTargetContract`, selected target facts, linker flavor, and sysroot status.

## Graph Inputs

When an agent is authoring a repository graph package, patch the package graph
and use normal build/run commands. They compile from `zero.graph` and do not
require `.0` projections to exist:

```sh
zero patch --op 'addMain'
zero run
zero build --out .zero/out/app
```

Use `zero export` when humans need checked-in `.0` projections. After a human edits a projection, the next graph-store compile refreshes the store automatically, or run `zero import` to refresh it explicitly. `zero status` reports the active store format.

Build, run, test, size, and mem commands maintain a derived final-MIR cache in the native cache, keyed by graph hash, compiler version, target, emit kind, and backend request. Agents should not patch `.zmir` files; JSON outputs report cache reuse in a `mappedFinalMir` row.

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
zero build --profile release-small examples/hello.graph
zero size --profile tiny examples/hello.graph
```

Use `zero size` to explain retained functions, sections, literals, runtime shims, imports, debug metadata, and optimization hints. Add `--json` when a tool needs exact fields.
Use `zero size --backend llvm` when the question is specifically about the explicit LLVM backend; the report includes LLVM target triple, optimization level, retained runtime/helper facts, toolchain readiness, and direct-vs-LLVM comparison rows.

## Troubleshooting

- `zero doctor` checks host and target readiness.
- `zero doctor --json` reports `llvmToolchain` readiness for explicit LLVM host builds.
- LLVM JSON facts include `backendLifecycle` so tools can distinguish explicit experimental readiness from release support.
- `BLD003` means an old backend flag was requested; remove it.
- `BLD004` with `backendBlocker.backend: "llvm"` means the selected LLVM artifact, target, command, MIR subset, or clang toolchain is not ready.
- Missing sysroot facts identify the required `ZERO_SYSROOT_*` variable.
- Unsupported targets fail explicitly instead of silently choosing another backend.
