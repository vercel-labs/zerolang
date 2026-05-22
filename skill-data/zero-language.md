---
name: zero-language
description: Compact Zero syntax and semantics guide for agents.
---

# Zero Language

Use this when writing or reviewing `.0` source, especially if the model has no prior Zero training. Zero favors explicit capabilities, explicit errors, and small syntax.

## Minimal Program

```zero
pub fn main Void world World !
  check world.out.write "hello from zero\n"
```

`pub fn` exports a function. `World` carries runtime capabilities. `!` marks a fallible function. `check` calls a fallible operation and propagates failure.

## Declarations

Top-level declarations include:

- `use std.mem` or `use helpers`
- `const answer i32 42`
- `type Point` with indented fields such as `x i32`
- `enum Mode` with indented cases such as `off`
- `choice Result` with indented cases such as `ok i32`
- `fn answer i32` with an indented `ret 42`
- `pub fn main Void world World !`
- `test "name"` with an indented `expect true`

Use `.0` for source files.

## Values And Control Flow

```zero
fn answer i32
  ret + 40 2

pub fn main Void world World !
  let value answer()
  if == value 42
    check world.out.write "math works\n"
  else
    check world.out.write "math broke\n"
```

Use `let` by default and `mut` only when a binding changes. Conditions are `Bool`; do not rely on truthy integers or strings.

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
type Point
  x i32
  y i32

enum Mode
  fast
  small

choice Result
  ok i32
  err String
```

Construct a shape with field names:

```zero
let point Point . x 1 y 2
```

Choice payload cases use the choice name:

```zero
let result Result Result.ok 42
```

Matches must be exhaustive unless they use the fallback arm `_`:

```zero
match result
  ok value
    expect (== value 42)
  err message
    expect true
```

## Errors

```zero
fn parse i32 ok Bool ![InvalidInput]
  if ok
    ret 42
  raise InvalidInput

pub fn main Void ![InvalidInput]
  let value check (parse true)
  expect (== value 42)
```

`![...]` restricts the error set. A plain `!` marker is open. Calling a fallible function requires `check`.

## Borrowing And Memory Views

- `ref<T>` is a read-only borrow, passed with `&value`.
- `mutref<T>` is a mutable borrow, passed with `&mut value`.
- `[N]T` is a fixed array.
- `Span<T>` is a read-only contiguous view.
- `MutSpan<T>` is a writable contiguous view.
- `Maybe<T>` represents absence; inspect `.has` and `.value`.
- `owned<T>` marks explicit resource ownership.

```zero
fn bump Void point mutref<Point>
  set point.x + point.x 1
```

## Generics

```zero
type Box<T: Type>
  value T

fn id<T: Type> T value T
  ret value
```

Static generic values are declared with `static`:

```zero
type FixedVec<T: Type, static N: usize>
  len usize
  items [N]T
```

If unsure, run `zero check --json <file>` and use the diagnostic span instead of inventing syntax.
