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
| `std.time.ns(value)` | `Duration` | Builds a nanosecond duration. |
| `std.time.us(value)` | `Duration` | Builds a microsecond duration. |
| `std.time.ms(value)` | `Duration` | Builds a millisecond duration. |
| `std.time.seconds(value)` | `Duration` | Builds a second duration. |
| `std.time.minutes(value)` | `Duration` | Builds a minute duration. |
| `std.time.hours(value)` | `Duration` | Builds an hour duration. |
| `std.time.zero()` | `Duration` | Returns a zero duration. |
| `std.time.add(a, b)` | `Duration` | Adds two durations. |
| `std.time.sub(a, b)` | `Duration` | Subtracts one duration from another. |
| `std.time.min(a, b)` | `Duration` | Returns the smaller duration. |
| `std.time.max(a, b)` | `Duration` | Returns the larger duration. |
| `std.time.clamp(value, low, high)` | `Duration` | Clamps a duration between normalized bounds. |
| `std.time.asNs(value)` | `i64` | Converts to nanoseconds. |
| `std.time.asUsFloor(value)` | `i64` | Converts to whole microseconds. |
| `std.time.asMsFloor(value)` | `i32` | Converts to whole milliseconds. |
| `std.time.asSecondsFloor(value)` | `i64` | Converts to whole seconds. |
| `std.time.lessThan(a, b)` | `Bool` | Compares two durations. |
| `std.time.isZero(value)` | `Bool` | Reports whether a duration is zero. |
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
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let a: Duration = std.time.ms(250)
    let b: Duration = std.time.seconds(1)
    let total: Duration = std.time.add(a, b)
    if std.time.asMsFloor(total) == 1250 {
        check world.out.write("duration ok\n")
    }
}
```

## Design Notes

Time is an effect when it observes the outside world.

Pure duration math can stay allocation-free and target-independent.
