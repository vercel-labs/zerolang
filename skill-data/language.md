---
name: language
description: Compact zerolang syntax and semantics guide for agents.
---

# zerolang Language

Use this when writing or reviewing `.0` source, especially if the model has no prior zerolang training. `.0` source is canonical text: regular declarations, typed bindings, braces, infix operators, and explicit calls.

## Minimal Program

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello from zerolang\n")
}
```

`pub fn` exports a function. `World` carries runtime capabilities. `raises` marks a fallible function. `check` calls a fallible operation and propagates failure.

## Declarations

Top-level declarations include:

- `use std.mem` or `use helpers`
- `const answer: i32 = 42`
- `const inferred = 42`
- `type Point { x: i32, y: i32, }`
- `enum Mode { fast, small, }`
- `choice Result { ok: i32, err: String, }`
- `fn answer() -> i32 { return 42 }`
- `pub fn main(world: World) -> Void raises { ... }`
- `test "name" { expect true }`

Use `.0` for source files.

## Values, Mutation, And Control Flow

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

Use `let` by default and `var` only when a binding changes. Conditions are `Bool`; do not rely on truthy integers or strings.

```zero
fn count(n: usize) -> usize {
    var i: usize = 0
    while i < n {
        i = i + 1
    }
    return i
}
```

Operators are normal infix expressions: `a + b`, `a - b`, `a * b`, `a % b`, `a == b`, `a < b`, `a && b`. Use parentheses for grouping. Comments start with `//`.

## Types

Common primitive types:

```text
Bool Void String char
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
```

Integer literals are checked against context. Use suffixes such as `_u8` or `_usize` when needed. Use `as` for intentional integer casts.

## Shapes, Enums, And Choices

```zero
type Point {
    x: i32,
    y: i32,
}

enum Mode {
    fast,
    small,
}

choice Result {
    ok: i32,
    err: String,
}
```

Construct a shape with field names:

```zero
let point: Point = Point { x: 1, y: 2 }
```

Choice payload cases use the choice name:

```zero
let result: Result = Result.ok(42)
```

Matches must be exhaustive unless they use the fallback arm `_`:

```zero
match result {
    .ok(value) {
        expect value == 42
    }
    .err(message) {
        expect true
    }
}
```

## Errors

```zero
fn value(i: i32) -> i32 raises [Odd] {
    if i % 2 == 0 {
        return i
    }
    raise Odd
}

pub fn main(world: World) -> Void raises {
    let item: i32 = rescue value(3) err 1
    if item == 1 {
        check world.out.write("fallible ok\n")
    }
}
```

`raises [Error]` restricts the error set. Plain `raises` is open. Calling a fallible function requires `check` or `rescue`.

## Borrowing And Memory Views

- `ref<T>` is a read-only borrow, passed with `&value`.
- `mutref<T>` is a mutable borrow, passed with `&mut value`.
- `[N]T` is a fixed array.
- `Span<T>` is a read-only contiguous view.
- `MutSpan<T>` is a writable contiguous view.
- Returning a span backed by local fixed-array storage is rejected; return an owned value or keep the view local.
- `Maybe<T>` represents absence; read `.value` only inside a visible `.has` guard, or use `check` / `rescue`.
- `owned<T>` marks explicit resource ownership.

```zero
fn bump(point: mutref<Point>) -> Void {
    point.x = point.x + 1
}
```

Arrays and views:

```zero
pub fn main() -> Void {
    var storage: [4]u8 = [1, 2, 3, 4]
    let view: MutSpan<u8> = storage
    let first: u8 = storage[0]
    storage[0] = 9
}
```

## Generics

```zero
type Box<T: Type> {
    value: T,
}

fn id<T: Type>(value: T) -> T {
    return value
}
```

Static generic values are declared with `static`:

```zero
type FixedVec<T: Type, static N: usize> {
    len: usize,
    items: [N]T,
}
```

## Standard Library Call Shapes

Common target-neutral helpers:

```zero
pub fn main() -> Void {
    let bytes: Span<u8> = std.mem.span("zero")
    let n: usize = std.mem.len(bytes)
    let same: Bool = std.mem.eql("zero", "zero")

    var dst: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    let writable: MutSpan<u8> = dst
    let copied: usize = std.mem.copy(writable, bytes)

    let parsed: Maybe<u16> = std.parse.parseU16("8080")
    if parsed.has {
        expect parsed.value == 8080
    }

    let checksum: u32 = std.codec.crc32("zero")
    let crc: u32 = std.codec.crc32Bytes(bytes)
    let hash: u32 = std.crypto.hash32(bytes)
    let hmac: u32 = std.crypto.hmac32(std.mem.span("key"), std.mem.span("message"))

    var reversed: [4]u8 = [0, 0, 0, 0]
    let reversed_text: Maybe<Span<u8>> = std.str.reverse(reversed, "zero")
    if reversed_text.has {
        expect std.mem.eql(reversed_text.value, "orez")
    }

    var rng: RandSource = std.rand.seed(7_u32)
    let random: u32 = std.rand.nextU32(&mut rng)

    let duration: Duration = std.time.add(std.time.ms(250), std.time.seconds(1))
    expect std.time.asMsFloor(duration) == 1250
}
```

Hosted helpers are capability-gated by target:

```zero
pub fn main(world: World) -> Void raises {
    let count: usize = std.args.len()
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        check world.out.write(first.value)
    }

    var path_storage: [16]u8 = [0; 16]
    let path: String = check std.path.join(path_storage, ".zero", "x")

    let fs: Fs = std.fs.host()
    var file: owned<File> = check std.fs.createOrRaise(fs, path)
    check std.fs.writeAllOrRaise(&mut file, std.mem.span("hello\n"))

    let status: ProcStatus = std.proc.spawn("zero-noop")
    if std.proc.exitCode(status) == 0 {
        check world.out.write("proc ok\n")
    }
}
```

## Compact Examples

Use these as small pattern anchors when generating code:

```zero
type Point {
    x: i32,
    y: i32,
}

fn one() -> i32 {
    return 1
}

fn two() -> i32 {
    return 1 + 1
}

fn sub(a: i32, b: i32) -> i32 {
    return a - b
}

fn even(n: i32) -> Bool {
    return n % 2 == 0
}

fn max(a: i32, b: i32) -> i32 {
    if a > b {
        return a
    }
    return b
}

fn sum_to(n: i32) -> i32 {
    var i: i32 = 0
    var total: i32 = 0
    while i <= n {
        total = total + i
        i = i + 1
    }
    return total
}

fn fib(n: u32) -> u32 {
    var i: u32 = 0
    var a: u32 = 0
    var b: u32 = 1
    while i < n {
        let next: u32 = a + b
        a = b
        b = next
        i = i + 1
    }
    return a
}

fn factorial(n: u32) -> u32 {
    var i: u32 = 2
    var total: u32 = 1
    while i <= n {
        total = total * i
        i = i + 1
    }
    return total
}

fn point_sum(point: Point) -> i32 {
    return point.x + point.y
}

test "shape" {
    let point: Point = Point { x: 40, y: 2 }
    expect point_sum(point) == 42
}
```

If unsure, run `zero check <file>` instead of inventing syntax. Add `--zdn` (preferred for agents) or `--json` only when you need structured fields.
