## C Interop Guide

Zero supports a small, explicit C ABI export surface in graph inputs. Exported
C functions are graph declarations that may render as the projection snippet
below; agents should inspect ABI facts with `zero abi`, `zero inspect`, and
diagnostics before patching the graph.

```zero
export c fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

Check the ABI surface:

```sh
bin/zero check conformance/native/pass/c-abi-export.graph
bin/zero abi dump --json conformance/native/pass/c-abi-export.graph
```

The ABI dump reports exported C symbols and a small generated header text block.
For `conformance/native/pass/c-abi-export.graph`, `generatedHeader.available` is
`true` and the header contains the `zero_add` declaration.

Invalid export surfaces fail before C emission; use the diagnostic explanation
when you need the current repair contract:

```sh
bin/zero explain ABI001
```

Header imports expose typed metadata and scalar C functions are callable through
the declared import alias:

```sh
bin/zero inspect --json --target linux-musl-x64 conformance/check/pass/c-header-import.graph
```

The graph JSON exposes `cImports[].typedModel` with imported functions,
constants, structs, enums, and typedefs.

```zero
extern c "/tmp/zero_ext.h" as c

export c fn main() -> i32 {
    return c.zero_ext_add(20, 22)
}
```

Callable imports are limited to direct scalar ABI types today: `Void`, `Bool`,
`u8`, `u16`, `usize`, `i32`, `u32`, `i64`, and `u64`. Pointer, array, struct,
and unsupported-width parameters should be wrapped behind a small C shim.

It also includes a cache object keyed by:

- header hash
- target
- ABI
- flags hash
- sysroot fingerprint

External C calls require target library audit facts. `zero inspect --json` reports
each `cLibraries[].linkPlan` with include paths, library paths, sysroot status,
target ABI, and host discovery status.

Executable builds with direct extern C calls require matching package link
metadata in `zero.toml`: the imported header must appear in
`c.libs.*.headers`, and that library must provide `lib` or `link` inputs.
Missing link inputs or unsafe system library names in `link` report `CIMP005`
before the linker runs.

Cross builds must use package-relative vendored headers/libraries or an
explicit target sysroot. They cannot silently reuse host include or library
paths.

Unsafe foreign-target discovery fails with `CIMP003` before code generation:

```sh
bin/zero build --json --target linux-musl-x64 conformance/c/host-leak-package --out .zero/out/host-leak-package
```
