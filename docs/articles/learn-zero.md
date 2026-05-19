## A Program Starts With `main`

The smallest example is `examples/hello.0`:

```zero
pub fun main(world: World) -> Void raises {
    check world.out.write("hello from zero\n")
}
```

`pub` exports the entry point. `fun` declares a function. `main` receives a
`World` capability instead of using hidden globals.

`-> Void` means the function does not return a useful value. `raises` means it
can fail.

Run:

```sh
zero check examples/hello.0
```

## Effects Use Capabilities

Output is not magic in Zero. The program writes through `world.out`:

```zero
check world.out.write("hello from zero\n")
```

`write` can fail, so it is called with `check`. The function that uses `check` declares `raises`.

## Bind Values With `let`

`examples/hello-let.0` introduces a local binding:

```zero
pub fun main(world: World) -> Void raises {
    let message = "hello from a binding\n"
    check world.out.write(message)
}
```

Use `let` when a value should not change. Use `let mut` only when the value is intentionally reassigned.

## Write Functions

`examples/add.0` defines a helper function and calls it from `main`:

```zero
fun answer() -> i32 {
    return 40 + 2
}

pub fun main(world: World) -> Void raises {
    let value = answer()
    if value == 42 {
        check world.out.write("math works\n")
    } else {
        check world.out.write("math broke\n")
    }
}
```

Function signatures list parameter names and types. Return types are explicit. Use `return` when you want to leave a function with a value.

The native compiler understands explicit integer widths today:

```text
i8 i16 i32 i64
u8 u16 u32 u64
usize isize
```

Integer literals support decimal, `0x` hex, `0b` binary, `0o` octal, `_`
separators, and suffixes such as `_u8` or `_usize`.

Literals are checked against their context. `let byte: u8 = 255` works.
`let byte: u8 = 256` fails at `zero check`.

Existing integer values keep their exact type. Use `as` when you intentionally
convert between primitive integer types:

```zero
let count: u32 = 0x12c_u32
let byte: u8 = count as u8
```

The current cast support is limited to integer-to-integer conversions.

`f32` and `f64` are available for decimal float literals. Untyped float literals
default to `f64`:

```zero
let ratio: f64 = 1.0e-3
let small: f32 = 0.5
let total = ratio + 2.0
```

Floats do not implicitly mix with integers or with each other across widths.

`char` is also available as a distinct byte-sized primitive for single quoted
byte literals. It does not cast to or from integers:

```zero
let letter: char = 'A'
let newline: char = '\n'
let same = letter == '\x41'
```

`f16`, Unicode scalar literals, and casts for non-integer values are not part of
the current public surface.

## Use Control Flow

Zero has ordinary `if` / `else` blocks:

```zero
if value == 42 {
    check world.out.write("math works\n")
} else {
    check world.out.write("math broke\n")
}
```

It also supports `while` loops in the current native subset:

```zero
while keepGoing {
    check world.out.write("loop\n")
}
```

Use range `for` when you need an integer counter:

```zero
for index in 0..4 {
    if index == 2 {
        continue
    }
    check world.out.write("tick\n")
}
```

Use `break` to leave the nearest loop and `continue` to skip to the next
iteration.

Conditions must be `Bool`, so compare values explicitly instead of relying on
truthy integers.

Prefer direct conditions and explicit state. The checker rejects assignments to
immutable bindings, so introduce `let mut` only when a loop or algorithm really
mutates state.

## Model Data With `shape`

Use `shape` for named records. `examples/point.0` defines a point and passes it to a helper:

```zero
shape Point {
    x: i32,
    y: i32,
}

fun sum(point: Point) -> i32 {
    return point.x + point.y
}

pub fun main(world: World) -> Void raises {
    let point = Point { x: 40, y: 2 }
    let total = sum(point)
    if total == 42 {
        check world.out.write("point works\n")
    }
}
```

Shape literals name their fields. Field access uses `value.field`.

## Use Field Defaults

Shapes can provide defaults for fields that callers may omit:

```zero
shape Counter {
    value: i32 = 0,
}

let counter = Counter {}
```

Defaults lower into ordinary concrete initializers. If a field has no default, shape literals must initialize it explicitly.

## Represent Alternatives With `enum` And `choice`

Use `enum` for a fixed set of names:

```zero
enum Status {
    ready,
    failed,
}
```

Use `choice` when alternatives may carry payloads:

```zero
choice Result {
    ok: i32,
    err: String,
}
```

`examples/result-choice.0` constructs a payload choice and matches it:

```zero
let result: Result = Result.ok(42)
match result {
    .ok => value {
        if value == 42 {
            check world.out.write("choice ok\n")
        }
    }
    .err => message {
        check world.out.write("choice err\n")
    }
}
```

Matches must be exhaustive. If a choice has `ok` and `err`, handle both. Use `=> name` to bind the payload of a choice case inside that arm.

## Import Standard Library Modules

Use `use` to import standard library modules. `examples/codec-varint.0` uses `std.codec`:

```zero
use std.codec

pub fun main(world: World) -> Void raises {
    let len = std.codec.encodedVarintLen(300)
    let checksum = std.codec.crc32("zero")
    if len == 2 && checksum > 0 {
        check world.out.write("codec primitives ok\n")
    }
}
```

`examples/parse-cursor.0` uses `std.parse`:

```zero
use std.parse

pub fun main(world: World) -> Void raises {
    let digit = std.parse.isAsciiDigit("7")
    let ident = std.parse.isIdentifierStart("_")
    if digit && ident {
        check world.out.write("parse primitives ok\n")
    }
}
```

The current native compiler supports early helpers from `std.mem`, `std.codec`,
`std.parse`, and duration-focused `std.time`.

Codec helpers now return their documented widths, such as
`std.codec.readU16(...) -> u16`.

CLI-oriented helpers are also available:

```zero
pub fun main(world: World) -> Void raises {
    let first = std.args.get(1)
    if first.has {
        let written = std.fs.write(".zero/out/name.txt", first.value)
        if written > 0 {
            check world.out.write("wrote argument\n")
        }
    }
}
```

`std.args.get` returns `Maybe<String>` because the requested argument may not
exist.

The current `std.fs` helpers are hosted APIs. Use the standard library reference
when you need explicit `Fs`, `File`, and `owned<File>` resource examples.

## Organize A Package

A package has a `zero.json` manifest and source files under `src/`.

```json
{
  "package": { "name": "systems-package", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
```

`examples/systems-package/src/main.0` imports modules and local declarations:

```zero
use std.codec
use std.parse
use std.time

pub fun main(world: World) -> Void raises {
    defer cleanup()
    let current: Status = status()
    let result: Result = Result.ok
    let word = std.codec.readU32("abcd")
    let digits = std.parse.scanDigits("123abc")
    let duration = std.time.add(std.time.ms(5), std.time.seconds(1))
    if digits == 3 && word > 0 && std.time.asMsFloor(duration) > 0 {
        check world.out.write("systems package\n")
    }
}
```

Check the package:

```sh
zero check examples/systems-package
```

## Run Tests

Zero test blocks live next to source code:

```zero
test "addition is stable" {
    expect(40 + 2 == 42)
}
```

Run tests with:

```sh
zero test conformance/native/pass/test-blocks.0
zero test --json --filter addition conformance/native/pass/test-blocks.0
```

Failing tests include the failing test name and exit nonzero.

## Check Cross Targets

Target names are explicit. Use `zero targets` to inspect support, then pass `--target` to `check`, `build`, `graph`, or `size`:

```sh
zero targets
zero check --target linux-musl-x64 examples/memory-package
zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
```

The checker rejects unavailable capabilities, such as hosted `std.fs` on target-neutral builds.

## Use Diagnostics

Diagnostics are stable enough for humans and agents:

```sh
zero check --json conformance/check/fail/unknown-name.0
zero explain NAM003
zero fix --plan --json conformance/check/fail/unknown-name.0
```

Each JSON diagnostic includes a code, span, expected/actual fields, help, fix safety, and repair metadata.

## Understand Cleanup With `defer`

`defer cleanup()` schedules cleanup for the end of the current scope:

```zero
pub fun main(world: World) -> Void raises {
    defer cleanup()
    check world.out.write("work\n")
}
```

Use `defer` for cleanup that should happen when a scope exits, including exits
through `return`, `break`, and `continue`.

Live `owned<T>` locals are also cleaned up when `T` defines the canonical non-raising `fun drop(self: mutref<Self>) -> Void`.
Direct user calls such as `value.drop()` remain rejected so cleanup stays deterministic.

## Read Memory-Oriented Types

Some examples introduce the vocabulary used by lower-level Zero code:

```zero
shape BufferView {
    bytes: Span<u8>,
}

pub fun main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero")
    let view = BufferView { bytes: bytes }
    if std.mem.len(view.bytes) == 4 && view.bytes[0] == 122 {
        check world.out.write("span ok\n")
    }
}
```

Useful terms:

- `Span<T>` is a read-only view over contiguous values.
- `MutSpan<T>` is an explicit writable view over mutable fixed-array storage.
- Current runnable layouts include `Span<T>`, `MutSpan<T>`, and single-element `[N]T`.
- Indexing supports spans, fixed arrays, and byte-oriented `String` values.
- Slices are half-open: `start..end`, `start..`, `..end`, and `..`.
- Bounds traps are emitted for indexes and slices.
- Indexed lvalues work for mutable shape fields, fixed arrays, and `MutSpan<T>`.
- Allocation-free helpers include `std.mem.span`, generic `std.mem.len`, and `std.mem.eqlBytes`.
- `Maybe<T>` represents a value that may be absent.
- `ref<T>` and `mutref<T>` make reference mutability explicit.
- `Alloc` is an allocator capability. Current allocation helpers stay explicit
  and limited to documented allocator-backed APIs.

You do not need all of these for hello world, but you will see them in systems code and C interop.

## Cross A C Boundary

Use `extern c` and `extern shape` for C interop declarations:

```zero
extern c "config.h" as config

extern shape CConfig {
    enabled: bool,
    limit: i32,
}
```

Interop types should make layout and ABI boundaries clear. Use `extern shape` for data that must match C layout.

## What To Read Next

- The examples index lists examples in a learning order.
- The language reference documents syntax and semantics.
- The native compiler guide explains source builds and compiler validation.
- Diagnostics explains how to read and use errors.
- Implementation status explains current limits.
