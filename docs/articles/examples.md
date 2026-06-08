## Examples

The examples directory is the best hands-on path through graph-first Zero. Most
examples are checked-in `.graph` inputs or graph-first packages with
`zero.graph` stores; sibling `.0` files, when present, are human-readable
projections for review. Run, inspect, and patch the graph input from the
repository root instead of editing projection files.

Start by inspecting the graph, then check or build it:

Start here:

```sh
bin/zero query examples/hello.graph
bin/zero check examples/hello.graph
bin/zero build --emit exe --target linux-musl-x64 examples/add.graph --out .zero/out/add
./.zero/out/add
```

Tiny profile smoke:

```sh
bin/zero build --release tiny --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-tiny
bin/zero size --json --release tiny --target linux-musl-x64 --out .zero/out/hello-tiny examples/hello.graph
```

The current compiler can produce a tiny hosted/musl-style artifact for hello
world. This is not a complete no-libc runtime.

Build profiles:

```sh
bin/zero build --json --profile debug --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-debug
bin/zero build --json --profile fast --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-fast
bin/zero build --json --profile small --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-small
bin/zero size --json --profile tiny --target linux-musl-x64 examples/fixed-vec.graph
```

Build JSON reports `profileSemantics` and `profileBudget`. Size JSON adds
`sizeBreakdown`, `retentionReasons`, and `optimizationHints`.

Direct helper audit:

```sh
bin/zero size --json --target linux-musl-x64 examples/fixed-vec.graph
```

The direct size and graph reports show which helpers are retained for
fixed-capacity shapes. They also make the absence of vtables, method registries,
generic registries, hidden allocator machinery, and reflection visible.

Cross-compile smoke:

```sh
bin/zero build --release tiny --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-linux-musl
bin/zero build --release tiny --target win32-x64.exe examples/hello.graph --out .zero/out/hello-win32
bin/zero size --json --release tiny --target linux-musl-x64 --out .zero/out/hello-linux-musl examples/hello.graph
```

On macOS hosts these commands produce Linux and Windows-style output artifacts
through direct emitters. `zero size --json` provides the target report and
artifact size without executing the foreign binary.

Core graph examples:

- `examples/hello.graph`: `World`, stdout, `check`, and `raises`.
- `examples/point.graph`: types, literals, fields, and helper functions.
- `examples/result-choice.graph`: enums, choices, payload binding, and `match`.
- `examples/fixed-vec.graph`: field defaults, static value parameters, constructor-style methods, and receiver calls.
- `examples/fallibility.graph`: named errors and explicit error sets.
- `examples/memory-primitives.graph`: spans, fixed buffers, references, `Maybe<T>`, and allocator vocabulary.
- `examples/allocator-collections.graph`: `NullAlloc`, fixed-buffer allocation, `Vec`, fixed-storage collections, and `zero mem --json` allocator budget reporting.
- `examples/compile-time-v1.graph`: bounded `meta`, target/type reflection facts, Bool and enum static values, and compile-time JSON metadata.
- `examples/ownership-cleanup.graph`: `owned<T>` cleanup, canonical `drop`, and `defer` at lexical scope exit.
- `examples/std-math.graph`: pure fixed-width integer helpers and number-theory routines.
- `examples/std-path-io.graph`: fixed-buffer `std.path` helpers and caller-owned `std.io` buffers.
- `examples/grep-scan.graph`: line-oriented scanning over byte spans with `std.io` and `std.str`.
- `examples/std-str.graph`: allocation-free byte-string helpers over spans and caller-owned storage.
- `examples/std-testing-log.graph`: test-block predicates and explicit-buffer structured log output.
- `examples/std-text-format-parse.graph`: ASCII helpers, runtime parsing, caller-buffer formatting, and UTF-8 validation.
- `examples/std-data-formats.graph`: codec encode/decode, JSON lookup/writing, and URL query helpers over caller-owned buffers.
- `examples/std-json-bytes.graph`: byte-span JSON validation, parsing, and token streaming.
- `examples/std-http-json.graph`: hosted HTTP request envelope into caller-owned storage followed by byte-span JSON parsing.
- `examples/std-http-request.graph`: hosted HTTP request envelope with custom method, headers, and request body.
- `examples/std-http-headers.graph`: hosted HTTP request envelope, response buffer, and header-value lookup.
- `examples/json-api-client.graph`: hosted JSON API client with request-envelope writing and response-body parsing.
- `examples/json-api-router.graph`: dependency-free JSON API request parsing and response-envelope writing.
- `examples/crm-api/`: larger CRM API router package with account, contact, deal, activity, and search routes over HTTP request envelopes, stored as a binary repository graph.
- `examples/std-platform.graph`: `std.time`, `std.rand`, `std.proc`, and `std.crypto` capability-shaped helpers.
- `examples/cli-file.graph`: args, env, file writes, stdout, and stderr.
- `examples/cli-config.graph`: option parsing, environment fallback, and JSON output checks.
- `examples/file-copy.graph`: `Fs`, explicit scratch storage, and hosted file copy.
- `examples/zero-hash/`: file checksum CLI with args fallback, fixed buffers, hosted reads, and CRC-32 over bytes.
- `examples/readall-cli/`: fixed-buffer allocator use and owned byte-buffer reads.
- `examples/resource-cli/`: package-local modules, resource cleanup, and hosted filesystem capability use.
- `examples/memory-package/`: target-neutral package helper checks without hosted filesystem dependencies.
- `examples/error-tour/`: broken examples, explanations, and canonical repairs for common diagnostics.
- `examples/agent-repair-demo/`: a scripted agent loop that checks JSON diagnostics, explains the code, plans a repair, applies the edit, and re-runs check.

Use the index in `examples/README.md` for the full learning order and copyable commands.

Native Workflow Coverage:

- arguments and environment: `examples/cli-file.graph` reads `std.args` and `std.env`.
- filesystem resources: `examples/zero-hash/` seeds and reads a hosted file through explicit capabilities.
- deterministic exit status: `examples/direct-exe-return.graph` builds to a tiny direct native executable that returns `42`.
- unhandled error exit path: `examples/direct-unhandled-error-exit.graph` keeps the fallible exit route visible.

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
bin/zero inspect --json examples/zero-hash
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

## Larger HTTP API: `crm-api`

`examples/crm-api/` is a larger graph-first API-router package. It compiles
from the binary repository graph at `examples/crm-api/zero.graph`; the `.0`
files under `src/` are synced source projections for human review. The package
models a CRM request handler over Zero's HTTP envelope helpers and includes
more than ten route branches:

- account CRUD routes
- contact CRUD routes
- deal CRUD routes
- activity list/create routes
- health and search routes

The graph-runnable update/delete routes use explicit POST action paths such as
`/crm/deals/42/update` and `/crm/deals/42/delete`; this keeps the example
inside the current executable graph backend while still modeling CRUD behavior.

Check command:

```sh
bin/zero check examples/crm-api
```

Build and run commands:

```sh
bin/zero build --emit exe --profile debug --out /tmp/zero-crm-api examples/crm-api
/tmp/zero-crm-api $'GET /health\n\n'
```

Expected output:

```text
HTTP/1.1 200 OK
content-type: application/json
content-length: 27

{"ok":true,"service":"crm"}
```
