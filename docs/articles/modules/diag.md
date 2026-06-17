## When To Use std.diag

In Zerolang, use `std.diag` to turn byte offsets into source locations and
small diagnostic snippets without allocating.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.diag.line(bytes, offset)` | `usize` | Returns the 1-based line for a byte offset, clamping offsets past the end. |
| `std.diag.column(bytes, offset)` | `usize` | Returns the 1-based byte column for a byte offset. |
| `std.diag.lineStart(bytes, offset)` | `usize` | Returns the byte index where the containing line starts. |
| `std.diag.lineEnd(bytes, offset)` | `usize` | Returns the byte index where the containing line ends, trimming a trailing CR before LF. |
| `std.diag.lineText(bytes, offset)` | `Span<u8>` | Borrows the containing line without its newline. |
| `std.diag.rangeLen(bytes, start, end)` | `usize` | Returns the clamped byte length for a half-open range. |
| `std.diag.rangeText(bytes, start, end)` | `Span<u8>` | Borrows the clamped half-open byte range. |
| `std.diag.formatLocation(buffer, path, line, column)` | `Maybe<Span<u8>>` | Writes `path:line:column` into caller storage. |
| `std.diag.formatOffsetLocation(buffer, path, bytes, offset)` | `Maybe<Span<u8>>` | Computes line and column from an offset, then writes `path:line:column`. |

Metadata labels:

- effects: parse for source offset scanning; memory for caller-buffer formatting
- allocation behavior: no allocation, except formatting writes into caller storage
- target support: target-neutral
- error behavior: formatting returns `null` when the buffer is too small
- ownership notes: text helpers return borrowed views into the input bytes
- example: `conformance/native/pass/std-diag.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let source: Span<u8> = "one\ntwo\nthree"
    var storage: [32]u8 = [0_u8; 32]
    let location: Maybe<Span<u8>> = std.diag.formatOffsetLocation(storage, "input.0", source, 5)
    if location.has && std.mem.eql(location.value, "input.0:2:2") && std.mem.eql(std.diag.lineText(source, 5), "two") {
        check world.out.write("diag ok\n")
    }
}
```

## Design Notes

Offsets are byte offsets, not Unicode scalar indexes or terminal display
columns. That keeps parser diagnostics deterministic and cheap across targets.
Line and column numbers are 1-based for user-facing output. Range helpers use
half-open byte ranges and clamp both ends to the input length.
