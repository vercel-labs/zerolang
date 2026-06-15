## When To Use std.io

In Zerolang, use `std.io` for byte reads and writes over caller-owned storage.
The module provides explicit cursor helpers for spans plus fixed reader and
writer values for sequential stream-style code.

Span helpers:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.copy(dst, src)` | `usize` | Copies as many bytes as fit and returns the count. |
| `std.io.copyN(dst, src, count)` | `Maybe<usize>` | Copies exactly `count` bytes when both spans are large enough. |
| `std.io.read(bytes, offset, dst)` | `Maybe<usize>` | Copies available bytes and returns the next cursor. |
| `std.io.readExact(bytes, offset, dst)` | `Maybe<usize>` | Fills the destination only when enough input bytes remain. |
| `std.io.readAt(bytes, offset, dst)` | `Maybe<usize>` | Reads from an explicit offset without owning state. |
| `std.io.readAll(bytes, dst)` | `Maybe<Span<u8>>` | Copies all input bytes and returns the written prefix. |
| `std.io.readByte(bytes, offset)` | `Maybe<u8>` | Reads one byte at an explicit cursor. |
| `std.io.writeByte(buffer, offset, byte)` | `Maybe<usize>` | Writes one byte and returns the next cursor. |
| `std.io.writeSpan(buffer, offset, bytes)` | `Maybe<usize>` | Writes bytes and returns the next cursor. |
| `std.io.writeAll(buffer, offset, bytes)` | `Maybe<usize>` | Writes all bytes only when the full input fits. |
| `std.io.writeAt(buffer, offset, bytes)` | `Maybe<usize>` | Writes at an explicit offset. |
| `std.io.written(buffer, len)` | `Span<u8>` | Borrows the written prefix, clamped to buffer length. |
| `std.io.remaining(buffer, offset)` | `usize` | Reports remaining byte capacity from an explicit cursor. |

Line and delimiter helpers:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.nextLine(bytes, start)` | `Maybe<Span<u8>>` | Borrows the next line without `\n` or trailing `\r`. |
| `std.io.nextLineStart(bytes, start)` | `usize` | Advances to the next line start or end of input. |
| `std.io.readLine(bytes, start)` | `Maybe<Span<u8>>` | Alias for line-oriented reads using the same line rules. |
| `std.io.readLineStart(bytes, start)` | `usize` | Advances to the next line start using the same line rules. |
| `std.io.readUntilDelimiter(bytes, start, delimiter)` | `Maybe<Span<u8>>` | Borrows bytes up to the delimiter or end of input. |
| `std.io.readUntilDelimiterStart(bytes, start, delimiter)` | `usize` | Advances past the delimiter when found, otherwise to end of input. |
| `std.io.countLines(bytes)` | `usize` | Counts lines using the same next-line rules. |

Fixed stream helpers:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.fixedReader(bytes, cursor)` | `FixedReader` | Builds a sequential reader over borrowed bytes. |
| `std.io.fixedReaderRead(&mut reader, dst)` | `usize` | Reads up to `dst` length and advances the reader cursor. |
| `std.io.fixedReaderReadExact(&mut reader, dst)` | `Bool` | Reads exactly `dst` length or leaves the cursor unchanged. |
| `std.io.fixedReaderReadLine(&mut reader)` | `Maybe<Span<u8>>` | Borrows the next line and advances the cursor. |
| `std.io.fixedReaderReadUntilDelimiter(&mut reader, delimiter)` | `Maybe<Span<u8>>` | Borrows bytes up to a delimiter and advances the cursor. |
| `std.io.fixedReaderReadAll(&mut reader, dst)` | `Maybe<Span<u8>>` | Copies remaining bytes into caller storage. |
| `std.io.fixedReaderReadAt(&reader, offset, dst)` | `Maybe<usize>` | Reads from an offset without moving the cursor. |
| `std.io.fixedReaderReadByte(&mut reader)` | `Maybe<u8>` | Reads one byte and advances the cursor. |
| `std.io.fixedReaderLimit(&reader, count)` | `FixedReader` | Creates a bounded reader view from the current cursor. |
| `std.io.fixedReaderSeek(&mut reader, cursor)` | `Bool` | Moves the cursor when the target is in range. |
| `std.io.fixedReaderCursor(&reader)` | `usize` | Reports the cursor. |
| `std.io.fixedReaderLen(&reader)` | `usize` | Reports input length. |
| `std.io.fixedReaderRemaining(&reader)` | `usize` | Reports unread bytes. |
| `std.io.fixedReaderDone(&reader)` | `Bool` | Reports whether the cursor reached the end. |
| `std.io.fixedWriter(buffer, cursor)` | `FixedWriter` | Builds a sequential writer over caller storage. |
| `std.io.fixedWriterWrite(&mut writer, bytes)` | `Bool` | Writes bytes and advances the writer cursor. |
| `std.io.fixedWriterWriteAll(&mut writer, bytes)` | `Bool` | Writes all bytes only when they fit. |
| `std.io.fixedWriterWriteByte(&mut writer, byte)` | `Bool` | Writes one byte and advances the cursor. |
| `std.io.fixedWriterWriteAt(&mut writer, offset, bytes)` | `Bool` | Writes at an offset without moving the cursor. |
| `std.io.fixedWriterView(&writer)` | `Span<u8>` | Borrows the live writer prefix. |
| `std.io.fixedWriterSeek(&mut writer, cursor)` | `Bool` | Moves the cursor when the target is in range. |
| `std.io.fixedWriterTruncate(&mut writer, len)` | `usize` | Clamps the live prefix length. |
| `std.io.fixedWriterClear(&mut writer)` | `usize` | Resets the cursor to zero. |
| `std.io.fixedWriterCursor(&writer)` | `usize` | Reports the cursor. |
| `std.io.fixedWriterCapacity(&writer)` | `usize` | Reports storage length. |
| `std.io.fixedWriterRemaining(&writer)` | `usize` | Reports unwritten capacity. |

Stream composition helpers:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.copyBuffer(&mut reader, &mut writer, scratch)` | `usize` | Copies until EOF or writer capacity using caller scratch storage. |
| `std.io.copyReaderN(&mut reader, &mut writer, count, scratch)` | `Maybe<usize>` | Copies exactly `count` bytes through scratch storage. |
| `std.io.discard(&mut reader, scratch)` | `usize` | Reads and drops bytes through scratch storage. |
| `std.io.teeRead(&mut reader, &mut writer, dst)` | `Maybe<usize>` | Reads into `dst` and writes the same bytes to the writer. |
| `std.io.multiRead(&mut first, &mut second, dst)` | `usize` | Fills `dst` from the first reader, then the second. |

Error labels:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.errorNone()` | `u32` | No IO error. |
| `std.io.errorEof()` | `u32` | End of input. |
| `std.io.errorShortRead()` | `u32` | Input ended before an exact read completed. |
| `std.io.errorShortWrite()` | `u32` | Output capacity ended before an exact write completed. |
| `std.io.errorCapacity()` | `u32` | Caller storage was too small. |
| `std.io.errorPermission()` | `u32` | Permission was denied. |
| `std.io.errorTimeout()` | `u32` | Operation timed out. |
| `std.io.errorIo()` | `u32` | General IO failure. |
| `std.io.errorName(code)` | `String` | Returns a stable label for a known code. |

Buffered capacity helpers:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.bufferedReader(buffer)` | `BufferedReader` | Builds a reader descriptor over caller storage. |
| `std.io.bufferedWriter(buffer)` | `BufferedWriter` | Builds a writer descriptor over caller storage. |
| `std.io.readerCapacity(&reader)` | `usize` | Reports reader storage capacity. |
| `std.io.writerCapacity(&writer)` | `usize` | Reports writer storage capacity. |

Metadata labels:

- effects: memory
- allocation behavior: uses caller buffer; no hidden heap
- target support: target-neutral
- error behavior: exact cursor reads and writes return `Maybe.none` or `false` on overflow or insufficient input
- ownership notes: borrows or writes caller-owned storage

## Example

```zero
pub fn main(world: World) -> Void raises {
    var scratch: [4]u8 = [0_u8; 4]
    var output: [16]u8 = [0_u8; 16]
    var reader: FixedReader = std.io.fixedReader("one\ntwo", 0)
    var writer: FixedWriter = std.io.fixedWriter(output, 0)
    let line: Maybe<Span<u8>> = std.io.fixedReaderReadLine(&mut reader)
    let copied: usize = std.io.copyBuffer(&mut reader, &mut writer, scratch)
    if line.has && copied == 3 && std.mem.eql(std.io.fixedWriterView(&writer), "two") {
        check world.out.write("io ok\n")
    }
}
```

## Design Notes

`std.io` is a caller-owned buffer surface, not an ambient process I/O layer.
Process stdin/stdout stay behind explicit capabilities such as `World` streams.
