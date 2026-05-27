## Status

Runnable today (type-check passes via `bin/zero check`):

| API | Return | Notes |
| --- | --- | --- |
| `std.decimal.parse(text, scale)` | `DecimalResult` | Parses `"67432.18"` with `scale=2` to mantissa `6743218`. `.has=false` on overflow, malformed input, or more fractional digits than `scale`. |
| `std.decimal.toString(buffer, mantissa, scale)` | `Maybe<Span<u8>>` | Writes a fixed-scale decimal into the caller-owned buffer. Returns `null` if the buffer is too small. |
| `std.decimal.add(a, b)` | `DecimalResult` | Overflow-checked i64 addition; operands must share scale. |
| `std.decimal.sub(a, b)` | `DecimalResult` | Overflow-checked i64 subtraction; operands must share scale. |
| `std.decimal.mul(a, b)` | `DecimalResult` | Overflow-checked i64 multiplication via post-condition inverse. Caller manages scale combination. |
| `std.decimal.div(a, b)` | `DecimalResult` | i64 truncating division. `.has=false` on divide-by-zero. |
| `std.decimal.cmp(a, b)` | `i32` | Returns `-1`, `0`, or `1` (libc `qsort` convention). Operands must share scale. |
| `std.decimal.abs(value)` | `DecimalResult` | Fallible: `.has=false` for `i64.MIN` whose negation overflows. |

The `DecimalResult` shape is declared in this module:

```zero
pub type DecimalResult
  has Bool
  value i64
```

It mirrors the `Maybe<T>` caller API (`.has`, `.value`) and is used because
source-backed Zero cannot construct a non-null `Maybe<primitive>` from source
today; only C-implemented helpers can return `Maybe<i64>`, `Maybe<u32>`, or
`Maybe<usize>`. See issue #305 for the design conversation.

Current scope:

- All helpers operate on i64 mantissa with a caller-managed `u8` scale.
- Maximum precision is 18 decimal digits before overflow guards trip.
- The module does not provide 128-bit arithmetic, arbitrary precision,
  rounding-mode selection, or floating-point conversion. Those are
  intentional scope-out decisions; see issue #305.
- The direct AArch64 / x86_64 backends do not yet lower function calls that
  return user-defined record types (`BLD004`). The conformance runtime
  fixture for this module uses the `assertDirectRuntimeOrUnsupported` path
  and accepts that outcome. Runtime behavior is exercisable via the
  C-emitter target or via `bin/zero check` today.

## Example

```zero
pub fn main Void world World !
  let price std.decimal.parse "67432.18" 2_u8
  let sats i64 100000000_i64
  if price.has
    let product std.decimal.mul price.value sats
    if product.has
      check world.out.write "decimal mul ok\n"
```

## Design Notes

`std.decimal` does not allocate and does not require a hosted runtime
capability. Operations that can fail at runtime (parsing, all arithmetic,
`abs`) return `DecimalResult`, matching the `std.parse` precedent of surfacing
absence rather than allocating diagnostics; callers can layer richer errors on
top.

The caller-managed `(mantissa, scale)` pattern keeps the implementation
target-neutral and avoids hidden heap allocation or implicit widening — both
constraints the `std.math` design note explicitly establishes. A
shape-returning ergonomic layer (`Decimal { mantissa, scale }`) and 128-bit
precision extension are deliberate follow-ups tracked in #305.
