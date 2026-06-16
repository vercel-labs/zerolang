## When To Use std.log

In Zerolang, use `std.log` for explicit-buffer structured log record formatting.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.log.levelDebug()` | `String` | Static `debug` level text. |
| `std.log.levelInfo()` | `String` | Static `info` level text. |
| `std.log.levelWarn()` | `String` | Static `warn` level text. |
| `std.log.levelError()` | `String` | Static `error` level text. |
| `std.log.message(buffer, level, message)` | `Maybe<Span<u8>>` | Writes one newline-terminated JSON Lines record with `level` and `message`. |
| `std.log.keyValue(buffer, level, key, value)` | `Maybe<Span<u8>>` | Writes one newline-terminated JSON Lines record with `level`, `key`, and `value`. |
| `std.log.stringField(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one JSON string field fragment. |
| `std.log.messageField(buffer, level, message, field)` | `Maybe<Span<u8>>` | Writes one JSON Lines message record with one field fragment. |
| `std.log.redacted(buffer, level, key)` | `Maybe<Span<u8>>` | Writes one JSON Lines record marking a field name as redacted. |

Metadata labels:

- effects: memory; `messageField` also validates JSON field fragments
- allocation behavior: writes caller buffer; no hidden heap
- target support: target-neutral
- error behavior: returns `null` when the buffer is too small or a value cannot be JSON-escaped
- ownership notes: borrows returned bytes from caller-owned storage
- example: `examples/std-testing-log.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [128]u8 = [0_u8; 128]
    var field_storage: [64]u8 = [0_u8; 64]
    let field: Maybe<Span<u8>> = std.log.stringField(field_storage, "event", "startup")
    if field.has {
        let entry: Maybe<Span<u8>> = std.log.messageField(storage, std.log.levelInfo(), "started", field.value)
        if entry.has {
            check world.out.write(entry.value)
        }
    }
}
```

Expected output:

```json
{"level":"info","message":"started","event":"startup"}
```

## Design Notes

`std.log` is a formatting surface, not a global logger. The caller owns the
storage and chooses where to write the resulting span, such as `World.out`, a
file handle, or a test assertion.

Records use JSON Lines so downstream tools can parse them without guessing at
ad hoc separators. The helpers write exactly one record and include a trailing
newline.

`messageField` validates the final JSON object before returning it. Build field
fragments with `stringField` unless the field fragment is already known to be
valid JSON.

Use `redacted` for logs that need to state which field was intentionally
withheld without writing the sensitive value.
