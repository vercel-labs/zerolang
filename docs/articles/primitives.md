## The Pieces The Graph Stores

Zerolang exposes language pieces as graph facts and as `.0`
projection syntax. The graph stores the type and layout facts. The projection
lets humans read them.

## Scalar Values

| Type | Purpose |
| --- | --- |
| `Bool` | Conditions and logical results. |
| `i8` `i16` `i32` `i64` | Signed fixed-width integers. |
| `u8` `u16` `u32` `u64` | Unsigned fixed-width integers. |
| `usize` `isize` | Pointer-sized integers. |
| `f32` `f64` | Floating-point values. |
| `char` | Byte-sized character value for ASCII/parser/codec work. |
| `String` | Text value used by string literals and current I/O examples. |
| `Void` | Return type for functions that produce no useful value. |

Integer literals support decimal, hexadecimal, binary, octal, `_` separators,
and optional suffixes such as `_u8` or `_usize`. An unsuffixed integer literal
adopts the type of a typed integer operand in arithmetic and comparisons when
the value fits, so `index + 1` and `index < 10` work when `index` is `usize`.
Out-of-range literals are rejected, so `byte > 300` fails for a `u8` operand.

```zero
let count: u32 = 0x12c_u32
let byte: u8 = 255
let page: usize = 4_096
```

Primitive numeric types do not implicitly narrow, widen, or change signedness.
Use an explicit cast when the conversion is intentional.

```zero
let count: u32 = 300
let byte: u8 = count as u8
```

## Absence

`Maybe<T>` represents an optional value:

```zero
let parsed: Maybe<u32> = std.args.parseU32(1)
if parsed.has {
    return parsed.value
}
return 0
```

`.value` reads require a visible `.has` guard or fallible handling. That rule is
part of the graph semantics, not a formatter convention.

## Fixed Storage And Views

| Type form | Meaning |
| --- | --- |
| `[N]T` | Fixed-size array with `N` elements of `T`. |
| `Span<T>` | Read-only borrowed pointer plus length. |
| `MutSpan<T>` | Mutable borrowed pointer plus length. |
| `ref<T>` | Immutable reference. |
| `mutref<T>` | Mutable reference. |

```zero
var scratch: [16]u8 = [0_u8; 16]
let bytes: Span<u8> = std.mem.span("hello")
let copied: usize = std.mem.copy(scratch, bytes)
```

These types are central to Zero's size and memory model. Helpers generally
write into caller-owned storage so allocation behavior remains visible.

## Ownership

Owned values use explicit ownership forms:

```zero
fn drop(self: mutref<Self>) -> Void {
    return
}
```

The canonical non-raising `fn drop(self: mutref<Self>) -> Void` shape lets the
graph model cleanup without a hidden runtime cleanup registry. Owned resources,
allocators, and cleanup behavior should be visible through graph inspection.

## User Types

```zero
type Point {
    x: i32,
    y: i32,
}
```

Fields, defaults, and constructor-like projections are graph declarations and
edges. Public type surfaces should stay explicit because agents rely on stable
field and type facts.

## Enums And Choices

```zero
enum Status {
    Pending,
    Ready,
}
```

Enums are named value sets. Choices and payload-bearing cases are represented
as graph facts so `match` can be checked semantically.

## Fallibility

Fallible functions use `raises`:

```zero
fn validate(ok: Bool) -> i32 raises [InvalidInput] {
    if !ok {
        raise InvalidInput
    }
    return 42
}
```

`check` propagates failure explicitly. There is no hidden exception system.

## Compile-Time Values

Compile-time facts currently cover bounded integer, `Bool`, and enum static
values. The metadata surface includes facts such as `compileTime`,
`target.pointerWidth`, `fieldType`, and `hasEnumCase`.

Use `zero inspect --json` or `zero check --json` when an agent needs those facts
for a patch.

## Projection Examples

Projection syntax is for humans. The graph stores the same facts directly.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "what types does this helper use?"
    },
    {
      "role": "assistant",
      "text": "I’ll inspect the function facts and summarize the types."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query --fn add",
          "output": "fn add(x: i32, y: i32) -> i32\n  return x + y"
        }
      ]
    }
  ]
}
```

For manual review, export the projection:

```sh
zero export
```
