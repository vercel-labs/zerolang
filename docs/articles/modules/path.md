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
| `std.path.basename(path)` | `String` | Borrows the final lexical component of `path`. |
| `std.path.dirname(path)` | `String` | Borrows or returns the lexical parent portion of `path`. |
| `std.path.extension(path)` | `String` | Borrows the suffix after the last `.` in the final component. |
| `std.path.join(buffer, left, right)` | `Maybe<String>` | Joins two path fragments into caller-provided fixed buffer storage. |
| `std.path.normalize(buffer, path)` | `Maybe<String>` | Collapses repeated `/`, `.`, and lexical `..` segments into caller-provided storage. |
| `std.path.relative(buffer, base, target)` | `Maybe<String>` | Produces a target-relative lexical path when possible, or copies `target`. |

Current scope:

- Helpers are target-neutral lexical operations over `/`-separated paths.
- Buffer-writing helpers return `null` when caller storage is too small.
- The module does not implement platform-specific path rules, drive prefixes, or filesystem access.

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [64]u8 = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    let path: Maybe<String> = std.path.join(storage, ".zero", "example.txt")
    if path.has {
        check world.out.write(path.value)
        check world.out.write("\n")
    }
}
```

## Design Notes

`std.path.join`, `std.path.normalize`, and `std.path.relative` write into caller
storage and return `null` when the buffer is too small. They do not allocate.

The current behavior uses `/` as the portable package/example separator. These
helpers are lexical string helpers, not target-specific filesystem resolvers.
