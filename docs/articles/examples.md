## Examples

The examples directory is the best hands-on path through Zero. Each example is
small, deterministic, and intended to be checked or built from the repository
root.

Start here:

```sh
bin/zero check examples/hello.0
bin/zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
./.zero/out/add
```

Tiny profile smoke:

```sh
bin/zero build --release tiny --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-tiny
bin/zero size --json --release tiny --target linux-musl-x64 --out .zero/out/hello-tiny examples/hello.0
```

The current compiler can produce a tiny hosted/musl-style artifact for hello
world. This is not a complete no-libc runtime.

Build profiles:

```sh
bin/zero build --json --profile debug --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-debug
bin/zero build --json --profile fast --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-fast
bin/zero build --json --profile small --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-small
bin/zero size --json --profile tiny --target linux-musl-x64 examples/fixed-vec.0
```

Build JSON reports `profileSemantics` and `profileBudget`. Size JSON adds
`sizeBreakdown`, `retentionReasons`, and `optimizationHints`.

Direct helper audit:

```sh
bin/zero size --json --target linux-musl-x64 examples/fixed-vec.0
```

The direct size and graph reports show which helpers are retained for
fixed-capacity shapes. They also make the absence of vtables, method registries,
generic registries, hidden allocator machinery, and reflection visible.

Cross-compile smoke:

```sh
bin/zero build --release tiny --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-linux-musl
bin/zero build --release tiny --target win32-x64.exe examples/hello.0 --out .zero/out/hello-win32
bin/zero size --json --release tiny --target linux-musl-x64 --out .zero/out/hello-linux-musl examples/hello.0
```

On macOS hosts these commands produce Linux and Windows-style output artifacts
through direct emitters. `zero size --json` provides the target report and
artifact size without executing the foreign binary.

Core examples:

- `examples/hello.0`: `World`, stdout, `check`, and `!`.
- `examples/point.0`: types, literals, fields, and helper functions.
- `examples/result-choice.0`: enums, choices, payload binding, and `match`.
- `examples/fixed-vec.0`: field defaults, static value parameters, constructor-style methods, and receiver calls.
- `examples/fallibility.0`: named errors and explicit error sets.
- `examples/memory-primitives.0`: spans, fixed buffers, references, `Maybe<T>`, and allocator vocabulary.
- `examples/allocator-collections.0`: `NullAlloc`, fixed-buffer allocation, `Vec`, empty map/set metadata, and `zero mem --json` allocator budget reporting.
- `examples/compile-time-v1.0`: bounded `meta`, target/type reflection facts, Bool and enum static values, and compile-time JSON metadata.
- `examples/ownership-cleanup.0`: `owned<T>` cleanup, canonical `drop`, and `defer` at lexical scope exit.
- `examples/std-path-io.0`: fixed-buffer `std.path` helpers and caller-owned `std.io` buffers.
- `examples/std-data-formats.0`: `std.codec` encoders and basic explicit-allocator `std.json` helpers.
- `examples/std-json-bytes.0`: byte-span JSON validation, parsing, and token streaming.
- `examples/std-http-json.0`: hosted HTTP request envelope into caller-owned storage followed by byte-span JSON parsing.
- `examples/std-http-request.0`: hosted HTTP request envelope with custom method, headers, and request body.
- `examples/std-http-headers.0`: hosted HTTP request envelope, response buffer, and header-value lookup.
- `examples/std-platform.0`: `std.time`, `std.rand`, `std.proc`, and `std.crypto` capability-shaped helpers.
- `examples/cli-file.0`: args, env, file writes, stdout, and stderr.
- `examples/file-copy.0`: `Fs`, `owned<File>`, read/write resource capabilities, and automatic close.
- `examples/zero-hash/`: file checksum CLI with args fallback, fixed buffers, hosted reads, and CRC-32 over bytes.
- `examples/readall-cli/`: fixed-buffer allocator use and owned byte-buffer reads.
- `examples/resource-cli/`: package-local modules, resource cleanup, and hosted filesystem capability use.
- `examples/memory-package/`: target-neutral package helper checks without hosted filesystem dependencies.
- `examples/error-tour/`: broken examples, explanations, and canonical repairs for common diagnostics.
- `examples/agent-repair-demo/`: a scripted agent loop that checks JSON diagnostics, explains the code, plans a repair, applies the edit, and re-runs check.

Use the index in `examples/README.md` for the full learning order and copyable commands.

Native Workflow Coverage:

- arguments and environment: `examples/cli-file.0` reads `std.args` and `std.env`.
- filesystem resources: `examples/zero-hash/` seeds and reads a hosted file through explicit capabilities.
- deterministic exit status: `examples/direct-exe-return.0` builds to a tiny direct native executable that returns `42`.
- unhandled error exit path: `examples/direct-unhandled-error-exit.0` keeps the fallible exit route visible.

## Larger CLI: `zero-hash`

`examples/zero-hash/` is a larger CLI example. It exercises:

- args and hosted filesystem access
- fixed-buffer allocation
- memory helpers
- codec/hash-style APIs
- fallibility and owned resources

It stays useful without a large standard library.

Build command:

```sh
bin/zero build --emit exe --target linux-musl-x64 examples/zero-hash --out .zero/out/zero-hash
```

Run command:

```sh
pnpm run native:test
```

Expected output:

```text
zero-hash ok
```

Size output:

```sh
bin/zero size --json --target linux-musl-x64 examples/zero-hash --out .zero/out/zero-hash-size.json
```

Inspect metadata:

```sh
bin/zero graph --json examples/zero-hash
```

The graph and size reports show the helper use behind `zero-hash`: args,
fixed-buffer allocation, CRC-32 bytes, and hosted reads. They also show that the
program does not retain hidden runtime machinery such as global allocation,
registries, reflection, or heap allocation.

Benchmark case:

```sh
ZERO_BENCH_RUNS=1 pnpm run bench
```

The benchmark report includes the Zero-only `zero-hash` case with expected output checking.

Cross-target status:

```sh
bin/zero check --json --target linux-musl-x64 examples/zero-hash
```

`zero-hash` intentionally uses hosted filesystem APIs. Use
`examples/memory-package/` for target-neutral direct builds that avoid hosted
filesystem requirements.

Use `examples/memory-package/` for target-neutral cross-target direct builds.
