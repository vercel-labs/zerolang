## Reading An Error

A typical error looks like this:

```text
error[NAM003]: Unknown identifier
  unknown identifier 'message'
  examples/hello.0:2:27

  2 |     check world.out.write(message)
    |                           ^^^^^^^
  rule: Names must be declared before use in the current lexical scope.
  expected: local binding, parameter, function, builtin value
  actual: no visible symbol named 'message'
  fix: Introduce a local binding before this use (local-edit)
  explain: zero explain NAM003
```

Read it from top to bottom:

- The first line gives the stable code and short title.
- The message says what went wrong.
- The span points to the file, line, and column.
- The source excerpt marks the exact token.
- `rule`, `expected`, and `actual` explain the mismatch.
- `fix` gives the safest repair shape.
- `explain` points to deeper help.

In this case, the program tried to write `message` without declaring it. A local binding fixes it:

```zero
pub fn main Void world World !
  let message "hello from zero\n"
  check world.out.write message
```

## Plain Text By Default

Default diagnostics should be short and useful in terminal logs.

They should not include ANSI colors, bold styling, hyperlinks, OSC escapes, or
terminal control sequences by default. Those bytes bloat agent context and make
logs harder to compare.

Use:

```sh
zero check examples/hello.0
```

## JSON For Tools

JSON is explicit. Use `--json` for agents, CI, editors, deep dives, and tools that need stable structured data.

```sh
zero check --json examples/hello.0
```

The native JSON shape is versioned:

```json
{
  "schemaVersion": 1,
  "ok": false,
  "diagnostics": [
    {
      "severity": "error",
      "code": "NAM003",
      "message": "unknown identifier 'message'",
      "path": "examples/hello.0",
      "line": 2,
      "column": 27,
      "length": 7,
      "expected": "visible local, parameter, function, or builtin",
      "actual": "no visible symbol named 'message'",
      "help": "declare the name before using it",
      "fixSafety": "behavior-preserving",
      "repair": {
        "id": "manual-review",
        "summary": "Inspect the diagnostic fields and choose a repair manually."
      },
      "related": []
    }
  ]
}
```

## Fix Safety

Fixes carry safety labels so agents know when they can act:

- `format-only`: changes only formatting
- `behavior-preserving`: preserves program behavior
- `local-edit`: confined to the current local scope or file
- `api-changing`: changes function signatures, exported names, package APIs, or call sites
- `requires-human-review`: risky or ambiguous; show the plan but do not apply automatically

## Current Native Diagnostics

The native compiler keeps stable codes for implemented control-flow and type rules:

- `PAR100`: parser syntax failures such as missing braces, commas, or malformed type argument lists
- `NAM003`: unknown identifiers
- `NAM004`: duplicate names, wrong call arity, or generic type-name shadowing
- `IMP001`: unknown package-local imports, with repair id `fix-import-path`
- `IMP002`: package-local import cycles
- `IMP003`: duplicate public exports across imported modules
- `PKG001`: a local package dependency path does not contain `zero.json`
- `PKG002`: package dependencies form a cycle
- `PKG003`: one package name resolves to conflicting versions
- `PKG004`: a package dependency does not support the selected target
- `BLD002`: bad project manifest or unsupported manifest target shape
- `ERR002`: a caller's explicit error set is missing an error raised by a callee
- `ERR003`: a fallible call was used without `check` or `rescue`
- `ABI001`: unsupported C ABI export or extern layout surface
- `CIMP003`: a foreign-target C dependency would use host include paths, host library paths, or implicit host `pkg-config` discovery
- `BOR001`: lexical borrow conflicts, with JSON `borrowTrace.activeBorrows`
  entries naming each reported borrowed root, path, kind, live binding,
  declaration range when known, and repair shape. `borrowTrace.truncated` is
  true if the report hit the cap.
- `BOR002`: reference-origin escapes, including references returned from calls or stored through mutable parameter storage
- `OWN001`: owned value use after move, or generic containers that would own unconstrained generic payloads
- `TYP010`: conditions must be `Bool`
- `TYP002`: type mismatch in assignments, literals, returns, or type defaults
- `TYP011`: `null` requires a `Maybe<T>` context
- `TYP012`: `break` requires an enclosing loop
- `TYP013`: `continue` requires an enclosing loop
- `TYP014`: range loop bounds must be integer-compatible
- `TYP015`: an integer literal must use valid digits, separators, radix prefixes, and integer suffixes
- `TYP016`: an integer literal must fit the expected primitive integer width
- `TYP017`: `as` casts are limited to primitive numeric and byte `char` source and target types
- `TYP018`: a character literal must contain exactly one byte or a supported byte escape
- `TYP019`: a float literal must use `digits "." digits` with an optional exponent
- `TYP020`: a float literal must fit the expected primitive float width
- `TYP021`: indexing, slicing, and indexed assignment require a supported target for that operation
- `TYP022`: index expressions and present slice bounds must be integers
- `TYP023`: generic call type argument count mismatch, or type arguments on a non-generic function
- `TYP024`: generic inference found conflicting concrete types for one type parameter
- `TYP025`: generic type arguments could not be inferred from local call arguments
- `TYP026`: a type alias is duplicated, malformed, or cyclic
- `TYP027`: recursive generic call changes type arguments
- `PUB001`: a public declaration omitted required explicit API type metadata
- `MET001`: a parsed `meta` expression requested compile-time behavior this compiler slice cannot evaluate yet
- `IFC001`: an interface constraint is unknown or a concrete type argument has no static type body
- `IFC002`: a constrained concrete type is missing a required static interface method
- `IFC003`: a concrete static method has the wrong parameter count for an interface
- `IFC004`: a concrete static method has the wrong return type for an interface
- `IFC005`: a concrete static method has the wrong parameter type for an interface
- `STC001`: a static value parameter uses an unsupported non-integer type
- `STC002`: a static value argument is not an integer literal or deterministic top-level const
- `STC003`: an explicit static value argument conflicts with the value carried by an annotated type
- `SHM001`: a generic type method call cannot infer inherited type/static parameters
- `SHM002`: arguments to a generic type method imply conflicting `Self` instantiations
- `RCV001`: a receiver-style call names an unknown method or a static method without `self`
- `RCV002`: a receiver-style call needs an addressable receiver, or a mutable receiver for `mutref<Self>`
- `FLD001`: a type literal includes an unknown field
- `FLD002`: a type literal omitted a required field that has no default
- `TAR001`: the requested target name is not in `zero targets`
- `TAR002`: the selected target does not provide a capability required by the program
- Bounds check failures: native executables print `zero bounds check failed` and
  abort when an index, indexed assignment, or slice range is outside the base
  length.
- `MAT004`: a match arm can bind a payload only for a choice case that carries one

## Standard Library Diagnostics

Standard library modules use the same structured diagnostic contract as compiler diagnostics.

- `MEM001` reports malformed memory type forms such as `Maybe` without its required type argument.
- `std.parse`, `std.json`, and `std.env` diagnostics carry source spans where
  applicable.
- `std.time` diagnostics can use offset-only spans for single-token inputs.
- Standard library codes stay stable and package-local, while `zero explain <code>` provides human guidance and `--json` exposes structured fix metadata.

## Common Fix Plans

Hosted filesystem helpers are host-only in the current compiler. This fails clearly on non-host targets:

```sh
bin/zero check --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```

The diagnostic uses `TAR002`, `fixSafety: "requires-human-review"`, and repair
id `choose-target-with-required-capability`.

The canonical repair is to build for a target with the required capability or
move that code behind a target-specific entry point.

Writable byte helpers require mutable storage:

```zero
let dst [4]u8 [0, 0, 0, 0]
let src [4]u8 [122, 101, 114, 111]
let _copied std.mem.copy dst src
```

This reports `TYP009` with repair id `make-binding-mutable`. The canonical repair is:

```zero
mut dst [4]u8 [0, 0, 0, 0]
let src [4]u8 [122, 101, 114, 111]
let _copied std.mem.copy dst src
```

Named-error `std.fs` calls require explicit error flow:

```zero
let file std.fs.createOrRaise fs ".zero/out.txt"
```

This reports `ERR003` with repair id `check-or-rescue-fallible-call`.

Use `check` and include `NotFound`, `TooLarge`, and `Io` in the caller's
`![NotFound TooLarge Io]` set. Use `rescue` locally when the call should recover in
place.

Generic calls use local inference only. This fails because `T` would need to be both `i32` and `u8`:

```zero
fn first<T: Type> T left T right T
  ret left

let value i32 first 1 2_u8
```

The repair is to make the arguments agree or pass explicit type arguments with compatible values:

```zero
let value i32 first<i32> 1 2
```

Public constants need explicit API shape:

```zero
pub const answer 42
```

This reports `PUB001`. The behavior-preserving repair is:

```zero
pub const answer i32 42
```

C interop keeps host and target discovery separate. This fails for a foreign target because the manifest asks for host include/library discovery:

```sh
bin/zero build --json --target linux-musl-x64 conformance/c/host-leak-package --out .zero/out/host-leak-package
```

This reports `CIMP003` with repair id `configure-target-c-dependency`.

The canonical repair is to use package-relative vendored headers/libraries or
configure the target sysroot. Do not rely on host include paths, host library
paths, or host `pkg-config` discovery for cross-target builds.

Package dependency diagnostics are graph-level repairs:

| Code | Meaning |
| --- | --- |
| `PKG001` | A local dependency path is wrong or missing. |
| `PKG002` | Two package manifests depend on each other cyclically. |
| `PKG003` | The graph resolved one package name to multiple versions. |
| `PKG004` | The selected target is outside the dependency's target list. |

These all use `requires-human-review` because the correct repair may change
package topology or target support.

Type aliases are compile-time spellings and cannot cycle:

```zero
alias A B

alias B A
```

This reports `TYP026`. Point the alias at a concrete type such as `Span<u8>` or
remove the cycle.

Unsupported compile-time execution reports `MET001`, for example
`const os String meta target.os`, until target facts are implemented in the
native compiler.

Generic type methods must specialize from a concrete `Self` value or explicit type arguments:

```zero
type FixedVec<T: Type, static N: usize>
  fn cap usize
    ret N

let cap FixedVec.cap()
```

This reports `SHM001`.

Repair it in one of two ways:

- pass explicit type arguments, such as `FixedVec.cap<u8, 4>()`
- call a method that receives a concrete `Self` value, such as
  `FixedVec.push(&mut vec, value)` or `vec.push(value)`

`SHM002` means the explicit method arguments and the receiver's annotated type
disagree.

Receiver calls require a declared method whose first parameter is `self: ref<Self>` or `self: mutref<Self>`:

```zero
let vec FixedVec<u8,4> FixedVec . len 0 items [0, 0, 0, 0]
check vec.push 1
```

This reports `RCV002` because `push` needs `mutref<Self>` and `vec` is
immutable.

Unknown receiver methods report `RCV001`. Static methods without `self` should
be called through the type namespace.

Type field defaults allow omitted fields, but only when the declaration provides a compatible default:

```zero
type NeedsItem
  count usize 0
  item u8

let value NeedsItem NeedsItem .
```

This reports `FLD002` with repair id `initialize-missing-field` because `item`
has no default. A default with the wrong type reports `TYP002` at the default
expression.

Static interfaces are checked at generic specialization time. This fails because `Counter` does not provide the required static method:

```zero
interface Readable<T: Type>
  fn read i32 self ref<T>

type Counter
  value i32

fn readValue<T: Readable<T>> i32 value ref<T>
  ret T.read value
```

This reports `IFC002`. Add a concrete static method with the matching signature:

```zero
fn read i32 self ref<Self>
  ret self.value
```

Static value parameters are checked before emission so fixed-size layouts stay concrete:

```zero
type FixedVec<T: Type, static N: usize>
  len usize
  items [N]T

fn first<T: Type, static N: usize> T vec ref<FixedVec<T,N>>
  ret vec.items[0]

let vec FixedVec<u8,4> FixedVec . len 4 items [1, 2, 3, 4]
let bad first<u8, 8> (&vec)
```

This reports `STC003` because the explicit `8` conflicts with the annotated
`FixedVec<u8,4>`.

Related static-value diagnostics:

- `STC001`: unsupported static parameter type
- `STC002`: runtime value used where a compile-time integer is required

## Commands

- `zero check <input>`: human-first plain text by default
- `zero check --json <input>`: full diagnostic JSON
- `zero explain <code>`: human explanation for a diagnostic code
- `zero explain <code> --json`: machine-readable explanation
- `zero fix --plan --json <input>`: proposed typed fixes without editing files

Useful examples:

```sh
zero explain TAR002
zero explain --json TYP009
zero fix --plan --json conformance/native/fail/mem-copy-immutable-dst.0
zero fix --plan --json --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0
```
