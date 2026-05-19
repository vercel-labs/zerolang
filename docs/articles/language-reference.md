## Program Model

A Zero program can be a single `.0` file or a package with a `zero.json` manifest.

```json
{
  "package": { "name": "hello", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
```

Command-line programs export `main`:

```zero
pub fun main(world: World) -> Void raises {
    check world.out.write("hello from zero\n")
}
```

Examples print user output through `World.out` and diagnostics through
`World.err`. Zero does not expose `std.debug.print` or `std.log`; keeping
printing capability-based makes formatting small and pay-as-used.

`World` is a capability object created by the selected runtime. It is not a
global singleton.

Targets can reject unavailable capabilities. In the current compiler:

- hosted `std.fs` helpers are accepted for the host target
- hosted `std.fs` reports `TAR002` on non-host targets
- `std.mem.copy` and `std.mem.fill` remain target-neutral

## Lexical Basics

Zero source uses UTF-8 text. Identifiers are case-sensitive. Line comments start with `//`.

Common literal forms include:

```zero
let name = "zero"
let marker: char = 'z'
let count = 42
let ratio: f64 = 0.5
let ok = true
```

Top-level `const` declarations can name deterministic compile-time values for use in functions:

```zero
const base: i32 = 40
const answer: i32 = base + 2

pub fun main(world: World) -> Void raises {
    if answer == 42 {
        check world.out.write("const ok\n")
    }
}
```

Literal arithmetic, references to earlier constants, and supported `meta`
expressions are evaluated by the bounded compile-time evaluator. They lower into
ordinary artifact constants.

Public constants must write an explicit type annotation so graph JSON and docs
expose stable API shape.

The V1 compile-time evaluator is deterministic and sandboxed.

`zero check --json` and `zero graph --json` include a `compileTime` object with:

- cache key inputs and limits
- sandbox policy
- supported facts
- static value support
- typed builder limits
- reflection retention policy

The evaluator currently supports:

- literal arithmetic, comparisons, and Bool logic
- target facts such as `target.pointerWidth`, `target.abi`, and `target.hasCapability("fs")`
- typed reflection facts such as `fieldCount(Point)`, `fieldType(Point, "x")`,
  `enumCaseCount(Mode)`, `hasEnumCase(Mode, "tiny")`,
  `choiceCaseCount(Event)`, and `hasChoiceCase(Event, "tick")`

Filesystem, network, ambient environment, and process effects are denied.
Unsupported or cyclic compile-time expressions report `MET001`.

Type aliases provide a compile-time spelling for an existing type:

```zero
pub type ByteCount = usize
type BytePair = Pair<u8, u8>
```

Aliases do not create runtime wrapper types, layout identity, or conversion
code. Cyclic aliases are rejected, and graph JSON reports alias names, targets,
and visibility.

The current compiler keeps compile-time execution intentionally small:

- bounded steps
- bounded recursion depth
- compile-time-only typed reflection
- no release metadata retention by default
- no raw token-string builders

## Functions

Functions are declared with `fun`. Exported functions use `pub fun`.

```zero
fun answer() -> i32 {
    return 40 + 2
}

pub fun main(world: World) -> Void raises {
    let value = answer()
    check world.out.write("done\n")
}
```

Signatures list parameters as `name: Type`. Return types are explicit. Fallible functions include `raises`.

The current compiler supports a narrow, static generic slice. Generic functions
use explicit type parameters and are emitted as concrete specializations only
when called:

```zero
fun identity<T>(value: T) -> T {
    return value
}

let a: i32 = identity<i32>(41)
let b: u8 = identity(7_u8)
```

Argument-based inference is local to the call. If the same generic parameter is
used by more than one argument, all inferred concrete types must match.

Public signatures still write parameter and return types explicitly. The
compiler does not infer exported API shape from function bodies.

Generic declarations can also carry static value parameters. Static values are
known at specialization time and can appear in fixed array lengths or direct
type specializations:

```zero
shape FixedVec<T, static N: usize> {
    len: usize,
    items: [N]T,
}

fun first<T, static N: usize>(vec: ref<FixedVec<T,N>>) -> T {
    return vec.items[0]
}
```

Call sites pass explicit literals, enum cases, or top-level deterministic
`const` values:

```zero
first<u8, 4>(&vec)
Gate<enabled, Mode.fast>
```

The compiler supports integer, `Bool`, and enum static values. It emits concrete
layouts such as `z_FixedVec_u8_4_`.

Static value diagnostics:

| Code | Meaning |
| --- | --- |
| `STC001` | Unsupported static parameter type. |
| `STC002` | Runtime value used where a compile-time value is required. |
| `STC003` | Static argument does not match the expected value. |

Static value support does not add runtime registries, reflection tables, vtables,
or hidden allocation.

Methods declared inside a generic shape inherit the shape's type and static
parameters through `Self`.

Calls may use namespace style or receiver style. Both specialize from a
concrete receiver:

```zero
shape FixedVec<T, static N: usize> {
    len: usize = 0,
    items: [N]T,

    fun init(items: [N]T) -> Self {
        return FixedVec { items: items }
    }

    fun push(self: mutref<Self>, value: T) -> Void raises { Full } {
        if self.len == (N) {
            raise Full
        }
        self.items[self.len] = value
        self.len = self.len + 1
    }
}

let mut vec: FixedVec<u8,4> = FixedVec.init<u8,4>([0, 0, 0, 0])
check FixedVec.push(&mut vec, 10)
check vec.push(20)
```

Field defaults let shape literals omit fields such as `len` when an annotated
generic shape supplies `T` and `N`.

Method lowering stays direct:

- `FixedVec.init<u8,4>(...)` specializes to `z_FixedVec_u8_4_init`
- `vec.push(20)` passes `&mut vec` as the explicit first argument
- the receiver call emits a direct function such as `z_FixedVec_u8_4_push`

There is no method registry, vtable, reflection, hidden allocation, or dynamic
dispatch.

Method diagnostics:

| Code | Meaning |
| --- | --- |
| `SHM001` | Generic shape method call cannot bind `Self`, `T`, or `N`. |
| `SHM002` | Explicit method arguments and receiver shape disagree. |
| `RCV001` | Unknown or non-receiver method. |
| `RCV002` | Temporary or immutable receiver used where a mutable receiver is required. |

Static interfaces constrain generic functions without runtime dispatch:

```zero
interface Readable<T> {
    fun read(self: ref<T>) -> i32
}

fun readValue<T: Readable<T>>(value: ref<T>) -> i32 {
    return T.read(value)
}
```

The concrete type argument must be a shape with matching static methods.
`Readable<T>` is checked at specialization time and erases before direct
emission.

Calls such as `readValue<Counter>(&counter)` lower to direct concrete calls like
`z_Counter_read(...)`. Missing methods or signature mismatches report `IFC001`
through `IFC005`.

## Bindings And Mutation

Use `let` for immutable bindings:

```zero
let message = "hello\n"
```

Use `let mut` for bindings that are intentionally reassigned:

```zero
let mut index = 0
index = index + 1
```

Mutable bindings also support shape-field assignment and fixed-array element assignment through nested lvalue chains:

```zero
shape Point {
    x: i32,
    y: i32,
}

let mut point = Point { x: 1, y: 2 }
point.x = 3

let mut bytes: [4]u8 = [65, 66, 67, 68]
bytes[1] = 90
```

The checker rejects assignment to immutable bindings. Indexed assignment is
currently limited to:

- fixed arrays rooted in `let mut` lvalues
- explicit `MutSpan<T>` writable views

Read-only `Span<T>` and `String` indexed mutation are not part of the current
public surface.

## Types

Zero is statically typed. The native compiler currently implements checked
integer widths for `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`,
`usize`, and `isize`.

Integer literals support decimal, `0x` hexadecimal, `0b` binary, `0o` octal,
`_` separators, and optional suffixes such as `_u8` or `_usize`. Literals are
context-typed and range-checked: `let byte: u8 = 255` is valid, while
`let byte: u8 = 256` is rejected.

Non-literal integer values do not implicitly narrow, widen, or change
signedness. Use `value as Type` for explicit integer-to-integer casts.

```zero
let count: u32 = 0x12c_u32
let byte: u8 = count as u8
```

The current `as` form is intentionally explicit. It supports primitive integers,
floats, and byte-sized `char`.

It does not cast strings, booleans, memory views, shapes, choices, or pointers.

`f32` and `f64` are primitive floating-point types. Float literals use
`digits "." digits` with an optional exponent, such as `1.0`, `0.5`, and
`1.0e-3`.

Untyped float literals default to `f64`. `f32` literals require an expected
`f32` context.

Floats are distinct from integers. Arithmetic and comparisons require matching
float widths.

`char` is a distinct byte-sized primitive for ASCII/parser/codec-style values.
Character literals use single quotes and decode to one byte:

- `'a'`
- `'\n'`
- `'\''`
- `'\\'`
- `'\x41'`

A `char` is not a `String` or an integer type. It does not implicitly convert to
or from `u8`, and it is not accepted in integer arithmetic.

`f16`, Unicode scalar literals, and char arrays are not part of the current
public surface. `Void` is used when a function returns no useful value.

Optional values use `Maybe<T>`. Use `null` only where the expected type is a
`Maybe<T>`; untyped `null` is rejected.

Memory-oriented APIs use types such as `Span<T>`, `MutSpan<T>`, `ref<T>`,
`mutref<T>`, and `Alloc`. The hosted file slice also exposes `Fs`, `File`, and
`owned<File>` for explicit resource ownership.

The native compiler validates these forms today and emits runnable layouts for
`Span<T>`, `MutSpan<T>`, `Maybe<T>`, and the small hosted file structs.

The native compiler supports single-element indexing and half-open range slices
for fixed arrays, spans, and byte-oriented strings.

Index expressions and slice bounds must be integers. Integer literals in those
positions are checked as `usize`:

```zero
let bytes: [4]u8 = [65, 66, 67, 68]
let scratch: [16]u8 = [0_u8; 16]
let first: u8 = bytes[0]
let tail: Span<u8> = bytes[1..4]
let view: Span<u8> = std.mem.span("ABCD")
let second: u8 = view[1]
let pair: Span<u8> = view[1..3]
let suffix: Span<u8> = view[1..]
let prefix: Span<u8> = view[..3]
let all: Span<u8> = view[..]

let values: [4]i32 = [10, 20, 30, 40]
let numbers: Span<i32> = values
let third: i32 = numbers[2]
let middle: Span<i32> = values[1..3]

let mut writableValues: [3]i32 = [1, 2, 3]
let writable: MutSpan<i32> = writableValues
writable[1] = 20

let text: String = "zero"
let byte: u8 = text[1]
let bytes: Span<u8> = text[1..]
```

Current indexing behavior:

| Source | Result |
| --- | --- |
| `[N]T`, `Span<T>`, `MutSpan<T>` | `T` |
| `String` | `u8` |

Slice forms are `start..end`, `start..`, `..end`, and `..`. They return
`Span<T>` views for arrays/spans and `Span<u8>` views for strings. Slices are
half-open: the start is included, the end is excluded. Omitted starts default to
`0`; omitted ends default to the base length.

Assignments may target:

- mutable local bindings
- shape fields rooted in mutable locals
- fixed-array indexes in those lvalue chains
- `MutSpan<T>` elements
- indexed `mutref<MutSpan<T>>` paths

Bounds are checked at runtime for indexes, slices, fixed-array indexed
assignment, and `MutSpan<T>` indexed assignment. Failures print
`zero bounds check failed` and abort. Use `std.mem.get(value, index)` when a
recoverable `Maybe<T>` result is preferred.

String indexing and slicing are byte-oriented, not Unicode scalar operations.
`std.mem.len` accepts fixed arrays, `Span<T>`, and `MutSpan<T>`.
`std.mem.eqlBytes` compares same-element span views.

The native compiler does not yet support:

- read-only `Span<T>` or `String` indexed mutation
- slice assignment
- assignment through calls or temporaries
- profile-specific bounds-check elision

## Control Flow

Use `if` / `else` for branches:

```zero
if value == 42 {
    check world.out.write("math works\n")
} else {
    check world.out.write("math broke\n")
}
```

Conditions must be `Bool`; integers and pointers do not coerce to truthy or falsey values.

Use `while` for loops:

```zero
while keepGoing {
    check world.out.write("loop\n")
}
```

Use range `for` loops for integer ranges. The end bound is exclusive:

```zero
for index in 0..4 {
    if index == 2 {
        continue
    }
    check world.out.write("tick\n")
}
```

Use `break` to exit the nearest loop and `continue` to skip to the next iteration.

Use `return` to exit a function with a value.

## Effects And Errors

Zero keeps effectful operations visible.

```zero
pub fun main(world: World) -> Void raises {
    check world.out.write("hello\n")
}
```

`check` calls a fallible operation and propagates failure. Functions that use `check` declare `raises`.

User-defined errors are named symbols. A function can declare an open `raises` marker, or an explicit error set:

```zero
fun validate(ok: Bool) -> i32 raises { InvalidInput } {
    if ok == false {
        raise InvalidInput
    }
    return 42
}

fun run() -> Void raises { InvalidInput } {
    check validate(true)
}
```

The native compiler validates explicit error flow:

- `raise ErrorName` can appear only in a raising function.
- A function with `raises { ... }` may only raise listed errors.
- Calling a fallible user function requires `check`.
- Callers with explicit error sets must include every checked callee error.
- `let value = check fallible_call()` works for user fallible calls, `Maybe<T>`, and named-error `std.fs` helpers.
- `let value = expr rescue err { fallback }` works for the same simple cases and lowers to direct branches.

Zero does not use language-level exceptions.

For the current native helper slice, `check` on a `Maybe<T>` lowers to a direct
branch. If the value is absent, the function returns its default failure value.

No exception object, unwinding, or hidden global error state is created.
User-defined fallible functions lower to small generated status/result structs
only when they use explicit error flow.

## Shapes

Use `shape` for named records:

```zero
shape Point {
    x: i32,
    y: i32,
}

let point = Point { x: 40, y: 2 }
let total = point.x + point.y
```

Shape literals name their fields. Field access uses dot syntax.

Shape fields can declare defaults:

```zero
shape Pair {
    left: u8 = 1,
    right: u8,
}

let pair: Pair = Pair { right: 2 }
```

Only fields with defaults may be omitted. Defaults are typechecked against the
declared field type and lower as ordinary C initializers at each shape literal
site.

Generic shapes are supported when construction has an explicit annotated type:

```zero
shape Pair<T, U> {
    left: T,
    right: U,
}

let pair: Pair<i32, u8> = Pair { left: 42, right: 7_u8 }
let value: i32 = pair.left
```

Generic shape layouts are monomorphized before emission. The current compiler
supports:

- multiple type parameters
- integer static value parameters
- field defaults
- generic functions that return instantiated shapes such as `Pair<T, U>`
- generic shape methods with namespace and receiver-style calls

Broader static value types and defaulted generic arguments are not part of the
current public surface.

Shapes may define small static methods that are called through namespace-style lookup:

```zero
shape Counter {
    value: i32,

    fun add(self: ref<Self>, amount: i32) -> i32 {
        return self.value + amount
    }
}

let counter: Counter = Counter { value: 40 }
let answer = Counter.add(&counter, 2)
```

This is direct static lowering to a concrete function such as `z_Counter_add`.
There is no dynamic dispatch, vtable, or method registry.

Receiver-style calls are reserved for shape methods whose first parameter is
`self: ref<Self>` or `self: mutref<Self>`.

## Enums, Choices, And Match

Use `enum` for a fixed set of names:

```zero
enum Status {
    ready,
    failed,
}
```

Use `choice` for alternatives, including alternatives with payloads:

```zero
choice Result {
    ok: i32,
    err: String,
}
```

Construct payload variants with the choice name:

```zero
let result: Result = Result.ok(42)
```

Match choices exhaustively:

```zero
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

Use `._` as a fallback arm when a match intentionally groups remaining cases:

```zero
match mode {
    .fast {
        check world.out.write("fast\n")
    }
    ._ {
        check world.out.write("other\n")
    }
}
```

Fallback arms cannot bind payloads. Use a named choice case with `=> payload` when the payload value is needed.

## Defer

`defer` schedules cleanup for the end of the current scope:

```zero
pub fun main(world: World) -> Void raises {
    defer cleanup()
    check world.out.write("work\n")
}
```

The current native compiler supports simple `defer` on lexical scope exit, including exits through `return`, `break`, and `continue`.

Live `owned<T>` locals are also cleaned up at lexical exits when `T` defines the canonical non-raising shape method:

```zero
shape Handle {
    marker: MutSpan<u8>,

    fun drop(self: mutref<Self>) -> Void {
        self.marker[0] = 1
    }
}
```

The compiler emits a direct `Handle_drop(&value)` call in reverse declaration
order.

If an owned local is moved into another owned binding, owned parameter, or owned
return, the old binding is not dropped. Direct user calls such as `value.drop()`
remain rejected; use the shape method for automatic cleanup or a separate
explicit cleanup function when you need manual control.

`owned<File>` is compiler-known in the current hosted `std.fs` slice. It lowers
to the underlying file handle and closes deterministically at lexical exits,
including early `return`.

This does not use a registry, refcount, or process-global cleanup list. Explicit
`std.fs.close(&mut file)` is allowed and is idempotent with the automatic cleanup
path.

## Borrows

Use `&value` to create a shared `ref<T>` and `&mut value` to create a mutable `mutref<T>`:

```zero
shape Point {
    x: i32,
    y: i32,
}

fun read_x(point: ref<Point>) -> i32 {
    return point.x
}

fun write_x(point: mutref<Point>, value: i32) -> Void {
    point.x = value
}
```

`&mut` requires a mutable lvalue root, and assignment through `ref<T>` is
rejected.

The current native checker tracks lexical borrow conflicts and references stored
inside values, returned from calls, or merged through control flow. It rejects
assignment while a reachable reference is live, rejects returning references to
local bindings, and rejects storing callee-local references into caller-owned
`mutref` storage. `BOR001` JSON includes a `borrowTrace.activeBorrows` array
for lexical conflicts so agents can identify live bindings and move the
conflicting operation or narrow the borrows with an inner block. Borrows lower
to direct address expressions; there is no borrow registry or runtime alias
metadata.

## Imports And Standard Library

Use `use` to import modules:

```zero
use std.codec
use std.parse
```

Current native helpers include:

- `std.mem`: allocation-free memory helpers, span construction, byte equality,
  explicit allocators, fixed-capacity `Vec`, empty map/set metadata, and
  `ByteBuf`
- `std.codec`: byte and checksum helpers such as `readU32`, `encodedVarintLen`, and `crc32`
- `std.parse`: scanner helpers such as digit and identifier predicates
- `std.time`: duration helpers such as `ms`, `seconds`, `add`, and `asMsFloor`
- `std.args`: CLI helpers `len()` and `get(index) -> Maybe<String>`
- `std.path`: fixed-buffer path helpers `basename(path) -> String` and `join(buffer, left, right) -> Maybe<String>`
- `std.fs`: hosted path helpers, explicit `Fs` handles, owned file handles,
  fallible reads/writes, and `readAll` helpers backed by an explicit allocator
  and size limit

The current `std.fs` helpers are hosted CLI APIs. They use:

- path strings or explicit `Fs` capabilities
- caller-owned fixed buffers
- `Maybe<T>` and `Bool` results
- named-error variants where examples need recovery
- `owned<File>` cleanup

`readAll` and `readAllOrRaise` use an explicit allocator and size limit; neither
reaches for a process heap. Non-host target checks reject this hosted slice with
`TAR002`. Use `std.mem` helpers and package-local modules for target-neutral
builds.

Richer file modes, permissions, and platform-specific path normalization are not
part of the current public surface.

## Packages

A package uses `zero.json`:

```json
{
  "package": { "name": "systems-package", "version": "0.1.0", "license": "MIT" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } },
  "deps": {},
  "profiles": {
    "dev": { "inherits": "dev" },
    "release-small": { "inherits": "release-small" }
  }
}
```

Check a package by passing its directory:

```sh
zero check examples/systems-package
```

Package-local imports are explicit:

- `use helpers` resolves `src/helpers.0`
- `use config.parser` resolves `src/config/parser.0` or `src/config/parser/mod.0`

Build resolution is declarative and does not execute dependency code. Unknown
imports, direct import cycles, bad manifests, and duplicate public exports are
reported before parsing the combined package source.

`zero graph --json <package>` lists module names, source paths, import edges
with source ranges, parsed `useImports` with source ranges, public/private
symbol counts, target metadata, function effects, required capabilities, and
whether the selected target provides hosted filesystem support.

There is no published package registry or semantic version solver in the
current compiler.

Local path dependencies resolve from `zero.json`. Exact versioned registry
references are recorded as metadata without remote fetches. The resolver writes
deterministic dependency fingerprint files under `.zero/package-locks/`.

## C Interop

Use `extern c` and `extern shape` for C boundaries:

```zero
extern c "config.h" as config

extern shape CConfig {
    enabled: bool,
    limit: i32,
}
```

Interop declarations should make layout and ABI expectations explicit.

## Toolchain

Common native commands:

```sh
zero check examples/hello.0
zero build examples/hello.0 --out .zero/out/hello
zero build --emit exe examples/add.0 --out .zero/out/add
zero build --emit exe --target linux-musl-x64 examples/add.0 --out .zero/out/add-linux-musl
zero graph --json examples/systems-package
zero size --json examples/point.0
zero targets
```

Executable targets are named after the supported artifact family:

- `darwin-arm64`
- `darwin-x64`
- `linux-arm64`
- `linux-musl-arm64`
- `linux-musl-x64`
- `win32-arm64.exe`
- `win32-x64.exe`

Supported non-host executable builds use direct emitters.
