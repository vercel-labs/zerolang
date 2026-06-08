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
| `std.io.bufferedReader(buffer)` | `BufferedReader` | Builds a reader over caller-owned fixed storage. |
| `std.io.bufferedWriter(buffer)` | `BufferedWriter` | Builds a writer over caller-owned fixed storage. |
| `std.io.readerCapacity(&reader)` | `usize` | Reports reader storage capacity. |
| `std.io.writerCapacity(&writer)` | `usize` | Reports writer storage capacity. |
| `std.io.copy(dst, src)` | `usize` | Copies bytes into caller-owned mutable storage. |
| `std.io.writeByte(buffer, offset, byte)` | `Maybe<usize>` | Writes one byte at an explicit cursor and returns the next cursor. |
| `std.io.writeSpan(buffer, offset, bytes)` | `Maybe<usize>` | Writes bytes at an explicit cursor and returns the next cursor. |
| `std.io.written(buffer, len)` | `Span<u8>` | Borrows the written prefix, clamping to buffer length. |
| `std.io.remaining(buffer, offset)` | `usize` | Reports remaining byte capacity from an explicit cursor. |
| `std.io.nextLine(bytes, start)` | `Maybe<Span<u8>>` | Borrows the next line without `\n` or trailing `\r`. |
| `std.io.nextLineStart(bytes, start)` | `usize` | Advances to the next line start or the end of input. |
| `std.io.countLines(bytes)` | `usize` | Counts lines using the same next-line rules. |

Metadata labels:

- effects: memory
- allocation behavior: uses caller buffer; no hidden heap
- target support: target-neutral
- error behavior: cursor writes return `Maybe.none` on overflow; copy returns the copied byte count
- ownership notes: borrows or writes caller-owned storage
- example: `examples/std-path-io.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var copy_dst: [4]u8 = [0, 0, 0, 0]
    var reader_buf: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    let reader: BufferedReader = std.io.bufferedReader(reader_buf)
    let copied: usize = std.io.copy(copy_dst, std.mem.span("abcd"))
    let line: Maybe<Span<u8>> = std.io.nextLine("one\ntwo", 0)
    if std.io.readerCapacity(&reader) == 8 && copied == 4 && line.has {
        check world.out.write("io ok\n")
    }
}
```

## Design Notes

`std.io` is a caller-owned buffer surface, not an ambient process I/O layer.

Process stdin/stdout stays behind explicit capabilities such as `World` and
`Io`.
