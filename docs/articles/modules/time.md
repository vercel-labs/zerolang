## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.time.ms(value)` | `Duration` | Builds a millisecond duration. |
| `std.time.seconds(value)` | `Duration` | Builds a second duration. |
| `std.time.add(a, b)` | `Duration` | Adds two durations. |
| `std.time.sub(a, b)` | `Duration` | Subtracts one duration from another. |
| `std.time.min(a, b)` | `Duration` | Returns the smaller duration. |
| `std.time.max(a, b)` | `Duration` | Returns the larger duration. |
| `std.time.asMsFloor(value)` | `i32` | Converts to whole milliseconds. |
| `std.time.lessThan(a, b)` | `Bool` | Compares two durations. |
| `std.time.monotonic()` | `Duration` | Reads a monotonic target clock where available. |
| `std.time.wallSeconds()` | `i64` | Reads target wall-clock seconds where available. |

Current limits:

- Monotonic instants.
- Deadlines and request budgets.
- Target-specific clock availability diagnostics.

Metadata labels:

- effects: time
- allocation behavior: no allocation
- target support: duration math is target-neutral; clock reads require a time-capable target
- error behavior: infallible helpers
- ownership notes: no ownership transfer
- example: `examples/std-platform.0`

## Example

```zero
pub fn main Void world World !
  let a std.time.ms 250
  let b std.time.seconds 1
  let total std.time.add a b
  if == (std.time.asMsFloor total) 1250
    check world.out.write "duration ok\n"
```

## Design Notes

Time is an effect when it observes the outside world.

Pure duration math can stay allocation-free and target-independent.
