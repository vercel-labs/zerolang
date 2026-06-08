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
| `std.ascii.isDigit(byte)` | `Bool` | Checks `0` through `9`. |
| `std.ascii.isAlpha(byte)` | `Bool` | Checks `A` through `Z` or `a` through `z`. |
| `std.ascii.isAlnum(byte)` | `Bool` | Checks ASCII alphabetic or digit bytes. |
| `std.ascii.isWhitespace(byte)` | `Bool` | Checks space, tab, line feed, and carriage return. |
| `std.ascii.isLower(byte)` / `std.ascii.isUpper(byte)` | `Bool` | Checks ASCII case ranges. |
| `std.ascii.isHexDigit(byte)` | `Bool` | Checks decimal digits and `a-f` / `A-F`. |
| `std.ascii.toLower(byte)` / `std.ascii.toUpper(byte)` | `u8` | Converts ASCII letters and leaves other bytes unchanged. |
| `std.ascii.digitValue(byte)` | `Maybe<u8>` | Converts an ASCII decimal digit to `0..9`. |
| `std.ascii.hexValue(byte)` | `Maybe<u8>` | Converts an ASCII hexadecimal digit to `0..15`. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    let digit: Maybe<u8> = std.ascii.digitValue(55_u8)
    let hex: Maybe<u8> = std.ascii.hexValue(70_u8)
    if std.ascii.isAlpha(65_u8) && std.ascii.toLower(90_u8) == 122_u8 && digit.has && digit.value == 7_u8 && hex.has && hex.value == 15_u8 {
        check world.out.write("ascii ok\n")
    }
}
```

Effects: none.

Allocation behavior: no allocation.

Error behavior: value helpers return `null` when the byte is outside the accepted range.

Target support: current compiler targets.
