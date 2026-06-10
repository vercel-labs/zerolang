---
name: language
description: Compact zerolang syntax and semantics guide for agents.
---

# zerolang Language

Use this when reading or writing Zero source. `.0` files are the
human-readable projection syntax, not the package compiler input: package
commands compile from `zero.graph` and refresh it automatically after `.0`
edits. Read one function with `zero view --fn <name>` instead of whole files.

## Minimal Program

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello from zerolang\n")
}
```

`pub fn` exports a function. `World` carries runtime capabilities. `raises` marks a fallible function. `check` calls a fallible operation and propagates failure.

## Declarations

- `use std.mem` or `use helpers`
- `const answer: i32 = 42` (top-level consts work in functions and tests)
- `type Point { x: i32, y: i32, }`
- `enum Mode { fast, small, }`
- `choice Result { ok: i32, err: String, }`
- `fn answer() -> i32 { return 42 }`
- `pub fn main(world: World) -> Void raises { ... }`
- `test "name" { expect true }`

## Values, Mutation, And Control Flow

Use `let` by default and `var` only when a binding changes. Conditions are `Bool`; do not rely on truthy integers or strings. Operators are normal infix expressions: `a + b`, `a % b`, `a == b`, `a < b`, `a && b`. Comments start with `//`.

```zero
fn sum_odds(n: i32) -> i32 {
    var i: i32 = 0
    var total: i32 = 0
    while i < n {
        i = i + 1
        if i % 2 == 0 {
            continue
        }
        if total > 100 {
            break
        }
        total = total + i
    }
    return total
}
```

`break` exits the nearest loop and `continue` skips to its next iteration.

## Types

Primitive and scalar types:

```text
Bool Void String char Type
i8 i16 i32 i64 isize
u8 u16 u32 u64 usize
f32 f64
```

Bare integer literals adopt the other operand's integer type when in range, so `i * 12` works for `i: usize`. Use suffixes such as `_u8` or `_usize` only when no operand gives context, and `as` for intentional casts.

Core capability, resource, and helper types recognized by the compiler:

```text
World WorldStream
Fs File ByteBuf
NullAlloc FixedBufAlloc PageAlloc GeneralAlloc
Vec Duration RandSource ProcStatus
Address Net Conn Listener
HttpMethod HttpClient HttpServer HttpResult HttpError HttpHeaderValue
JsonDoc BufferedReader BufferedWriter
Env Args Clock Rand Proc Alloc
Maybe<T> Span<T> MutSpan<T>
ref<T> mutref<T> owned<T>
```

## Shapes, Enums, And Choices

```zero
let point: Point = Point { x: 1, y: 2 }
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
- `mutref<T>` is a mutable borrow, passed with `&mut value`. Shape parameters such as `mutref<Point>` work, including field mutation and nested calls.
- `[N]T` is a fixed array; `Span<T>` is a read-only view; `MutSpan<T>` is a writable view.
- Returning a span backed by local fixed-array storage is rejected; return an owned value or keep the view local.
- `Maybe<T>` represents absence; read `.value` only inside a visible `.has` guard, or use `check` / `rescue`. `var name: Maybe<String> = null` declares an absent local to assign or return later.
- `owned<T>` marks explicit resource ownership.
- One function's fixed locals are limited to 128 KiB (MEM003). Split large buffers into smaller buffers in helper functions, or process data in fixed-size chunks.

```zero
fn bump(point: mutref<Point>) -> Void {
    point.x = point.x + 1
}

pub fn main() -> Void {
    var storage: [4]u8 = [1, 2, 3, 4]
    let view: MutSpan<u8> = storage
    let first: u8 = storage[0]
    storage[0] = 9
}
```

Array and span indexing is bounds-checked at runtime; an out-of-range index traps with a signal exit and no output, so exercise boundary inputs before declaring a change done.

## Generics

```zero
type Box<T: Type> {
    value: T,
}

fn id<T: Type>(value: T) -> T {
    return value
}

type FixedVec<T: Type, static N: usize> {
    len: usize,
    items: [N]T,
}
```

## Standard Library Call Shapes

Load `zero skills get stdlib` for the full signature catalog. Target-neutral helpers follow these shapes:

```zero
pub fn main() -> Void {
    let bytes: Span<u8> = std.mem.span("zero")
    let n: usize = std.mem.len(bytes)
    let same: Bool = std.mem.eql("zero", "zero")

    let parsed: Maybe<u16> = std.parse.parseU16("8080")
    if parsed.has {
        expect parsed.value == 8080
    }

    var rng: RandSource = std.rand.seed(7_u32)
    let random: u32 = std.rand.nextU32(&mut rng)
    let count: Maybe<u32> = std.json.u32("{\"count\":42}", "count")
    expect same && random == 1025555898_u32 && count.has
}
```

Hosted helpers are capability-gated by target. There is no stdin; hosted programs take input from `std.args` and files through `std.fs`:

```zero
pub fn main(world: World) -> Void raises {
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        check world.out.write(first.value)
    }

    var path_storage: [16]u8 = [0; 16]
    let path: String = check std.path.join(path_storage, ".zero", "x")
    let fs: Fs = std.fs.host()
    var file: owned<File> = check std.fs.createOrRaise(fs, path)
    check std.fs.writeAllOrRaise(&mut file, std.mem.span("hello\n"))
}
```

If unsure, run `zero check` instead of inventing syntax, and pair behavior changes with a `test` block:

```zero
fn point_sum(point: Point) -> i32 {
    return point.x + point.y
}

test "shape" {
    let point: Point = Point { x: 40, y: 2 }
    expect point_sum(point) == 42
}
```
