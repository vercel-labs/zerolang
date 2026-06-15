## When To Use std.fmt

In Zerolang, use `std.fmt` when a program needs to format booleans or integers into a
caller-owned buffer instead of allocating text.

Runnable today:

Formatting helpers write into caller-provided `MutSpan<u8>` storage and return a
borrowed prefix on success.

| API | Return | Notes |
| --- | --- | --- |
| `std.fmt.bool(buffer, value)` | `Maybe<Span<u8>>` | Writes `true` or `false`. |
| `std.fmt.u32(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal unsigned 32-bit text. |
| `std.fmt.u32Base(buffer, value, base)` | `Maybe<Span<u8>>` | Writes unsigned 32-bit text in base 2 through 36. |
| `std.fmt.u64(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal unsigned 64-bit text. |
| `std.fmt.u64Base(buffer, value, base)` | `Maybe<Span<u8>>` | Writes unsigned 64-bit text in base 2 through 36. |
| `std.fmt.usize(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal `usize` text. |
| `std.fmt.usizeBase(buffer, value, base)` | `Maybe<Span<u8>>` | Writes `usize` text in base 2 through 36. |
| `std.fmt.i32(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal signed 32-bit text, including the minimum value. |
| `std.fmt.i32Base(buffer, value, base)` | `Maybe<Span<u8>>` | Writes signed 32-bit text in base 2 through 36. |
| `std.fmt.i32Sign(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal signed 32-bit text with an explicit `+` for non-negative values. |
| `std.fmt.i64(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal signed 64-bit text, including the minimum value. |
| `std.fmt.i64Base(buffer, value, base)` | `Maybe<Span<u8>>` | Writes signed 64-bit text in base 2 through 36. |
| `std.fmt.i64Sign(buffer, value)` | `Maybe<Span<u8>>` | Writes decimal signed 64-bit text with an explicit `+` for non-negative values. |
| `std.fmt.hexLowerU32(buffer, value)` | `Maybe<Span<u8>>` | Writes lowercase hexadecimal without a prefix. |
| `std.fmt.padLeft(buffer, text, width, pad)` | `Maybe<Span<u8>>` | Left-pads `text` with `pad` until `width`, or copies `text` when already wide enough. |
| `std.fmt.padRight(buffer, text, width, pad)` | `Maybe<Span<u8>>` | Right-pads `text` with `pad` until `width`, or copies `text` when already wide enough. |
| `std.fmt.writeSpan(writer, text)` | `Bool` | Writes bytes into a `FixedWriter`. |
| `std.fmt.writeBool(writer, value)` | `Bool` | Formats a boolean into a `FixedWriter`. |
| `std.fmt.writeU32(writer, value)` / `std.fmt.writeU64(writer, value)` / `std.fmt.writeUsize(writer, value)` | `Bool` | Formats unsigned decimal text into a `FixedWriter`. |
| `std.fmt.writeI32(writer, value)` / `std.fmt.writeI64(writer, value)` | `Bool` | Formats signed decimal text into a `FixedWriter`. |
| `std.fmt.writeI32Sign(writer, value)` / `std.fmt.writeI64Sign(writer, value)` | `Bool` | Formats signed decimal text with an explicit `+` for non-negative values into a `FixedWriter`. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var number_buf: [12]u8 = [0_u8; 12]
    var big_buf: [20]u8 = [0_u8; 20]
    var hex_buf: [8]u8 = [0_u8; 8]
    var padded_buf: [6]u8 = [0_u8; 6]
    let number: Maybe<Span<u8>> = std.fmt.i32(number_buf, -42)
    let big: Maybe<Span<u8>> = std.fmt.u64(big_buf, 18446744073709551615_u64)
    let hex: Maybe<Span<u8>> = std.fmt.hexLowerU32(hex_buf, 48879_u32)
    let padded: Maybe<Span<u8>> = std.fmt.padLeft(padded_buf, "42", 5, 48_u8)
    var writer_storage: [24]u8 = [0_u8; 24]
    var writer: FixedWriter = std.io.fixedWriter(writer_storage, 0)
    let wrote: Bool = std.fmt.writeSpan(&mut writer, "n=") && std.fmt.writeI32(&mut writer, -42)
    if number.has && big.has && hex.has && padded.has && wrote && std.mem.eql(number.value, "-42") && std.mem.eql(big.value, "18446744073709551615") && std.mem.eql(hex.value, "beef") && std.mem.eql(padded.value, "00042") && std.mem.eql(std.io.fixedWriterView(&writer), "n=-42") {
        check world.out.write("fmt ok\n")
    }
}
```

Effects: writes to caller-provided mutable storage or an explicit fixed writer.

Allocation behavior: no allocation.

Error behavior: returns `null` when the buffer is too small.

Target support: current compiler targets.
