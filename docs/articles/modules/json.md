## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.json.validate(text)` | `Bool` | Checks the current JSON subset without allocation. |
| `std.json.validateBytes(bytes)` | `Bool` | Checks a `Span<u8>` JSON payload without allocation. |
| `std.json.parse(alloc, text)` | `Maybe<JsonDoc>` | Parses with an explicit allocator and returns `null` on failure. |
| `std.json.parseBytes(alloc, bytes)` | `Maybe<JsonDoc>` | Parses a `Span<u8>` payload with an explicit allocator and returns `null` on failure. |
| `std.json.streamTokens(text)` | `usize` | Counts stream tokens without building an owned tree. |
| `std.json.streamTokensBytes(bytes)` | `usize` | Counts stream tokens from a `Span<u8>` payload. |
| `std.json.writeString(buffer, text)` | `Maybe<String>` | Writes an escaped JSON string into caller storage. |
| `std.json.decodeBoundary()` | `String` | Documents the typed decode boundary exposed by current metadata. |

Metadata labels:

- effects: parse or alloc
- allocation behavior: validation and streaming are allocation-free; parse uses explicit allocator only; writeString writes caller buffer
- target support: target-neutral
- error behavior: `Maybe` helpers return null on failure
- ownership notes: parsed documents are owned by explicit allocator storage in this compiler slice
- examples: `examples/std-data-formats.0`, `examples/std-json-bytes.0`

## Example

```zero
pub fn main Void world World !
  mut arena_buf [16]u8 [0_u8;16]
  mut arena std.mem.fixedBufAlloc arena_buf
  let parsed std.json.parse arena "{\"ok\":true}"
  mut out [16]u8 [0_u8;16]
  let text std.json.writeString out "zero"
  if && (&& parsed.has text.has) (== (std.json.streamTokens "{\"ok\":true}") 3)
    check world.out.write "json ok\n"
```

Byte-span parse form:

```zero
pub fn main Void world World !
  let bytes std.mem.span "{\"ok\":1}"
  mut arena_buf [16]u8 [0_u8;16]
  mut arena std.mem.fixedBufAlloc arena_buf
  let parsed std.json.parseBytes arena bytes
  if && (&& parsed.has (std.json.validateBytes bytes)) (== (std.json.streamTokensBytes bytes) 3)
    check world.out.write "json bytes ok\n"
    ret
  check world.err.write "json bytes failed\n"
```

## Design Notes

JSON should not fake allocation-free semantics. Validation and streaming stay
allocation-free.

Parsing into an owned document requires an explicit allocator. The current
`JsonDoc` value is opaque; examples inspect `Maybe.has` and use token streaming
for allocation-free summaries. String and byte-span entry points share the same
JSON subset.
