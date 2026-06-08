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
| `std.str.copy(buffer, text)` | `Maybe<Span<u8>>` | Copies `text` into caller storage. |
| `std.str.concat(buffer, left, right)` | `Maybe<Span<u8>>` | Writes `left` followed by `right`. |
| `std.str.repeat(buffer, text, count)` | `Maybe<Span<u8>>` | Repeats `text` into caller storage. |
| `std.str.reverse(buffer, text)` | `Maybe<Span<u8>>` | Writes reversed bytes into non-overlapping caller-provided storage. |
| `std.str.countByte(text, byte)` | `usize` | Counts exact byte matches. |
| `std.str.count(text, needle)` | `usize` | Counts non-overlapping byte substring matches; the empty needle returns `len + 1`. |
| `std.str.indexOf(text, needle)` / `std.str.lastIndexOf(text, needle)` | `usize` | Returns a matching byte index or the input length when absent. |
| `std.str.startsWith(text, prefix)` | `Bool` | Checks a byte prefix. |
| `std.str.endsWith(text, suffix)` | `Bool` | Checks a byte suffix. |
| `std.str.contains(text, needle)` | `Bool` | Checks for a byte substring; the empty needle is present. |
| `std.str.trimAscii(text)` | `Span<u8>` | Borrows `text` without leading or trailing ASCII space bytes. |
| `std.str.trimStartAscii(text)` / `std.str.trimEndAscii(text)` | `Span<u8>` | Borrows one-sided trimmed views. |
| `std.str.toLowerAscii(buffer, text)` / `std.str.toUpperAscii(buffer, text)` | `Maybe<Span<u8>>` | Writes ASCII case-converted bytes into caller storage. |
| `std.str.eqlIgnoreAsciiCase(left, right)` | `Bool` | Compares ASCII case-insensitively. |
| `std.str.wordCountAscii(text)` | `usize` | Counts non-empty runs separated by ASCII space bytes. |

Current scope:

- Helpers operate on byte spans and ASCII delimiter rules for space, tab, line feed, and carriage return.
- `reverse` writes into caller storage and returns `null` when the buffer is too small. The destination buffer must not overlap `text`.
- The module does not implement Unicode case mapping, grapheme segmentation, or locale-aware text rules.

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [6]u8 = [0_u8; 6]
    let reversed: Maybe<Span<u8>> = std.str.reverse(storage, "drawer")
    var lower_storage: [4]u8 = [0_u8; 4]
    let lower: Maybe<Span<u8>> = std.str.toLowerAscii(lower_storage, "ZERO")
    if reversed.has && lower.has {
        if std.mem.eql(reversed.value, "reward") && std.mem.eql(lower.value, "zero") {
            check world.out.write("string helper ok\n")
        }
    }
}
```

## Design Notes

`std.str` is allocation-free. Functions that create new byte sequences use
caller-provided storage, and functions that return spans borrow from an input or
that caller-provided storage.

`reverse` is a copy helper, not an in-place reversal primitive. Pass separate
destination storage when the source text comes from mutable bytes.

String literals can be passed directly to these helpers; fixed arrays and
mutable buffers can be passed as spans when the caller needs non-literal input.

The current helpers are byte-string primitives. They are suitable for protocol
tokens, Rosetta-style ASCII examples, and fixed-buffer tools. Unicode text
algorithms should be added as explicit APIs with documented behavior instead of
being implied by these byte-span helpers.
