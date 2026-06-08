## Graph Surface

This module is graph-backed. The compiler uses its standard-library graph store,
while the Zero snippets below show the human-readable projection that agents may
export for review. Agents should discover helpers with `zero skills get stdlib`,
inspect user packages with `zero query [graph-input]` or
`zero inspect [graph-input]`, and patch user code through the graph instead of
hand-editing `.0` files.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.math.minU32(left, right)` | `u32` | Returns the smaller unsigned value. |
| `std.math.minI32(left, right)` | `i32` | Returns the smaller signed 32-bit value. |
| `std.math.minUsize(left, right)` | `usize` | Returns the smaller pointer-width unsigned value. |
| `std.math.minI64(left, right)` | `i64` | Returns the smaller signed 64-bit value. |
| `std.math.minU64(left, right)` | `u64` | Returns the smaller unsigned 64-bit value. |
| `std.math.maxU32(left, right)` | `u32` | Returns the larger unsigned value. |
| `std.math.maxI32(left, right)` | `i32` | Returns the larger signed 32-bit value. |
| `std.math.maxUsize(left, right)` | `usize` | Returns the larger pointer-width unsigned value. |
| `std.math.maxI64(left, right)` | `i64` | Returns the larger signed 64-bit value. |
| `std.math.maxU64(left, right)` | `u64` | Returns the larger unsigned 64-bit value. |
| `std.math.clampU32(value, low, high)` | `u32` | Clamps between the two bounds; swapped bounds are normalized. |
| `std.math.clampI32(value, low, high)` | `i32` | Clamps a signed 32-bit value; swapped bounds are normalized. |
| `std.math.clampUsize(value, low, high)` | `usize` | Clamps a pointer-width unsigned value; swapped bounds are normalized. |
| `std.math.clampI64(value, low, high)` | `i64` | Clamps a signed 64-bit value; swapped bounds are normalized. |
| `std.math.clampU64(value, low, high)` | `u64` | Clamps an unsigned 64-bit value; swapped bounds are normalized. |
| `std.math.absI32(value)` | `u32` | Returns the unsigned magnitude of a signed 32-bit value. |
| `std.math.absI64(value)` | `u64` | Returns the unsigned magnitude of a signed 64-bit value. |
| `std.math.checkedAddU32(left, right)` | `Maybe<u32>` | Adds only when the result fits in `u32`. |
| `std.math.checkedSubU32(left, right)` | `Maybe<u32>` | Subtracts only when the result fits in `u32`. |
| `std.math.checkedMulU32(left, right)` | `Maybe<u32>` | Multiplies only when the result fits in `u32`. |
| `std.math.checkedAddUsize(left, right)` | `Maybe<usize>` | Adds only when the result fits in `usize`. |
| `std.math.checkedSubUsize(left, right)` | `Maybe<usize>` | Subtracts only when the result fits in `usize`. |
| `std.math.checkedMulUsize(left, right)` | `Maybe<usize>` | Multiplies only when the result fits in `usize`. |
| `std.math.checkedAddI32(left, right)` | `Maybe<i32>` | Adds only when the result fits in `i32`. |
| `std.math.checkedSubI32(left, right)` | `Maybe<i32>` | Subtracts only when the result fits in `i32`. |
| `std.math.checkedMulI32(left, right)` | `Maybe<i32>` | Multiplies only when the result fits in `i32`. |
| `std.math.saturatingAddU32(left, right)` | `u32` | Adds and clamps overflow to `u32` max. |
| `std.math.saturatingSubU32(left, right)` | `u32` | Subtracts and clamps underflow to `0`. |
| `std.math.saturatingMulU32(left, right)` | `u32` | Multiplies and clamps overflow to `u32` max. |
| `std.math.saturatingAddUsize(left, right)` | `usize` | Adds and clamps overflow to `usize` max. |
| `std.math.saturatingSubUsize(left, right)` | `usize` | Subtracts and clamps underflow to `0`. |
| `std.math.saturatingMulUsize(left, right)` | `usize` | Multiplies and clamps overflow to `usize` max. |
| `std.math.saturatingAddI32(left, right)` | `i32` | Adds and clamps overflow to the nearest `i32` bound. |
| `std.math.saturatingSubI32(left, right)` | `i32` | Subtracts and clamps overflow to the nearest `i32` bound. |
| `std.math.saturatingMulI32(left, right)` | `i32` | Multiplies and clamps overflow to the nearest `i32` bound. |
| `std.math.gcdU32(left, right)` | `u32` | Euclidean greatest common divisor. |
| `std.math.lcmU32(left, right)` | `u32` | Least common multiple; returns `0` when either input is `0`. |
| `std.math.checkedLcmU32(left, right)` | `Maybe<u32>` | Least common multiple only when the result fits in `u32`. |
| `std.math.powU32(base, exponent)` | `u32` | Fixed-width exponentiation by squaring. |
| `std.math.checkedPowU32(base, exponent)` | `Maybe<u32>` | Exponentiation only when the result fits in `u32`. |
| `std.math.modPowU32(base, exponent, modulus)` | `u32` | Modular exponentiation; returns `0` for modulus `0`. |
| `std.math.isPrimeU32(value)` | `Bool` | Trial division primality for unsigned integers. |
| `std.math.isEvenU32(value)` | `Bool` | Reports whether a `u32` value is even. |
| `std.math.isOddU32(value)` | `Bool` | Reports whether a `u32` value is odd. |
| `std.math.sqrtFloorU32(value)` | `u32` | Integer square root rounded down. |
| `std.math.factorialU32(value)` | `Maybe<u32>` | Factorial only when the result fits in `u32`. |
| `std.math.binomialU32(n, k)` | `Maybe<u32>` | Binomial coefficient only when the result fits in `u32`. |
| `std.math.divisorCountU32(value)` | `u32` | Counts positive divisors; returns `0` for `0`. |
| `std.math.properDivisorSumU32(value)` | `u32` | Sums positive divisors smaller than `value`. |

Current scope:

- Helpers are pure, target-neutral fixed-width integer operations.
- Checked helpers return `Maybe<T>` instead of wrapping or trapping.
- Saturating helpers clamp to documented integer bounds.
- The module does not provide floating-point math, big integers, or arbitrary-precision number theory.

## Example

```zero
pub fn main(world: World) -> Void raises {
    if std.math.gcdU32(84, 30) == 6 && std.math.isPrimeU32(31) {
        check world.out.write("math helper ok\n")
    }
}
```

## Design Notes

`std.math` does not allocate and does not require a hosted runtime capability.
Names include the integer width because Zero does not overload standard-library
helpers by argument type.

Number-theory helpers are intentionally simple and deterministic. They are
suitable for small fixed-width tasks, examples, and compiler-portable checks.
Large-number algorithms should be added as explicit APIs with documented bounds
instead of hidden heap allocation or implicit widening.
