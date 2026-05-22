## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.io.bufferedReader(buffer)` | `BufferedReader` | Builds a reader over caller-owned fixed storage. |
| `std.io.bufferedWriter(buffer)` | `BufferedWriter` | Builds a writer over caller-owned fixed storage. |
| `std.io.readerCapacity(&reader)` | `usize` | Reports reader storage capacity. |
| `std.io.writerCapacity(&writer)` | `usize` | Reports writer storage capacity. |
| `std.io.copy(dst, src)` | `usize` | Copies bytes into caller-owned mutable storage. |

Metadata labels:

- effects: memory
- allocation behavior: uses caller buffer; no hidden heap
- target support: target-neutral
- error behavior: infallible for capacity helpers; copy returns the copied byte count
- ownership notes: borrows or writes caller-owned storage
- example: `examples/std-path-io.0`

## Example

```zero
pub fn main Void world World !
  mut copy_dst [4]u8 [0, 0, 0, 0]
  mut reader_buf [8]u8 [0, 0, 0, 0, 0, 0, 0, 0]
  let reader std.io.bufferedReader reader_buf
  let copied std.io.copy copy_dst (std.mem.span "abcd")
  if && (== (std.io.readerCapacity (&reader)) 8) (== copied 4)
    check world.out.write "io ok\n"
```

## Design Notes

`std.io` is a caller-owned buffer surface, not an ambient process I/O layer.

Process stdin/stdout stays behind explicit capabilities such as `World` and
`Io`.
