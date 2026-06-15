## When To Use std.toml

In Zerolang, use `std.toml` for TOML validation, shallow field lookup, and typed scalar
decode helpers.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.toml.validate(text)` | `Bool` | Checks the current TOML subset without allocation. |
| `std.toml.validateBytes(bytes)` | `Bool` | Checks a `Span<u8>` TOML payload without allocation. |
| `std.toml.field(bytes, key)` | `Maybe<Span<u8>>` | Returns the raw value for a direct, dotted, or shallow table field. |
| `std.toml.stringDecode(buffer, value)` | `Maybe<Span<u8>>` | Decodes a TOML string value into caller storage. |
| `std.toml.string(buffer, bytes, key)` | `Maybe<Span<u8>>` | Looks up and decodes a TOML string field. |
| `std.toml.u32(bytes, key)` | `Maybe<u32>` | Looks up and decodes an unsigned integer field. |
| `std.toml.bool(bytes, key)` | `Maybe<Bool>` | Looks up and decodes a boolean field. |

Metadata labels:

- effects: parse
- allocation behavior: allocation-free; decoded strings write into caller storage
- target support: target-neutral
- error behavior: `Maybe` helpers return null on malformed or missing fields
- ownership notes: returned raw fields borrow from the input span; decoded strings borrow from the caller buffer
- examples: `conformance/native/pass/std-toml-basic.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let input: Span<u8> = "[package]\nname = \"demo\"\n\n[features]\ngraph = true\n"
    var name_buffer: [16]u8 = [0_u8; 16]
    let name: Maybe<Span<u8>> = std.toml.string(name_buffer, input, "package.name")
    let graph: Maybe<Bool> = std.toml.bool(input, "features.graph")
    if std.toml.validateBytes(input) && name.has && graph.has && graph.value {
        check world.out.write("toml ok\n")
    }
}
```

## Design Notes

The current TOML helper surface is deliberately narrow. It supports the package
manifest subset agents need most often: tables, dotted keys, strings, booleans,
integers, and string arrays. Field lookup is shallow and table-aware, so
`std.toml.string(buffer, input, "package.name")` can read `name` inside a
`[package]` table.

The helpers avoid hidden allocation. Use `field` when a raw value slice is
enough, and use `string` or `stringDecode` when escape decoding into explicit
caller storage is required.
