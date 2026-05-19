# Zero Examples

Use these examples as the hands-on path through Zero. Start at the top, run `bin/zero check`, and open the matching docs-site article when you want an explanation.

## Profile And Size Checks

Use `hello.0` and `fixed-vec.0` to compare profile contracts and size metadata:

```sh
bin/zero build --json --profile debug --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-debug
bin/zero build --json --profile fast --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-fast
bin/zero build --json --profile small --target linux-musl-x64 examples/hello.0 --out .zero/out/hello-small
bin/zero size --json --profile tiny --target linux-musl-x64 examples/fixed-vec.0
```

Build JSON reports `profileSemantics` and `profileBudget`. Size JSON adds `sizeBreakdown`, `retentionReasons`, and `optimizationHints`.

## First Programs

| Example | What it teaches | Try it |
| --- | --- | --- |
| `hello.0` | `pub fun main`, `World`, `check`, stdout | `bin/zero check examples/hello.0` |
| `hello-let.0` | immutable `let` bindings | `bin/zero check examples/hello-let.0` |
| `add.0` | helper functions, `return`, `if` / `else` | `bin/zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add` |
| `direct-wasm-add.0` | experimental direct wasm backend for exported primitive arithmetic | `bin/zero build --emit wasm --target wasm32-wasi examples/direct-wasm-add.0 --out .zero/out/direct-wasm-add.wasm` |
| `direct-u8-helper-call.0` | direct wasm signed LEB literals, byte arrays, and helper calls | `bin/zero build --emit wasm --target wasm32-web examples/direct-u8-helper-call.0 --out .zero/out/direct-u8-helper-call` |
| `direct-array-bounds-trap.0` | direct wasm stack-memory bounds traps | `bin/zero build --emit wasm --target wasm32-web examples/direct-array-bounds-trap.0 --out .zero/out/direct-array-bounds-trap` |
| `direct-string-len.0` | direct wasm string literal length for compiler token scans | `bin/zero build --emit wasm --target wasm32-web examples/direct-string-len.0 --out .zero/out/direct-string-len` |
| `direct-string-literal.0` | direct wasm readonly string data segments and byte loads | `bin/zero build --emit wasm --target wasm32-web examples/direct-string-literal.0 --out .zero/out/direct-string-literal` |
| `direct-span-read.0` | direct wasm readonly string slices as byte-span views | `bin/zero build --emit wasm --target wasm32-web examples/direct-span-read.0 --out .zero/out/direct-span-read` |
| `direct-string-eql.0` | direct wasm byte-span equality over readonly string views | `bin/zero build --emit wasm --target wasm32-web examples/direct-string-eql.0 --out .zero/out/direct-string-eql` |
| `direct-byte-view-locals.0` | direct wasm local `String`/`Span<u8>` pointer-length byte views | `bin/zero build --emit wasm --target wasm32-web examples/direct-byte-view-locals.0 --out .zero/out/direct-byte-view-locals` |
| `direct-mutspan-len.0` | direct wasm local `MutSpan<u8>` pointer-length byte views | `bin/zero build --emit wasm --target wasm32-web examples/direct-mutspan-len.0 --out .zero/out/direct-mutspan-len` |
| `direct-memory-peek.0` | direct wasm explicit byte reads from exported linear memory | `bin/zero build --emit wasm --target wasm32-web examples/direct-memory-peek.0 --out .zero/out/direct-memory-peek` |
| `direct-byte-copy-fill.0` | direct wasm mutable byte copy/fill over fixed buffers | `bin/zero build --emit wasm --target wasm32-web examples/direct-byte-copy-fill.0 --out .zero/out/direct-byte-copy-fill` |
| `direct-alloc-bump.0` | direct wasm explicit `FixedBufAlloc` bump allocation over caller storage | `bin/zero build --emit wasm --target wasm32-web examples/direct-alloc-bump.0 --out .zero/out/direct-alloc-bump` |
| `direct-alloc-overflow.0` | direct wasm fixed-buffer allocation overflow returns `Maybe.none` without hidden heap growth | `bin/zero build --emit wasm --target wasm32-web examples/direct-alloc-overflow.0 --out .zero/out/direct-alloc-overflow` |
| `direct-token-shape.0` | direct wasm stack layout for shape literals, field loads, defaults, and field stores | `bin/zero build --emit wasm --target wasm32-web examples/direct-token-shape.0 --out .zero/out/direct-token-shape` |
| `direct-enum-match.0` | direct wasm compact enum cases and exhaustive match branches without matcher tables | `bin/zero build --emit wasm --target wasm32-web examples/direct-enum-match.0 --out .zero/out/direct-enum-match` |
| `direct-raises-basic.0` | direct wasm packed error-result propagation for `raise` and `check` | `bin/zero build --emit wasm --target wasm32-web examples/direct-raises-basic.0 --out .zero/out/direct-raises-basic` |
| `direct-rescue-basic.0` | direct wasm local `rescue` fallback over a raised error | `bin/zero build --emit wasm --target wasm32-web examples/direct-rescue-basic.0 --out .zero/out/direct-rescue-basic` |
| `direct-byte-buf.0` | direct wasm monomorphic byte buffer push, length, capacity, and overflow checks | `bin/zero build --emit wasm --target wasm32-web examples/direct-byte-buf.0 --out .zero/out/direct-byte-buf` |
| `direct-generic-identity.0` | direct wasm explicit generic function specialization without runtime metadata | `bin/zero build --emit wasm --target wasm32-web examples/direct-generic-identity.0 --out .zero/out/direct-generic-identity` |
| `direct-generic-fixedbuf.0` | direct wasm generic storage shape with concrete type and static value arguments | `bin/zero build --emit wasm --target wasm32-web examples/direct-generic-fixedbuf.0 --out .zero/out/direct-generic-fixedbuf` |
| `direct-generic-vec.0` | direct wasm generic fixed-capacity vector layout for byte, token, and AST-node element kinds | `bin/zero build --emit wasm --target wasm32-web examples/direct-generic-vec.0 --out .zero/out/direct-generic-vec` |
| `direct-i64-return.0` | direct ELF64 object backend support for i64/u64 values | `bin/zero build --emit obj --target linux-musl-x64 examples/direct-i64-return.0 --out .zero/out/direct-i64-return.o` |
| `direct-byte-view-reloc.0` | direct ELF64 readonly byte-view relocations for string-backed span locals | `bin/zero build --emit obj --target linux-musl-x64 examples/direct-byte-view-reloc.0 --out .zero/out/direct-byte-view-reloc.o` |
| `functions.0` | calling functions and ignoring return values | `bin/zero check examples/functions.0` |
| `branch.0` | booleans and branches | `bin/zero check examples/branch.0` |
| `countdown.0` | `while` loop syntax | `bin/zero check examples/countdown.0` |

## Data And Types

| Example | What it teaches | Try it |
| --- | --- | --- |
| `point.0` | `shape`, shape literals, field access | `bin/zero check examples/point.0` |
| `result-choice.0` | `enum`, payload `choice`, exhaustive `match`, payload binding | `bin/zero check examples/result-choice.0` |
| `primitive-language-gaps.0` | fixed arrays, `let mut`, assignment | `bin/zero check examples/primitive-language-gaps.0` |
| `memory-primitives.0` | `Span`, `Maybe`, references, allocator vocabulary, `std.mem` spans | `bin/zero check examples/memory-primitives.0` |
| `allocator-collections.0` | `NullAlloc`, explicit allocator handles, fixed-buffer allocation, `Vec`, and empty map/set metadata without a global heap | `bin/zero check examples/allocator-collections.0 && bin/zero mem --json examples/allocator-collections.0` |
| `const-arithmetic.0` | top-level deterministic `const` values and arithmetic | `bin/zero check examples/const-arithmetic.0` |
| `compile-time-v1.0` | bounded `meta`, target/type reflection facts, Bool and enum static values, and compile-time JSON metadata | `bin/zero check --json examples/compile-time-v1.0` |
| `generic-pair.0` | multi-parameter generic shapes and generic function returns | `bin/zero check examples/generic-pair.0` |
| `static-value-params.0` | integer static value parameters and fixed-capacity generic storage | `bin/zero check examples/static-value-params.0` |
| `fixed-vec.0` | field defaults, constructor-style shape methods, receiver calls, `Self`, and static capacity | `bin/zero check examples/fixed-vec.0` |
| `type-alias.0` | `type Alias = ExistingType` as compile-time spelling | `bin/zero check examples/type-alias.0` |
| `static-method.0` | static shape method namespace calls without dispatch | `bin/zero check examples/static-method.0` |
| `static-interface.0` | static interface constraints over generic functions with direct calls | `bin/zero check examples/static-interface.0` |
| `fallibility.0` | `raise`, `check`, and explicit `raises { ... }` error sets | `bin/zero build --emit exe --target linux-musl-x64 examples/fallibility.0 --out .zero/out/fallibility && ./.zero/out/fallibility` |
| `ownership-cleanup.0` | `owned<T>` cleanup, canonical `drop`, and `defer` at lexical scope exit | `bin/zero build --emit exe --target linux-musl-x64 examples/ownership-cleanup.0 --out .zero/out/ownership-cleanup && ./.zero/out/ownership-cleanup` |

## Standard Library

| Example | What it teaches | Try it |
| --- | --- | --- |
| `codec-varint.0` | `use std.codec`, varint length, CRC-32 | `bin/zero check examples/codec-varint.0` |
| `parse-cursor.0` | `use std.parse`, scanner predicates | `bin/zero check examples/parse-cursor.0` |
| `std-path-io.0` | `std.path` fixed-buffer path helpers and `std.io` caller-owned buffers | `bin/zero check examples/std-path-io.0` |
| `std-data-formats.0` | `std.codec` encoders and basic explicit-allocator `std.json` helpers | `bin/zero check examples/std-data-formats.0` |
| `std-json-bytes.0` | byte-span JSON validation, parsing, and token streaming | `bin/zero run --out /tmp/zero-json-bytes examples/std-json-bytes.0` |
| `std-http-json.0` | hosted HTTP request envelope into caller storage, then byte-span JSON parsing | `bin/zero check examples/std-http-json.0` |
| `std-http-request.0` | hosted HTTP request envelope with custom method, headers, and body | `bin/zero check examples/std-http-request.0` |
| `std-http-headers.0` | hosted HTTP request envelope, response buffer, and header-value lookup | `bin/zero check examples/std-http-headers.0` |
| `std-platform.0` | `std.time`, `std.rand`, `std.proc`, and `std.crypto` capability-shaped helpers | `bin/zero check examples/std-platform.0` |
| `cli-file.0` | `std.args`, `std.env`, byte-span file writes, stderr/stdout | `bin/zero build --emit wasm --target wasm32-wasi examples/cli-file.0 --out .zero/out/cli-file` |
| `file-copy.0` | `Fs`, `owned<File>`, read/write resource capabilities, automatic close | `bin/zero build --emit wasm --target wasm32-wasi examples/file-copy.0 --out .zero/out/file-copy` |
| `zero-hash/` | File checksum CLI with args, fixed buffers, `readAll`, and CRC-32 bytes | `bin/zero build --emit wasm --target wasm32-wasi examples/zero-hash --out .zero/out/zero-hash` |

## Native Workflow Coverage

These examples are the small native workflow set used by docs and tests:

| Surface | Example | Try it |
| --- | --- | --- |
| arguments and environment | `cli-file.0` | `ZERO_CLI_FILE_MODE=verbose bin/zero build --emit wasm --target wasm32-wasi examples/cli-file.0 --out .zero/out/cli-file` |
| filesystem resources | `zero-hash/` | `bin/zero check examples/zero-hash` |
| deterministic exit status | `direct-exe-return.0` | `bin/zero build --emit exe --target linux-musl-x64 examples/direct-exe-return.0 --out .zero/out/direct-exe-return` |
| unhandled error exit path | `direct-unhandled-error-exit.0` | `bin/zero check examples/direct-unhandled-error-exit.0` |

## Interop And Packages

| Example | What it teaches | Try it |
| --- | --- | --- |
| `config-shape.0` | `extern c`, `extern shape`, C-shaped data | `bin/zero check examples/config-shape.0` |
| `systems-package/` | `zero.json`, multiple source files, `defer`, std helpers | `bin/zero check examples/systems-package` |
| `readall-cli/` | package-local imports, named errors, `std.fs.readAll`, explicit fixed-buffer allocation | `bin/zero check examples/readall-cli` |
| `batch3-cli/` | module graph metadata, local `rescue`, path helpers, named fs errors, explicit allocation | `bin/zero check examples/batch3-cli` |
| `resource-cli/` | args/env fallback, path joins, `std.mem.copy`/`fill`, named-error owned-file resources | `bin/zero check examples/resource-cli` |
| `memory-package/` | target-neutral package imports and byte-span helper checks without hosted file I/O | `bin/zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package` |
| `direct-package-call-order/` | direct wasm package merge order and cross-module helper calls | `bin/zero build --emit wasm --target wasm32-web examples/direct-package-call-order --out .zero/out/direct-package-call-order` |
| `error-tour/` | copyable failing commands and repaired fixtures for common diagnostics | `bin/zero explain TAR002` |

## Web

| Example | What it teaches | Try it |
| --- | --- | --- |
| `web-response.0` | single-file web-style `GET` handler | `bin/zero check examples/web-response.0` |
| `web/hello/` | `[targets.web]`, route manifest metadata, `Request`, and `Response` | `bin/zero routes --json examples/web/hello` |

## Build A Runnable Program

Most examples are designed for `check`. To build and run an executable, use a CLI entry point:

```sh
bin/zero dev --json --target linux-musl-x64 examples/add.0
bin/zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add
bin/zero ship --target linux-musl-x64 examples/add.0 --out .zero/ship/add
./.zero/out/add
```

Expected output:

```text
math works
```

The larger CLI path is `examples/zero-hash/`. It seeds a small file, reads it through a fixed-buffer allocator, computes CRC-32 over the read bytes without heap allocation, and prints:

```text
zero-hash ok
```

For target-neutral direct cross builds, use `examples/memory-package/`. It exercises a multi-file package and byte-span helper checks without `std.fs`, so it can be checked and built against non-host targets:

```sh
bin/zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
```

## More Guidance

Run the docs site with:

```sh
npm run docs:dev
```

Start with Getting Started, then Learn Zero.
