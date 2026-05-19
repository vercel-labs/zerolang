## What Targets Mean Today

The current compiler has a small explicit target table. Inspect it with:

```sh
zero targets
```

The JSON includes:

- `schemaVersion`
- current `host`
- each target's `hosted` flag
- aliases
- mapped C target
- capabilities

## Host Capabilities

Only the current host target exposes the full hosted capability set:

- `args`
- `env`
- `fs`
- `memory`
- `net`
- `proc`
- `rand`
- `stdio`
- `time`

Non-host targets expose only the capabilities listed for that target in
`zero targets --json`. Several non-host targets include filesystem,
arguments, environment, time, or random support, but no non-host target
currently exposes `net`.

The `net` capability is a target gate for current `std.net` metadata and
outbound `std.http.fetch(...)`. `std.http.parseMethod(...)` remains a
metadata-only helper that does not require `net`. Outbound HTTP is available on
direct Darwin arm64 and Linux x64 host executable paths.

`zero targets --json` includes an `httpRuntime` object for each target. On a
supported host target it reports the curl provider, the platform-or-C-library
TLS boundary, TLS verification state, the curl system library, and the
`ZERO_HTTP_TEST_CA_BUNDLE` custom CA override.

The smallest common target-neutral subset remains:

- `memory`
- `stdio`

This means hosted `std.fs` examples are valid on targets that declare `fs`.
Memory-only packages can still build for target-neutral outputs.

## Hosted File I/O

This succeeds on the host target:

```sh
zero check examples/resource-cli
```

The same hosted filesystem surface fails clearly on a non-host target:

```sh
zero check --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```

The diagnostic is `TAR002` with repair id `choose-target-with-required-capability`.

## Network Metadata

This succeeds on the host target:

```sh
zero check conformance/native/pass/std-net-http-breadth.0
```

The check exercises `std.net` and `std.http` metadata. It expects
`std.net.connect(...)` and `std.net.listen(...)` to return absent handles.
`std.http.fetch(...)` performs outbound HTTP on supported host executable
paths, writing response metadata, headers, and body into caller-owned storage.
`std.http.headerValue(...)` can locate a named response header value inside
that buffer. There is no socket read/write API, structured header collection,
or streaming body API in the current public surface.

The same network surface fails clearly on a target without `net`:

```sh
zero check --json --target linux-musl-x64 conformance/check/fail/target-net-unsupported.0
```

## Target-Neutral Memory

`std.mem.copy` and `std.mem.fill` do not require hosted filesystem support:

```sh
zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
```

Use graph and size JSON to inspect target facts:

```sh
zero graph --json --target linux-musl-x64 examples/memory-package
zero size --json --target linux-musl-x64 examples/memory-package
```

Both outputs include `requiresCapabilities`, `targetSupport`, and `stdlibHelpers`.

## Repair Commands

Use `zero explain` for human and JSON explanations:

```sh
zero explain TAR002
zero explain --json TAR002
```

Use fix-plan mode to inspect the canonical repair without editing files:

```sh
zero fix --plan --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```
