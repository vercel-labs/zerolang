## Graph Surface

This module is graph-backed. The compiler uses its standard-library graph store,
while the Zero snippets below show the human-readable projection that agents may
export for review. Agents should discover helpers with `zero skills get stdlib`,
inspect user packages with `zero query [graph-input]` or
`zero inspect [graph-input]`, and patch user code through the graph instead of
hand-editing `.0` files.

Runnable today:

`std.text` is for byte-backed text validation and counting. It does not imply
locale-aware case mapping, grapheme segmentation, normalization, or display-width
rules.

| API | Return | Notes |
| --- | --- | --- |
| `std.text.isAscii(text)` | `Bool` | Checks that every byte is below `0x80`. |
| `std.text.utf8Valid(text)` | `Bool` | Validates UTF-8 byte structure, rejecting overlong encodings, surrogate code points, and values above `U+10FFFF`. |
| `std.text.utf8Len(text)` | `Maybe<usize>` | Counts Unicode scalar values when UTF-8 is valid; returns `null` on invalid input. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    let valid: [2]u8 = [195_u8, 169_u8]
    let invalid: [1]u8 = [128_u8]
    let len: Maybe<usize> = std.text.utf8Len(valid)
    if !std.text.isAscii(valid) && std.text.utf8Valid(valid) && !std.text.utf8Valid(invalid) && len.has && len.value == 1 {
        check world.out.write("text ok\n")
    }
}
```

Effects: none.

Allocation behavior: no allocation.

Error behavior: `utf8Len` returns `null` for invalid UTF-8.

Target support: current compiler targets.
