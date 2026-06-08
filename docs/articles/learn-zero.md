## Learn From The Graph First

Zero is learned through the same path agents should use: inspect the graph,
patch the graph, validate the graph, and export `.0` only when a human wants a
readable projection. The code snippets on this page show projection syntax so
you can understand reviews and examples, not because source text is the normal
agent write surface.

Start a learning session by asking the agent to load the compiler-matched
skills and stay graph-first:

```text
Use Zero's bundled agent, graph, language, and stdlib skills.
Teach me with graph inputs and zero patch. Do not hand-edit `.0` files.
Only export a source projection when I ask to review it.
```

Useful graph-first commands:

```sh
zero init
zero query
zero patch --op 'addMain' --op 'addCheckWrite fn="main" text="hello from zero\n"'
zero check
zero run
```

For checked-in examples, use their `.graph` inputs:

```sh
zero query examples/hello.graph
zero check examples/hello.graph
zero run examples/hello.graph
```

When a page shows `.0` syntax, read it as the human projection of the graph.
Agents should prefer `zero query`, `zero patch`, `zero check`, `zero test`, and
`zero run` against graph inputs or package directories.

## A Program Starts With `main`

The smallest example is `examples/hello.graph`:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello from zero\n")
}
```

`pub` exports the entry point. `fn` declares a function. `main` receives a
`World` capability instead of using hidden globals.

`Void` is the return type, and `raises` means the function can fail.

Run the graph input:

```sh
zero check examples/hello.graph
zero run examples/hello.graph
```

The exported projection would be `src/main.0` inside a package, but that file is
for human review and occasional manual edits. A graph-first agent should not
open or rewrite it unless the task asks for a projection.

## Patch Programs Through The Graph

Small edits can use structured operations:

```sh
zero patch examples/hello.graph \
  --op 'set node="#expr_653eeb6e" field="value" expect="hello from zero\n" value="hello graph\n"'
```

Larger edits can use a patch document. `replaceFunctionBody` and
`replaceBlockBody` accept compact row syntax so agents can replace behavior
without hand-authoring every node edge:

```text
zero-program-graph-patch v1
expect graphHash "graph:a7f7e6899a73f3b4"
replaceFunctionBody main
  let name Maybe<String> = std.args.get 1
  if name.has
    check world.out.write "hello "
    check world.out.write name.value
    check world.out.write "\n"
  else
    check world.out.write "hello anonymous\n"
end
```

Run `zero patch --check-only` before applying a larger patch when the agent is
assembling it from multiple observations. A successful patch validates the graph
and reports the updated graph hash, so agents usually do not need a separate
`zero check` just to know whether the edit was accepted.

## Effects Use Capabilities

Output is not magic in Zero. The program writes through `world.out`:

```zero
check world.out.write("hello from zero\n")
```

`write` can fail, so it is called with `check`. The function that uses `check` declares `raises`.

## Bind Values With `let`

`examples/hello-let.graph` introduces a local binding:

```zero
pub fn main(world: World) -> Void raises {
    let message: String = "hello from a binding\n"
    check world.out.write(message)
}
```

Use `let` when a value should not change. Use `var` only when the value is intentionally reassigned.

## Write Functions

`examples/add.graph` defines a helper function and calls it from `main`. Inspect
the callable surface with `zero query examples/add.graph`; read the projection
only when you want to understand how the graph renders:

```zero
fn answer() -> i32 {
    return 40 + 2
}

pub fn main(world: World) -> Void raises {
    let value: i32 = answer()
    if value == 42 {
        check world.out.write("math works\n")
    } else {
        check world.out.write("math broke\n")
    }
}
```

Function signatures name parameters in parentheses, then the return type after `->`. Use `return` when you want to leave a function with a value.

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
let total: f64 = ratio + 2.0
```

Floats do not implicitly mix with integers or with each other across widths.

`char` is also available as a distinct byte-sized primitive for single quoted
byte literals. It does not cast to or from integers:

```zero
let letter: char = 'A'
let newline: char = '\n'
let same: Bool = letter == '\x41'
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
immutable bindings, so introduce `var` only when a loop or algorithm really
mutates state.

## Model Data With `type`

Use `type` for named records. `examples/point.graph` defines a point and passes
it to a helper:

```zero
type Point {
    x: i32,
    y: i32,
}

fn sum(point: Point) -> i32 {
    return point.x + point.y
}

pub fn main(world: World) -> Void raises {
    let point: Point = Point { x: 40, y: 2 }
    let total: i32 = sum(point)
    if total == 42 {
        check world.out.write("point works\n")
    }
}
```

Type literals name their fields. Field access uses `value.field`.

## Use Field Defaults

Types can provide defaults for fields that callers may omit:

```zero
type Counter {
    value: i32 = 0,
}

let counter: Counter = Counter {}
```

Defaults lower into ordinary concrete initializers. If a field has no default, type literals must initialize it explicitly.

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

`examples/result-choice.graph` constructs a payload choice and matches it:

```zero
let result: Result = Result.ok(42)
match result {
    .ok(value) {
        if value == 42 {
            check world.out.write("choice ok\n")
        }
    }
    .err(message) {
        check world.out.write("choice err\n")
    }
}
```

Matches must be exhaustive. If a choice has `ok` and `err`, handle both. Put the payload name after the case name to bind it inside that arm.

## Import Standard Library Modules

Use `use` to import standard library modules. Agents should learn the callable
stdlib surface with `zero skills get stdlib`, then patch user programs through
the graph. `examples/codec-varint.graph` uses `std.codec`:

```zero
use std.codec

pub fn main(world: World) -> Void raises {
    let len: usize = std.codec.encodedVarintLen(300)
    let checksum: u32 = std.codec.crc32("zero")
    if len == 2 && checksum > 0 {
        check world.out.write("codec primitives ok\n")
    }
}
```

`examples/parse-cursor.graph` uses `std.parse`:

```zero
use std.parse

pub fn main(world: World) -> Void raises {
    let digit: Bool = std.parse.isAsciiDigit("7")
    let ident: Bool = std.parse.isIdentifierStart("_")
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
pub fn main(world: World) -> Void raises {
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        let written: usize = std.fs.write(".zero/out/name.txt", first.value)
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

## Organize A Graph-First Package

A package has a `zero.toml` manifest, a checked-in `zero.graph` store, and
source projections under `src/`.

```toml
[package]
name = "systems-package"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "src/main.0"
```

The `main` path names the projection used for source spans and human
import/export. It does not make `src/main.0` the normal package compile input;
`zero check examples/systems-package` reads the package graph store.

The usual package loop is:

```sh
zero query examples/systems-package
zero patch --check-only examples/systems-package --op 'addFunction name="helper" ret="i32"'
zero check examples/systems-package
```

If a human reviews or edits a projection, run `zero import` from the package
root to rebuild `zero.graph`. If a human only wants to see the current
projection, run `zero export`; agents should not export just to continue their
own edit loop.

`examples/systems-package/src/main.0` imports modules and local declarations:

```zero
use std.codec

use std.parse

use std.time

pub fn main(world: World) -> Void raises {
    defer cleanup()
    let current: Status = status()
    let result: Result = Result.ok
    let word: i32 = std.codec.readU32("abcd")
    let digits: i32 = std.parse.scanDigits("123abc")
    let duration: i32 = std.time.add(std.time.ms(5), std.time.seconds(1))
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

Zero test blocks live in the graph and export next to source projection code:

```zero
test "addition is stable" {
    expect (40 + 2 == 42)
}
```

Run tests with:

```sh
zero test conformance/native/pass/test-blocks.graph
zero test --json --filter addition conformance/native/pass/test-blocks.graph
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

Diagnostics are stable enough for humans and agents. Start with readable output
and use `--json` only when automation needs exact spans or stable fields:

```sh
zero explain --json NAM003
zero explain NAM003
zero query examples/hello.graph
```

Each JSON diagnostic includes a code, span, expected/actual fields, help, fix safety, and repair metadata.

## Understand Cleanup With `defer`

`defer cleanup()` schedules cleanup for the end of the current scope:

```zero
pub fn main(world: World) -> Void raises {
    defer cleanup()
    check world.out.write("work\n")
}
```

Use `defer` for cleanup that should happen when a scope exits, including exits
through `return`, `break`, and `continue`.

Live `owned<T>` locals are also cleaned up when `T` defines the canonical non-raising `fn drop(self: mutref<Self>) -> Void`.
Direct user calls such as `value.drop()` remain rejected so cleanup stays deterministic.

## Read Memory-Oriented Types

Some examples introduce the vocabulary used by lower-level Zero code:

```zero
type BufferView {
    bytes: Span<u8>,
}

pub fn main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero")
    let view: BufferView = BufferView { bytes: bytes }
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
- Indexed lvalues work for mutable type fields, fixed arrays, and `MutSpan<T>`.
- Allocation-free helpers include `std.mem.span`, generic `std.mem.len`, and `std.mem.eqlBytes`.
- `Maybe<T>` represents a value that may be absent.
- `ref<T>` and `mutref<T>` make reference mutability explicit.
- `Alloc` is an allocator capability. Current allocation helpers stay explicit
  and limited to documented allocator-backed APIs.

You do not need all of these for hello world, but you will see them in systems code and C interop.

## Cross A C Boundary

Use `extern c` and `extern type` for C interop declarations:

```zero
extern c "config.h" as config

extern type CConfig {
    enabled: bool,
    limit: i32,
}
```

Interop types should make layout and ABI boundaries clear. Use `extern type` for data that must match C layout.

## What To Read Next

- The examples index lists examples in a learning order.
- The CLI reference documents graph query, patch, import, export, and diff.
- The language reference documents projection syntax and semantics.
- The standard library reference documents graph-backed module surfaces.
- Diagnostics explains how humans and agents should use errors.
