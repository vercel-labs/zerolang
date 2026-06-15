## When To Use std.unicode

In Zerolang, use `std.unicode` for UTF-8 codepoint decode/encode iteration and
codepoint-class checks. For whole-span validation and codepoint counting, use
the existing `std.text.utf8Valid` and `std.text.utf8Len` helpers; `std.unicode`
extends them with per-codepoint access.

Decoding is strict UTF-8: overlong encodings, surrogate codepoints, values
above `U+10FFFF`, and truncated sequences all return `null`.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.unicode.decodeAt(text, index)` | `Maybe<u32>` | Decodes the codepoint starting at a byte index; `null` for invalid or out-of-range positions. |
| `std.unicode.widthAt(text, index)` | `Maybe<usize>` | Byte width of the sequence at a byte index; advance `index` by this to iterate codepoints. |
| `std.unicode.nextIndex(text, index)` | `Maybe<usize>` | Next byte index after the codepoint at `index`; `null` for invalid input. |
| `std.unicode.invalidIndex(text)` | `usize` | First invalid UTF-8 byte index, or the input length when valid. |
| `std.unicode.decodeStatusAt(text, index)` | `u32` | Strict UTF-8 status code at a byte index. |
| `std.unicode.statusName(status)` | `String` | Names a status code such as `truncated sequence`. |
| `std.unicode.encode(buffer, cp)` | `Maybe<Span<u8>>` | Encodes a codepoint as UTF-8 into a caller buffer; `null` for surrogates, values above `U+10FFFF`, or a too-small buffer. |
| `std.unicode.encodedWidth(cp)` | `Maybe<usize>` | UTF-8 byte width a codepoint needs (1-4); `null` for invalid codepoints. |
| `std.unicode.isDigit(cp)` | `Bool` | ASCII digit class, matching regex `\d` semantics by codepoint. |
| `std.unicode.isWord(cp)` | `Bool` | ASCII word class `[A-Za-z0-9_]`, matching regex `\w` semantics. |
| `std.unicode.isSpace(cp)` | `Bool` | ECMA-262 whitespace plus line terminators, matching regex `\s` semantics. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    let text: Span<u8> = std.mem.span("aé💯")
    var index: usize = 0
    var count: usize = 0
    while index < std.mem.len(text) {
        let next: Maybe<usize> = std.unicode.nextIndex(text, index)
        if !next.has {
            return
        }
        index = next.value
        count = count + 1
    }
    var storage: [4]u8 = [0; 4]
    let buffer: MutSpan<u8> = storage
    let encoded: Maybe<Span<u8>> = std.unicode.encode(buffer, 233)
    if count == 3 && std.unicode.invalidIndex(text) == std.mem.len(text) && (encoded.has && std.mem.len(encoded.value) == 2) {
        check world.out.write("unicode ok\n")
    }
}
```

Effects: none.

Allocation behavior: `encode` writes the caller buffer; all other helpers
allocate nothing.

Error behavior: decode/encode and cursor helpers return `null` for invalid
input. `decodeStatusAt` and `statusName` provide allocation-free status details;
class helpers are infallible.

Target support: current compiler targets.
