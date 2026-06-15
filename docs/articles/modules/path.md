## When To Use std.path

In Zerolang, use `std.path` for lexical path operations that borrow from input
paths or write into caller-owned buffers.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.path.basename(path)` | `String` | Borrows the final lexical component of `path`. |
| `std.path.dirname(path)` | `String` | Borrows or returns the lexical parent portion of `path`. |
| `std.path.extension(path)` | `String` | Borrows the suffix after the last `.` in the final component. |
| `std.path.stem(path)` | `String` | Borrows the final component without its extension. |
| `std.path.splitDir(path)` | `String` | Borrows the directory side of the final split. |
| `std.path.splitBase(path)` | `String` | Borrows the basename side of the final split. |
| `std.path.isAbs(path)` | `Bool` | Returns true for paths that begin with a path separator. |
| `std.path.componentCount(path)` | `usize` | Counts non-empty lexical path components. |
| `std.path.component(path, index)` | `Maybe<String>` | Borrows one non-empty lexical component by index. |
| `std.path.abs(buffer, base, target)` | `Maybe<String>` | Copies `target` when already absolute, or joins `base` and `target` into caller storage. |
| `std.path.join(buffer, left, right)` | `Maybe<String>` | Joins two path fragments into caller-provided fixed buffer storage. |
| `std.path.normalize(buffer, path)` | `Maybe<String>` | Collapses repeated `/`, `.`, and lexical `..` segments into caller-provided storage. |
| `std.path.relative(buffer, base, target)` | `Maybe<String>` | Produces a target-relative lexical path when possible, or copies `target`. |

Current scope:

- Helpers are target-neutral lexical operations over `/` and `\` separators.
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

`std.path.basename`, `dirname`, `extension`, `stem`, `splitDir`, `splitBase`,
and `component` return borrowed views into the input path. `std.path.abs`,
`join`, `normalize`, and `relative` write into caller storage and return `null`
when the buffer is too small. They do not allocate.

The current behavior uses `/` as the portable package/example separator. These
helpers are lexical string helpers, not target-specific filesystem resolvers.
