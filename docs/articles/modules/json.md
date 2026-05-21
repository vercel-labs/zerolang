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
| `std.json.root(doc)` | `JsonNode` | Returns the root node of a parsed document over the existing arena tree. |
| `std.json.kind(node)` | `JsonKind` | Reports the node kind (object, array, string, number, bool, or null). |
| `std.json.get(node, key)` | `Maybe<JsonNode>` | Looks up an object member by `Span<u8>` key; `null` when absent. |
| `std.json.at(node, i)` | `Maybe<JsonNode>` | Indexes an array element; `null` when out of range. |
| `std.json.len(node)` | `usize` | Element count for arrays or member count for objects. |
| `std.json.string(node, out)` | `Maybe<usize>` | Decodes an unescaped UTF-8 string (including `\uXXXX` and surrogate pairs) into caller storage; returns the byte length written. |
| `std.json.int(node)` | `Maybe<i64>` | Reads an integer number node; `null` when not an integer. |
| `std.json.float(node)` | `Maybe<f64>` | Reads a floating-point number node; `null` when not a number. |
| `std.json.bool(node)` | `Maybe<Bool>` | Reads a boolean node; `null` when not a bool. |

Metadata labels:

- effects: parse or alloc
- allocation behavior: validation and streaming are allocation-free; parse uses explicit allocator only; writeString writes caller buffer
- target support: target-neutral
- error behavior: `Maybe` helpers return null on failure
- ownership notes: parsed documents are owned by explicit allocator storage in this compiler slice
- examples: `examples/std-data-formats.0`, `examples/std-json-bytes.0`

## Example

```zero
pub fun main(world: World) -> Void raises {
    let mut arena_buf: [16]u8 = [0_u8; 16]
    let mut arena = std.mem.fixedBufAlloc(arena_buf)
    let parsed = std.json.parse(arena, "{\"ok\":true}")
    let mut out: [16]u8 = [0_u8; 16]
    let text = std.json.writeString(out, "zero")
    if parsed.has && text.has && std.json.streamTokens("{\"ok\":true}") == 3 {
        check world.out.write("json ok\n")
    }
}
```

Byte-span parse shape:

```zero
pub fun main(world: World) -> Void raises {
    let bytes = std.mem.span("{\"ok\":1}")
    let mut arena_buf: [16]u8 = [0_u8; 16]
    let mut arena = std.mem.fixedBufAlloc(arena_buf)
    let parsed = std.json.parseBytes(arena, bytes)
    if parsed.has && std.json.validateBytes(bytes) && std.json.streamTokensBytes(bytes) == 3 {
        check world.out.write("json bytes ok\n")
        return
    }
    check world.err.write("json bytes failed\n")
}
```

Typed navigation over a parsed document:

```zero
pub fun main(world: World) -> Void raises {
    let mut arena_buf: [256]u8 = [0_u8; 256]
    let mut arena = std.mem.fixedBufAlloc(arena_buf)
    let parsed = std.json.parse(arena, "{\"choices\":[{\"message\":{\"content\":\"hi\\n\"}}]}")
    if parsed.has {
        let root = std.json.root(parsed.value)
        let choices = std.json.get(root, std.mem.span("choices"))
        if choices.has {
            let first = std.json.at(choices.value, 0)
            if first.has {
                let message = std.json.get(first.value, std.mem.span("message"))
                if message.has {
                    let content = std.json.get(message.value, std.mem.span("content"))
                    let mut text: [16]u8 = [0_u8; 16]
                    if content.has && std.json.string(content.value, text).has {
                        check world.out.write("json nav ok\n")
                    }
                }
            }
        }
    }
}
```

## Design Notes

JSON should not fake allocation-free semantics. Validation and streaming stay
allocation-free.

Parsing into an owned document requires an explicit allocator. Typed navigation
(`root`/`kind`/`get`/`at`/`len`/`string`/`int`/`float`/`bool`) reads the existing
arena-backed tree in place rather than re-parsing; `string` unescapes `\uXXXX`
escapes and surrogate pairs into caller storage. String and byte-span entry
points share the same JSON subset.

Navigation accessor signatures use aggregate values (`JsonNode`, `Maybe<...>`,
`Span<u8>`, `MutSpan<u8>`) whose ABI exceeds the current direct-backend
register-width limit, so native executable lowering is deferred to the separate
aggregate-ABI work. The accessor surface is fully type-checked today; the
`std-json-nav.0` conformance fixture runs at host check level alongside
`std-platform-basics.0`.
