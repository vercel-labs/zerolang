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
| `std.testing.isTrue(value)` | `Bool` | Passes through a `Bool` for readable `expect` statements. |
| `std.testing.isFalse(value)` | `Bool` | Returns true when the value is false. |
| `std.testing.equalBool(actual, expected)` | `Bool` | Compares booleans explicitly. |
| `std.testing.equalUsize(actual, expected)` | `Bool` | Compares `usize` values explicitly. |
| `std.testing.equalU32(actual, expected)` | `Bool` | Compares `u32` values explicitly. |
| `std.testing.equalI32(actual, expected)` | `Bool` | Compares `i32` values explicitly. |
| `std.testing.equalBytes(actual, expected)` | `Bool` | Compares byte spans by value. |
| `std.testing.containsBytes(actual, needle)` | `Bool` | Checks whether a byte span contains a byte substring. |
| `std.testing.startsWith(actual, prefix)` | `Bool` | Checks a byte prefix. |
| `std.testing.endsWith(actual, suffix)` | `Bool` | Checks a byte suffix. |

Metadata labels:

- effects: none for scalar comparisons; memory for byte-span checks
- allocation behavior: no allocation
- target support: target-neutral
- error behavior: infallible
- ownership notes: no ownership transfer
- example: `examples/std-testing-log.graph`

## Example

```zero
test "testing helpers support direct test blocks" {
    expect std.testing.equalU32(42_u32, 42_u32)
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
}
```

## Design Notes

`std.testing` helpers return `Bool`; they do not register tests, hide failures,
allocate output, or produce process I/O. Use them inside ordinary `expect`
statements so the compiler and `zero test` keep one visible test model.

The byte helpers are byte-span predicates. They are suitable for output checks,
protocol fixtures, and small examples where a full parser would be more complex
than the assertion.
