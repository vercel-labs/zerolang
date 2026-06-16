## When To Use std.json

In Zerolang, use `std.json` for validation, shallow field lookup, object and
array cursor access, explicit-allocator parsing, and caller-buffer JSON writing.

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
| `std.json.objectFieldCount(bytes)` | `Maybe<usize>` | Counts fields in a JSON object slice. |
| `std.json.objectKey(buffer, bytes, ordinal)` | `Maybe<Span<u8>>` | Decodes an ordinal object key into caller storage. |
| `std.json.objectValue(bytes, ordinal)` | `Maybe<Span<u8>>` | Returns an ordinal object value as a raw JSON slice. |
| `std.json.arrayCount(bytes)` | `Maybe<usize>` | Counts items in a JSON array slice. |
| `std.json.arrayValue(bytes, ordinal)` | `Maybe<Span<u8>>` | Returns an ordinal array value as a raw JSON slice. |
| `std.json.path(bytes, path)` | `Maybe<Span<u8>>` | Returns a raw value for a dot-separated object path. |
| `std.json.pathString(buffer, bytes, path)` | `Maybe<Span<u8>>` | Looks up and decodes a string at a dot-separated object path. |
| `std.json.pathU32(bytes, path)` | `Maybe<u32>` | Looks up and decodes a `u32` at a dot-separated object path. |
| `std.json.pathBool(bytes, path)` | `Maybe<Bool>` | Looks up and decodes a bool at a dot-separated object path. |
| `std.json.stringDecode(buffer, value)` | `Maybe<Span<u8>>` | Decodes a JSON string value, including Unicode escapes as UTF-8, into caller storage. |
| `std.json.string(buffer, bytes, key)` | `Maybe<Span<u8>>` | Looks up and decodes a top-level string field. |
| `std.json.u32(bytes, key)` | `Maybe<u32>` | Looks up and decodes a top-level unsigned integer field. |
| `std.json.bool(bytes, key)` | `Maybe<Bool>` | Looks up and decodes a top-level boolean field. |
| `std.json.writeStringBytes(buffer, text)` | `Maybe<Span<u8>>` | Writes an escaped JSON string from byte input. |
| `std.json.writeObject1String(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a string value. |
| `std.json.writeObject1U32(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a `u32` value. |
| `std.json.writeObject1Bool(buffer, key, value)` | `Maybe<Span<u8>>` | Writes a one-field object with a bool value. |
| `std.json.writeFieldRaw(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one object field from a key and validated raw JSON value. |
| `std.json.writeFieldString(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one object field with an escaped string value. |
| `std.json.writeFieldU32(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one object field with a `u32` value. |
| `std.json.writeFieldBool(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one object field with a bool value. |
| `std.json.writeObject2Fields(buffer, field0, field1)` | `Maybe<Span<u8>>` | Writes a two-field object from field fragments and validates the final object. |
| `std.json.writeObject2StringField(buffer, key, value, field1)` | `Maybe<Span<u8>>` | Writes a two-field object from a string field and a prebuilt field fragment. |
| `std.json.writeObject2U32Field(buffer, key, value, field1)` | `Maybe<Span<u8>>` | Writes a two-field object from a `u32` field and a prebuilt field fragment. |
| `std.json.writeObject2BoolField(buffer, key, value, field1)` | `Maybe<Span<u8>>` | Writes a two-field object from a bool field and a prebuilt field fragment. |
| `std.json.writeArray2Strings(buffer, value0, value1)` | `Maybe<Span<u8>>` | Writes a two-item array with escaped string values. |
| `std.json.writeArray2U32(buffer, value0, value1)` | `Maybe<Span<u8>>` | Writes a two-item array with `u32` values. |
| `std.json.writeArray2Bools(buffer, value0, value1)` | `Maybe<Span<u8>>` | Writes a two-item array with bool values. |

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
    var count_field_buf: [24]u8 = [0_u8; 24]
    let count_field: Maybe<Span<u8>> = std.json.writeFieldU32(count_field_buf, "count", 42_u32)
    var out: [48]u8 = [0_u8; 48]
    var written: Maybe<Span<u8>> = null
    if count_field.has {
        written = std.json.writeObject2StringField(out, "name", "zero", count_field.value)
    }
    if name.has && count.has && written.has && std.json.validateError(written.value) == std.json.errorNone() {
        check world.out.write("json lookup ok\n")
    }
}
```

Object and array cursors return borrowed raw JSON slices. Object keys are
decoded into caller-owned storage:

```zero
pub fn main(world: World) -> Void raises {
    let input: Span<u8> = "{\"user\":{\"name\":\"zero\",\"count\":42},\"items\":[1,2]}"
    let field_count: Maybe<usize> = std.json.objectFieldCount(input)
    var key_buf: [8]u8 = [0_u8; 8]
    let first_key: Maybe<Span<u8>> = std.json.objectKey(key_buf, input, 0)
    let items: Maybe<Span<u8>> = std.json.field(input, "items")
    var name_buf: [8]u8 = [0_u8; 8]
    let name: Maybe<Span<u8>> = std.json.pathString(name_buf, input, "user.name")
    if field_count.has && first_key.has && items.has && std.json.arrayCount(items.value).has && name.has {
        check world.out.write("json cursors ok\n")
    }
}
```

Small array responses use the same caller-buffer pattern:

```zero
pub fn main(world: World) -> Void raises {
    var out: [32]u8 = [0_u8; 32]
    let tags: Maybe<Span<u8>> = std.json.writeArray2Strings(out, "api", "agent")
    if tags.has {
        check world.out.write(tags.value)
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
top-level object fields and returns raw slices or typed scalar decodes. Object
path lookup follows dot-separated object keys; array indexing remains explicit
through `arrayValue`. When an object contains duplicate keys, name-based lookup
returns the first matching value, while ordinal object cursors preserve the
source order and expose every field.
