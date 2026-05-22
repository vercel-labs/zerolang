## Scalar Values

| Primitive | Purpose | Current status |
| --- | --- | --- |
| `Bool` | Conditions and logical results. Only `Bool` is accepted in `if` and `while` conditions. | Runnable |
| `i8` `i16` `i32` `i64` | Signed fixed-width integers. | Runnable |
| `u8` `u16` `u32` `u64` | Unsigned fixed-width integers. | Runnable |
| `usize` `isize` | Pointer-sized integers. | Runnable |
| `f32` `f64` | Floating-point values. | Runnable |
| `char` | Byte-sized character value for ASCII/parser/codec-style work. | Runnable |
| `String` | Text value used by string literals and current I/O examples. | Runnable |
| `Void` | Return type for functions that produce no useful value. | Runnable |

Integer literals support decimal, hexadecimal, binary, octal, `_` separators, and optional suffixes such as `_u8` or `_usize`.

```zero
let count u32 0x12c_u32
let byte u8 255
let page usize 4_096
```

Primitive numeric types do not implicitly narrow, widen, or change signedness. Use an explicit cast when the conversion is intentional.

```zero
let count u32 300
let byte u8 count as u8
let whole i32 7.9 as i32
let marker u8 'A' as u8
```

## Absence And Fallibility

`Maybe<T>` represents an optional value. `null` is accepted only when the expected type is known to be `Maybe<T>`.

```zero
let name Maybe<String> null
let fallback name else "world"
```

Fallible functions are marked with `!` or `![...]`, and `check` propagates the current
error. Fallibility is part of a function signature rather than a runtime exception
mechanism.

The native compiler accepts explicit error sets for user-defined fallible
functions.

```zero
fn validate i32 ok Bool ![InvalidInput]
  if == ok false
    raise InvalidInput
  ret 42

fn run Void ![InvalidInput]
  check validate true
```

The native compiler lowers `check` on `Maybe<T>` to a direct branch for the
first recoverable helper slice. A failed `Maybe` check returns the function's
default failure value without exceptions or unwinding.

User-defined error flow lowers to small status/result structs only when a
function actually raises named errors.

## Memory And Ownership

Zero makes memory layout visible in types. These forms are primitive type constructors because they affect ownership, borrowing, layout, and cleanup.

| Primitive | Purpose |
| --- | --- |
| `[N]T` | Fixed-size array with `N` elements of `T`. |
| `Span<T>` | Non-owning pointer plus length. |
| `ref<T>` | Non-owning shared reference. |
| `mutref<T>` | Non-owning mutable reference. |
| `owned<T>` | Move-only value that owns cleanup responsibility. |
| `const T` | Read-only view of `T`. |

```zero
type BufferView
  bytes Span<u8>
  owner Maybe<mutref<Alloc>>

pub fn len usize view BufferView
  ret std.mem.len view.bytes
```

`Alloc` is a capability type used by allocation APIs. Heap allocation should be
visible in function parameters and documentation; there is no hidden global
allocator.

Allocator primitives are explicit handles:

| Primitive | Behavior |
| --- | --- |
| `NullAlloc` | Always reports allocation failure, useful for proving a path does not allocate. |
| `FixedBufAlloc` | Allocates from caller-owned mutable bytes and returns borrowed `MutSpan<u8>` views. |
| `std.mem.arena(buffer)` | Arena-style fixed-buffer allocation over caller storage. |
| `PageAlloc`, `GeneralAlloc` | Explicit handles, not ambient global heaps. |
| `std.mem.byteBuf(alloc, len)` | Returns `Maybe<owned<ByteBuf>>` backed by explicit caller storage. |

Borrow expressions create references without allocation or runtime metadata. Use `&value` for `ref<T>` and `&mut value` for `mutref<T>`.

```zero
fn read_x i32 point ref<Point>
  ret point.x

fn write_x Void point mutref<Point> value i32
  set point.x value

let shared &point
write_x (&mut point) 5
```

An `owned<T>` local is automatically cleaned up at lexical scope exit when `T`
defines the canonical non-raising method
`fn drop Void self mutref<Self>`.

Cleanup is lowered to a direct call and skipped once the owned binding has
moved.

```zero
type Temp
  bytes MutSpan<u8>

  fn drop Void self mutref<Self>
    set self.bytes[0] 0
```

## Layout Primitives

User-defined types are not primitives, but some layout markers are primitive because they define how values cross ABI or binary boundaries.

| Form | Purpose |
| --- | --- |
| `type` | Default Zero aggregate layout. Not ABI-stable by default. |
| `extern type` | C ABI-compatible aggregate layout for the selected target. |
| `packed type` | Bit-exact layout with declared field widths. |
| `enum Name : uN` | Enum with an explicit integer backing type. |
| `choice` | Tagged choice value. Exhaustive matching is required. |

```zero
extern type CPoint
  x i32
  y i32

enum Color u8
  red
  green
  blue
```

## Capability Names Are Not Primitives

Names such as `World`, `Fs`, `Net`, `Env`, `Args`, `Clock`, `Rand`, and `Proc`
are capability surfaces.

They are foundational to Zero's effect model, but they are not primitive values
in the same sense as `Bool`, `u32`, `Maybe<T>`, or `Span<T>`.

```zero
pub fn main Void world World !
  check world.out.write "hello\n"
```

## Current Native Status

The native compiler currently supports:

- checked primitive integer widths and exact C integer types
- explicit casts among primitive integers, floats, and byte `char`
- memory-oriented generic forms
- `Maybe<T>` and native layouts for span views
- source-level `ref<T>` and `mutref<T>` borrows
- lexical moves for `owned<T>`
- direct `drop` cleanup calls for live owned locals
- compiler-known `owned<File>` cleanup
- allocation through `NullAlloc`, mutable `FixedBufAlloc`, and `ByteBuf`

None of these ownership features add runtime ownership machinery.

Not part of the current native status:

- `f16`
- Unicode scalar character handling
- fuller borrow and alias analysis
- `Arena` and general allocator behavior
- drop glue for generic containers
- more exhaustive layout conformance across target ABIs
