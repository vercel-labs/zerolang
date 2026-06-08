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
| `std.json.validate(text)` | `Bool` | Checks the current JSON subset without allocation. |
| `std.json.validateBytes(bytes)` | `Bool` | Checks a `Span<u8>` JSON payload without allocation. |
| `std.json.parse(alloc, text)` | `Maybe<JsonDoc>` | Parses with an explicit allocator and returns `null` on failure. |
| `std.json.parseBytes(alloc, bytes)` | `Maybe<JsonDoc>` | Parses a `Span<u8>` payload with an explicit allocator and returns `null` on failure. |
| `std.json.streamTokens(text)` | `usize` | Counts stream tokens without building an owned tree. |
| `std.json.streamTokensBytes(bytes)` | `usize` | Counts stream tokens from a `Span<u8>` payload. |
| `std.json.writeString(buffer, text)` | `Maybe<String>` | Writes an escaped JSON string into caller storage. |
| `std.json.decodeBoundary()` | `String` | Documents the typed decode boundary exposed by current metadata. |
| `std.json.errorNone()` | `u32` | Validation status for a clean payload. |
| `std.json.errorInvalid()` | `u32` | Validation status for malformed JSON. |
| `std.json.errorTrailing()` | `u32` | Validation status for trailing non-whitespace bytes. |
| `std.json.validateError(bytes)` | `u32` | Validates a byte span and returns a structured status code. |
| `std.json.field(bytes, key)` | `Maybe<Span<u8>>` | Returns the raw top-level object field value. |
| `std.json.stringDecode(buffer, value)` | `Maybe<Span<u8>>` | Decodes a JSON string value, including Unicode escapes as UTF-8, into caller storage. |
| `std.json.string(buffer, bytes, key)` | `Maybe<Span<u8>>` | Looks up and decodes a top-level string field. |
| `std.json.u32(bytes, key)` | `Maybe<u32>` | Looks up and decodes a top-level unsigned integer field. |
| `std.json.bool(bytes, key)` | `Maybe<Bool>` | Looks up and decodes a top-level boolean field. |
| `std.json.writeStringBytes(buffer, text)` | `Maybe<Span<u8>>` | Writes an escaped JSON string from byte input. |
| `std.json.writeObject1String(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a string value. |
| `std.json.writeObject1U32(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a `u32` value. |
| `std.json.writeObject1Bool(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a bool value. |

Metadata labels:

- effects: parse or alloc
- allocation behavior: validation and streaming are allocation-free; parse uses explicit allocator only; writeString writes caller buffer
- target support: target-neutral
- error behavior: `Maybe` helpers return null on failure
- ownership notes: parsed documents are owned by explicit allocator storage in this compiler slice
- examples: `examples/std-data-formats.graph`, `examples/std-json-bytes.graph`, `conformance/native/pass/std-codec-json-url.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var arena_buf: [16]u8 = [0_u8; 16]
    var arena: FixedBufAlloc = std.mem.fixedBufAlloc(arena_buf)
    let parsed: Maybe<JsonDoc> = std.json.parse(arena, "{\"ok\":true}")
    var out: [16]u8 = [0_u8; 16]
    let text: Maybe<String> = std.json.writeString(out, "zero")
    if parsed.has && text.has && std.json.streamTokens("{\"ok\":true}") == 3 {
        check world.out.write("json ok\n")
    }
}
```

Byte-span parse form:

```zero
pub fn main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("{\"ok\":1}")
    var arena_buf: [16]u8 = [0_u8; 16]
    var arena: FixedBufAlloc = std.mem.fixedBufAlloc(arena_buf)
    let parsed: Maybe<JsonDoc> = std.json.parseBytes(arena, bytes)
    if parsed.has && std.json.validateBytes(bytes) && std.json.streamTokensBytes(bytes) == 3 {
        check world.out.write("json bytes ok\n")
        return
    }
    check world.err.write("json bytes failed\n")
}
```

Top-level object lookup and caller-buffer writing:

```zero
pub fn main(world: World) -> Void raises {
    let input: Span<u8> = "{\"name\":\"zero\",\"count\":42,\"ok\":true}"
    var name_buf: [8]u8 = [0_u8; 8]
    let name: Maybe<Span<u8>> = std.json.string(name_buf, input, "name")
    let count: Maybe<u32> = std.json.u32(input, "count")
    var out: [24]u8 = [0_u8; 24]
    let written: Maybe<Span<u8>> = std.json.writeObject1U32(out, "count", 42_u32)
    if name.has && count.has && written.has && std.json.validateError(written.value) == std.json.errorNone() {
        check world.out.write("json lookup ok\n")
    }
}
```

## Design Notes

JSON should not fake allocation-free semantics. Validation, field lookup,
string decode, and writing stay allocation-free. String decode writes UTF-8 for
Unicode escapes and rejects malformed surrogate pairs.

Parsing into an owned document requires an explicit allocator. The current
`JsonDoc` value is opaque; examples inspect `Maybe.has` and use token streaming
for allocation-free summaries. Field lookup is intentionally shallow: it reads
top-level object fields and returns raw slices or typed scalar decodes.
