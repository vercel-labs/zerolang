## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.path.join(buffer, left, right)` | `Maybe<String>` | Joins two path fragments into caller-provided fixed buffer storage. |

Current scope:

- `readBytes`/`writeBytes` stay alongside resource-oriented `Fs`/`File` APIs because they are useful small-program conveniences.
- `basename`, `dirname`, and extension helpers are not part of the current API.
- The module is intentionally small and is not a general path normalization library.

## Example

```zero
pub fn main Void world World !
  mut storage [64]u8 [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  let path std.path.join storage ".zero" "example.txt"
  if path.has
    check world.out.write path.value
    check world.out.write "\n"
```

## Design Notes

`std.path.join` writes into caller storage and returns `null` when the buffer is
too small. It does not allocate.

The current behavior uses `/` as the portable package/example separator. Windows
path normalization remains a documented limitation until target-specific path
rules are added.
