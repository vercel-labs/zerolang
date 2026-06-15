## When To Use std.parse

In Zerolang, use `std.parse` for allocation-free byte scanners and scalar parsers.

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
| `std.parse.parseDuration(value)` | `Maybe<Duration>` | Parses signed `ns`, `us`, `ms`, `s`, `m`, and `h` duration components such as `1h30m`. |
| `std.parse.parseByteSize(value)` | `Maybe<usize>` | Parses bytes plus `KB`, `MB`, `GB`, `KiB`, `MiB`, and `GiB` suffixes. |
| `std.parse.parseI32(value)` | `Maybe<i32>` | Parses a full signed 32-bit decimal value. |
| `std.parse.parseI32Base(value, base)` | `Maybe<i32>` | Parses a full signed 32-bit value in base 2 through 36. |
| `std.parse.parseI32Prefix(value)` | `Maybe<i32>` | Parses signed decimal, `0x`, `0o`, or `0b` input. |
| `std.parse.parseI64(value)` | `Maybe<i64>` | Parses a full signed 64-bit decimal value. |
| `std.parse.parseI64Base(value, base)` | `Maybe<i64>` | Parses a full signed 64-bit value in base 2 through 36. |
| `std.parse.parseI64Prefix(value)` | `Maybe<i64>` | Parses signed decimal, `0x`, `0o`, or `0b` input. |
| `std.parse.parseU8(value)` | `Maybe<u8>` | Parses a full decimal byte value. |
| `std.parse.parseU16(value)` | `Maybe<u16>` | Parses a full decimal unsigned 16-bit value. |
| `std.parse.parseU32(value)` | `Maybe<u32>` | Parses a full decimal unsigned 32-bit value. |
| `std.parse.parseU32Base(value, base)` | `Maybe<u32>` | Parses a full unsigned 32-bit value in base 2 through 36. |
| `std.parse.parseU32Prefix(value)` | `Maybe<u32>` | Parses unsigned decimal, `0x`, `0o`, or `0b` input. |
| `std.parse.parseU64(value)` | `Maybe<u64>` | Parses a full decimal unsigned 64-bit value. |
| `std.parse.parseU64Base(value, base)` | `Maybe<u64>` | Parses a full unsigned 64-bit value in base 2 through 36. |
| `std.parse.parseU64Prefix(value)` | `Maybe<u64>` | Parses unsigned decimal, `0x`, `0o`, or `0b` input. |
| `std.parse.parseUsize(value)` | `Maybe<usize>` | Parses a full decimal `usize` value. |
| `std.parse.parseUsizeBase(value, base)` | `Maybe<usize>` | Parses a full `usize` value in base 2 through 36. |
| `std.parse.parseUsizePrefix(value)` | `Maybe<usize>` | Parses `usize` decimal, `0x`, `0o`, or `0b` input. |

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
    let signed: Maybe<i32> = std.parse.parseI32Prefix("-0x2a")
    let signed64: Maybe<i64> = std.parse.parseI64("-9223372036854775808")
    let hex: Maybe<u32> = std.parse.parseU32Base("ff", 16_u32)
    let hex64: Maybe<u64> = std.parse.parseU64Prefix("0xffffffffffffffff")
    let duration: Maybe<Duration> = std.parse.parseDuration("1h30m")
    let size: Maybe<usize> = std.parse.parseByteSize("2MiB")
    let token: Span<u8> = std.parse.tokenAscii("  zero text")
    if digit && ident && scanned == 3 && parsed.has && parsed.value == 8080 && signed.has && signed.value == -42 && signed64.has && signed64.value == 0 - 9223372036854775807 - 1 && hex.has && hex.value == 255_u32 && hex64.has && hex64.value == 18446744073709551615_u64 && duration.has && std.time.asSecondsFloor(duration.value) == 5400_i64 && size.has && size.value == 2097152 && std.mem.eql(token, "zero") {
        check world.out.write("parse primitives ok\n")
    }
}
```

## Design Notes

The module stays byte-oriented so compiler, config, and codec code can parse
without Unicode scalar handling or heap allocation. Public helpers accept byte
spans, so callers can parse string literals, fixed arrays, and runtime buffers.

Integer parsers return `Maybe<T>` instead of allocating diagnostics. Base parsers
accept bases 2 through 36, consume the full input, and reject overflow. Prefix
parsers recognize decimal by default plus `0x`, `0o`, and `0b` for 32-bit,
64-bit, and `usize` widths. Duration and byte-size parsers also consume the
full input and reject overflow.
