## C Interop Guide

Zero supports a small, explicit C ABI export surface. Exported C functions use
primitive scalar parameters or explicit references accepted by the native
checker.

```zero
export c fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

Check the ABI surface:

```sh
bin/zero check conformance/native/pass/c-abi-export.0
bin/zero abi dump --json conformance/native/pass/c-abi-export.0
```

The ABI dump reports exported C symbols and a small generated header text block.
For `conformance/native/pass/c-abi-export.0`, `generatedHeader.available` is
`true` and the header contains the `zero_add` declaration.

Invalid export surfaces fail before C emission:

```sh
bin/zero check --json conformance/native/fail/bad-c-export.0
```

Header imports expose typed metadata and scalar C functions are callable through
the declared import alias:

```sh
bin/zero graph --json --target linux-musl-x64 conformance/check/pass/c-header-import.0
```

The graph JSON exposes `cImports[].typedModel` with imported functions,
constants, structs, enums, and typedefs.

```zero
extern c "/tmp/zero_ext.h" as c

pub fn main() -> i32 {
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

External C calls require target library audit facts. `zero graph --json` reports
each `cLibraries[].linkPlan` with include paths, library paths, sysroot status,
target ABI, and host discovery status.

Cross builds must use package-relative vendored headers/libraries or an
explicit target sysroot. They cannot silently reuse host include or library
paths.

Unsafe foreign-target discovery fails with `CIMP003` before code generation:

```sh
bin/zero build --json --target linux-musl-x64 conformance/c/host-leak-package --out .zero/out/host-leak-package
```
