## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.log.message(buffer, level, message)` | `Maybe<Span<u8>>` | Writes one newline-terminated JSON Lines record with `level` and `message`. |
| `std.log.keyValue(buffer, level, key, value)` | `Maybe<Span<u8>>` | Writes one newline-terminated JSON Lines record with `level`, `key`, and `value`. |

Metadata labels:

- effects: memory
- allocation behavior: writes caller buffer; no hidden heap
- target support: target-neutral
- error behavior: returns `null` when the buffer is too small or a value cannot be JSON-escaped
- ownership notes: borrows returned bytes from caller-owned storage
- example: `examples/std-testing-log.0`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [128]u8 = [0_u8; 128]
    let entry: Maybe<Span<u8>> = std.log.keyValue(storage, "info", "event", "startup")
    if entry.has {
        check world.out.write(entry.value)
    }
}
```

Expected output:

```json
{"level":"info","key":"event","value":"startup"}
```

## Design Notes

`std.log` is a formatting surface, not a global logger. The caller owns the
storage and chooses where to write the resulting span, such as `World.out`, a
file handle, or a test assertion.

Records use JSON Lines so downstream tools can parse them without guessing at
ad hoc separators. The helpers write exactly one record and include a trailing
newline.
