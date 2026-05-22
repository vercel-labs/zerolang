## Building From Source

Use this path when you want to try Zero from a checkout or work on the compiler.

```sh
pnpm install
make -C native/zero-c
bin/zero --version
```

`make` builds the local compiler into `.zero/bin/zero`. The repository wrapper
`bin/zero` uses that local build.

## Quick Command Loop

```sh
bin/zero check examples/hello.0
bin/zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
./.zero/out/add
```

## Inspect Code

Inspect package/module structure:

```sh
bin/zero graph --json examples/systems-package
```

Inspect artifact size metadata:

```sh
bin/zero size --json examples/point.0
```

List known targets:

```sh
bin/zero targets
```

Explain diagnostics and inspect repair plans without editing files:

```sh
bin/zero explain TAR002
bin/zero fix --plan --json conformance/native/fail/mem-copy-immutable-dst.0
```

## Native Targets

The compiler currently supports direct executable output for the documented
native target names:

- `darwin-arm64`
- `darwin-x64`
- `linux-arm64`
- `linux-musl-arm64`
- `linux-musl-x64`
- `linux-x64`
- `win32-arm64.exe`
- `win32-x64.exe`

```sh
bin/zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add-linux-musl
bin/zero build --emit exe --target win32-x64.exe examples/hello.0 --out .zero/out/hello-win32
```

Unsupported target or feature requests report diagnostics instead of silently
choosing another backend.

## Direct Objects

Build a small object artifact for a foreign native target:

```sh
bin/zero build --emit obj --target darwin-arm64 examples/direct-call-add.0 --out .zero/out/direct-call-add.o
```

Use `bin/zero check --json --emit obj --target <target> <input>` before a build
when an agent needs structured readiness data without writing artifacts.

## Current Language Subset

The compiler supports the command-line language subset used by the examples:

- multi-file manifest packages
- functions and typed parameters
- typed `let` and `mut`
- fixed array literals, repeat literals, and assignment
- `defer`
- `match`
- `check`, `ret`, `if`, `else`, and `while`
- calls and member calls
- strings, numbers, booleans, and binary operators
- `type` and `extern type`
- type literals and direct field access
- `enum`
- payload and no-payload `choice` tags
- `owned<T>`, `Span<T>`, `ref<T>`, and `mutref<T>` checks for the documented subset
- early `std.mem`, `std.codec`, `std.parse`, `std.fs`, and platform helper surfaces

## Validate A Checkout

```sh
pnpm run docs:test
pnpm run conformance
pnpm run native:test
pnpm run command-contracts
```

Run local benchmark smoke coverage:

```sh
pnpm run bench
```

The TypeScript code in this repository is support tooling for docs, tests,
benchmarks, and editor integration. It is not a separate TypeScript compiler
implementation.
