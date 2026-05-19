## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.parse.isAsciiDigit(value)` | `Bool` | Checks whether the first byte is `0` through `9`. |
| `std.parse.isAsciiAlpha(value)` | `Bool` | Checks whether the first byte is ASCII alphabetic. |
| `std.parse.isIdentifierStart(value)` | `Bool` | Checks whether the first byte can start a Zero identifier. |
| `std.parse.isWhitespace(value)` | `Bool` | Checks ASCII whitespace. |
| `std.parse.scanDigits(value)` | `usize` | Counts a leading run of ASCII digits. |
| `std.parse.scanIdentifier(value)` | `usize` | Counts a leading identifier token. |
| `std.parse.parseU8(value)` | `Maybe<u8>` | Parses a full decimal byte value. |
| `std.parse.parseU16(value)` | `Maybe<u16>` | Parses a full decimal unsigned 16-bit value. |
| `std.parse.parseU32(value)` | `Maybe<u32>` | Parses a full decimal unsigned 32-bit value. |

Current limits:

- Source position and span types.
- Rich cursor objects beyond the current allocation-free scanner primitives.
- Token and diagnostic packets shared by language and data parsers.

## Example

```zero
use std.parse

pub fun main(world: World) -> Void raises {
    let digit = std.parse.isAsciiDigit("7")
    let ident = std.parse.isIdentifierStart("_")
    let scanned = std.parse.scanDigits("123abc")
    let parsed = std.parse.parseU16("8080")
    if digit && ident && scanned == 3 && parsed.has && parsed.value == 8080 {
        check world.out.write("parse primitives ok\n")
    }
}
```

## Design Notes

The module stays byte-oriented so compiler, config, and codec code can parse
without Unicode scalar handling or heap allocation.

Integer parsers return `Maybe<T>` instead of allocating diagnostics. Callers can
layer richer errors on top.
