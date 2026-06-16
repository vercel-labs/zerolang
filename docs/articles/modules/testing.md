## When To Use std.testing

In Zerolang, use `std.testing` inside test blocks for output checks and small boolean
assertion helpers.

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
| `std.testing.notEqualBytes(actual, expected)` | `Bool` | Checks byte-span inequality. |
| `std.testing.diffIndexBytes(actual, expected)` | `Maybe<usize>` | Returns the first differing byte index, or `null` when spans are equal. |
| `std.testing.jsonFieldEquals(actual, key, expected)` | `Bool` | Compares a raw top-level JSON field value. |
| `std.testing.jsonPathEquals(actual, path, expected)` | `Bool` | Compares a raw dotted JSON path value. |
| `std.testing.caseName(buffer, suite, index)` | `Maybe<Span<u8>>` | Writes a stable table-case name like `suite[3]` into caller storage. |

Metadata labels:

- effects: none for scalar comparisons; memory for byte-span/case-name checks; parse for JSON checks
- allocation behavior: no allocation
- target support: target-neutral
- error behavior: infallible
- ownership notes: no ownership transfer
- example: `examples/std-testing-log.graph`

## Example

```zero
test "testing helpers support direct test blocks" {
    let diff: Maybe<usize> = std.testing.diffIndexBytes("zero", "zeta")
    expect std.testing.equalU32(42_u32, 42_u32)
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
    expect diff.has && diff.value == 2
    expect std.testing.jsonPathEquals("{\"user\":{\"name\":\"zero\"}}", "user.name", "\"zero\"")
}
```

## Design Notes

`std.testing` helpers return `Bool`; they do not register tests, hide failures,
allocate output, or produce process I/O. Use them inside ordinary `expect`
statements so the compiler and `zero test` keep one visible test model.

The byte helpers are byte-span predicates. They are suitable for output checks,
protocol fixtures, and small examples where a full parser would be more complex
than the assertion.

JSON helpers compare raw JSON values, so string expectations include their JSON
quotes, such as `"\"zero\""`.
