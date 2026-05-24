---
name: language
description: Compact zerolang syntax and semantics guide for agents.
---

# zerolang Language

Use this when writing or reviewing `.0` source, especially if the model has no prior zerolang training. Prefer the row patterns below; they are the most reliable shapes for generation.

## Minimal Program

```zero
pub fn main Void world World !
  check world.out.write "hello from zerolang\n"
```

`pub fn` exports a function. `World` carries runtime capabilities. `!` marks a fallible function. `check` calls a fallible operation and propagates failure.

## Row Syntax

One statement or declaration starts each row. Child rows are indented two spaces. Calls are prefix rows:

```zero
fn add i32 a i32 b i32
  ret + a b

pub fn main Void world World !
  let value i32 add 40 2
  if == value 42
    check world.out.write "math works\n"
```

Use `name()` for a zero-argument call. Use parentheses only to group nested expressions:

```zero
let value i32 answer()
let sum i32 add 40 2
let hmac u32 std.crypto.hmac32 (std.mem.span "key") (std.mem.span "message")
let ok Bool && (== sum 42) (!= hmac 0_u32)
```

Operators are prefix calls: `+ a b`, `- a b`, `* a b`, `% a b`, `== a b`, `< a b`, `&& a b`. Comments start with `//`.

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

## Values, Mutation, And Control Flow

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

```zero
fn count usize n usize
  mut i usize 0
  while < i n
    set i + i 1
  ret i
```

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
fn value i32 i i32 ![Odd]
  if == (% i 2) 0
    ret i
  raise Odd

pub fn main Void world World !
  let item i32 rescue (value 3) err 1
  if == item 1
    check world.out.write "fallible ok\n"
```

`![...]` restricts the error set. A plain `!` marker is open. Calling a fallible function requires `check` or `rescue`.

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

Arrays and views:

```zero
mut storage [4]u8 [1, 2, 3, 4]
let view MutSpan<u8> storage
let first u8 storage[0]
set storage[0] 9
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

## Standard Library Call Shapes

Common target-neutral helpers:

```zero
let bytes Span<u8> std.mem.span "zero"
let n usize std.mem.len bytes
let same Bool std.mem.eql "zero" "zero"

mut dst [8]u8 [0, 0, 0, 0, 0, 0, 0, 0]
let writable MutSpan<u8> dst
let copied usize std.mem.copy writable bytes

let parsed std.parse.parseU16 "8080"
if parsed.has
  expect (== parsed.value 8080)

let checksum u32 std.codec.crc32 "zero"
let crc u32 std.codec.crc32Bytes bytes
let hash u32 std.crypto.hash32 bytes
let hmac u32 std.crypto.hmac32 (std.mem.span "key") (std.mem.span "message")

mut rng std.rand.seed 7_u32
let random u32 std.rand.nextU32 (&mut rng)

let duration std.time.add (std.time.ms 250) (std.time.seconds 1)
expect (== (std.time.asMsFloor duration) 1250)
```

Hosted helpers are capability-gated by target:

```zero
let count usize std.args.len()
let first std.args.get 1
if first.has
  check world.out.write first.value

mut path_storage [16]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
let path check std.path.join path_storage ".zero" "x"

let fs std.fs.host()
mut file owned<File> check std.fs.createOrRaise fs path
check std.fs.writeAllOrRaise (&mut file) bytes

let status std.proc.spawn "zero-noop"
if == (std.proc.exitCode status) 0
  check world.out.write "proc ok\n"
```

## Compact Examples

Use these as small pattern anchors when generating code:

```zero
fn one i32
  ret 1

fn two i32
  ret + 1 1

fn sub i32 a i32 b i32
  ret - a b

fn even Bool n i32
  ret == (% n 2) 0

fn max i32 a i32 b i32
  if > a b
    ret a
  ret b

fn sum_to i32 n i32
  mut i i32 0
  mut total i32 0
  while <= i n
    set total + total i
    set i + i 1
  ret total

fn fib u32 n u32
  mut i u32 0
  mut a u32 0
  mut b u32 1
  while < i n
    let next u32 + a b
    set a b
    set b next
    set i + i 1
  ret a

fn factorial u32 n u32
  mut i u32 2
  mut total u32 1
  while <= i n
    set total * total i
    set i + i 1
  ret total

type Point
  x i32
  y i32

fn point_sum i32 point Point
  ret + point.x point.y

test "shape"
  let point Point . x 40 y 2
  expect (== (point_sum point) 42)
```

If unsure, run `zero check --json <file>` and use the diagnostic span instead of inventing syntax.
