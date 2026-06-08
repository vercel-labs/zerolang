## Graph Surface

This module is graph-backed. The compiler uses its standard-library graph store,
while the Zero snippets below show the human-readable projection that agents may
export for review. Agents should discover helpers with `zero skills get stdlib`,
inspect user packages with `zero query [graph-input]` or
`zero inspect [graph-input]`, and patch user code through the graph instead of
hand-editing `.0` files.

Runnable today:

Formatting helpers write into caller-provided `MutSpan<u8>` storage and return a
borrowed prefix on success.

| API | Return | Notes |
| --- | --- | --- |
| `std.fmt.bool(buffer, value)` | `Maybe<Span<u8>>` | Writes `true` or `false`. |
| `std.fmt.u32(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal unsigned 32-bit text. |
| `std.fmt.usize(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal `usize` text. |
| `std.fmt.i32(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal signed 32-bit text, including the minimum value. |
| `std.fmt.hexLowerU32(buffer, value)` | `Maybe<Span<u8>>` | Writes lowercase hexadecimal without a prefix. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var number_buf: [12]u8 = [0_u8; 12]
    var hex_buf: [8]u8 = [0_u8; 8]
    let number: Maybe<Span<u8>> = std.fmt.i32(number_buf, -42)
    let hex: Maybe<Span<u8>> = std.fmt.hexLowerU32(hex_buf, 48879_u32)
    if number.has && hex.has && std.mem.eql(number.value, "-42") && std.mem.eql(hex.value, "beef") {
        check world.out.write("fmt ok\n")
    }
}
```

Effects: writes to caller-provided mutable storage.

Allocation behavior: no allocation.

Error behavior: returns `null` when the buffer is too small.

Target support: current compiler targets.
