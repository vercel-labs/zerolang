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
| `std.parse.isAsciiDigit(value)` | `Bool` | Checks whether the first byte is `0` through `9`. |
| `std.parse.isAsciiAlpha(value)` | `Bool` | Checks whether the first byte is ASCII alphabetic. |
| `std.parse.isIdentifierStart(value)` | `Bool` | Checks whether the first byte can start a Zero identifier. |
| `std.parse.isWhitespace(value)` | `Bool` | Checks ASCII whitespace. |
| `std.parse.scanDigits(value)` | `usize` | Counts a leading run of ASCII digits. |
| `std.parse.scanIdentifier(value)` | `usize` | Counts a leading identifier token. |
| `std.parse.scanWhitespace(value)` | `usize` | Counts leading spaces, tabs, line feeds, and carriage returns. |
| `std.parse.scanUntilByte(value, byte)` | `usize` | Returns the first matching byte index or the input length. |
| `std.parse.tokenAscii(value)` | `Span<u8>` | Borrows the first non-whitespace ASCII token. |
| `std.parse.parseBool(value)` | `Maybe<Bool>` | Parses `true` or `false`. |
| `std.parse.parseI32(value)` | `Maybe<i32>` | Parses a full signed 32-bit decimal value. |
| `std.parse.parseU8(value)` | `Maybe<u8>` | Parses a full decimal byte value. |
| `std.parse.parseU16(value)` | `Maybe<u16>` | Parses a full decimal unsigned 16-bit value. |
| `std.parse.parseU32(value)` | `Maybe<u32>` | Parses a full decimal unsigned 32-bit value. |
| `std.parse.parseUsize(value)` | `Maybe<usize>` | Parses a full decimal `usize` value. |

Current limits:

- Source position and span types.
- Rich cursor objects beyond the current allocation-free scanner primitives.
- Token and diagnostic data shared by language and data parsers.

## Example

```zero
use std.parse

pub fn main(world: World) -> Void raises {
    let digit: Bool = std.parse.isAsciiDigit("7")
    let ident: Bool = std.parse.isIdentifierStart("_")
    let scanned: usize = std.parse.scanDigits("123abc")
    let parsed: Maybe<u16> = std.parse.parseU16("8080")
    let signed: Maybe<i32> = std.parse.parseI32("-42")
    let token: Span<u8> = std.parse.tokenAscii("  zero text")
    if digit && ident && scanned == 3 && parsed.has && parsed.value == 8080 && signed.has && signed.value == -42 && std.mem.eql(token, "zero") {
        check world.out.write("parse primitives ok\n")
    }
}
```

## Design Notes

The module stays byte-oriented so compiler, config, and codec code can parse
without Unicode scalar handling or heap allocation. Public helpers accept byte
spans, so callers can parse string literals, fixed arrays, and runtime buffers.

Integer parsers return `Maybe<T>` instead of allocating diagnostics. Callers can
layer richer errors on top.
