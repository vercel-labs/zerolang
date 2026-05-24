---
name: builds
description: Build, run, ship, target, and profile Zero programs.
---

# Zero Builds

Use this when an agent needs to run, build, cross-build, inspect artifacts, or explain target support for a Zero program.

## Inputs

Most build commands accept one of these:

- a single `.0` file
- a package directory containing `zero.json`
- a direct path to `zero.json`

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

Use `--json` when a tool will read the result:

```sh
zero build --json --target linux-musl-x64 examples/memory-package
```

Useful JSON fields include `artifact`, `sizeBytes`, `toolchain`, `releaseTargetContract`, selected target facts, linker flavor, and sysroot status.

## Targets

Inspect target names and capability facts before cross-building:

```sh
zero targets
zero check --target linux-musl-x64 examples/memory-package
zero graph --json --target linux-musl-x64 examples/memory-package
```

Hosted APIs such as process args, environment, filesystem, net, and proc are target-gated. A non-host target may reject code that checks on the host.

## Profiles

Common profile names are `debug`, `dev`, `release-fast`, `release-small`, `tiny`, and `audit`.

```sh
zero build --profile release-small examples/hello.0
zero size --json --profile tiny examples/hello.0
```

Use `zero size --json` to explain retained functions, sections, literals, runtime shims, imports, debug metadata, and optimization hints.

## Ship

`zero ship` produces a release preview:

```sh
zero ship --json --target linux-musl-x64 examples/hello.0 \
  --out .zero/ship/hello
```

The preview includes artifact names, sizes, hashes, checksum file metadata, size report data, debug-symbol metadata, and target contract facts.

## Troubleshooting

- `zero doctor --json` checks host and target readiness.
- `BLD003` means an old backend flag was requested; remove it.
- Missing sysroot facts identify the required `ZERO_SYSROOT_*` variable.
- Unsupported targets fail explicitly instead of silently choosing another backend.
