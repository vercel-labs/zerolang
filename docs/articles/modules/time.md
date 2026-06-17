## When To Use std.time

In Zerolang, use `std.time` for duration math, RFC 3339 date and time validation
and parsing, and target-gated monotonic or wall-clock helpers.

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
| `std.time.abs(value)` | `Duration` | Returns a non-negative duration magnitude. |
| `std.time.between(start, end)` | `Duration` | Returns the non-negative duration between two values. |
| `std.time.hasElapsed(start, now, timeout)` | `Bool` | Reports whether a timeout window has elapsed. |
| `std.time.deadlineAfter(start, timeout)` | `Duration` | Builds a deadline by adding a timeout to a start instant. |
| `std.time.remainingUntil(deadline, now)` | `Duration` | Returns remaining time or zero once the deadline has passed. |
| `std.time.deadlineExpired(deadline, now)` | `Bool` | Reports whether `now` is at or past `deadline`. |
| `std.time.sleep(duration)` | `Bool` | Sleeps for a hosted non-negative duration; returns `false` on host failure. |
| `std.time.asNs(value)` | `i64` | Converts to nanoseconds. |
| `std.time.asUsFloor(value)` | `i64` | Converts to whole microseconds. |
| `std.time.asMsFloor(value)` | `i32` | Converts to whole milliseconds. |
| `std.time.asSecondsFloor(value)` | `i64` | Converts to whole seconds. |
| `std.time.lessThan(a, b)` | `Bool` | Compares two durations. |
| `std.time.isZero(value)` | `Bool` | Reports whether a duration is zero. |
| `std.time.monotonic()` | `Duration` | Reads a monotonic target clock where available. |
| `std.time.wallSeconds()` | `i64` | Reads target wall-clock seconds where available. |
| `std.time.isRfc3339Date(text)` | `Bool` | Validates an RFC 3339 full-date with leap years and days-in-month. |
| `std.time.isRfc3339Time(text)` | `Bool` | Validates an RFC 3339 full-time with fractional seconds, numeric offsets, and the leap-second rule. |
| `std.time.isRfc3339DateTime(text)` | `Bool` | Validates an RFC 3339 date-time joined by `T` or `t`. |
| `std.time.parseRfc3339DateTimeOr(text, fallback)` | `i64` | Parses a date-time into UTC epoch seconds; returns the fallback when invalid. Fractional seconds truncate; a valid leap second maps to the same epoch second as `:59`. |
| `std.time.isLeapYear(year)` | `Bool` | Gregorian leap-year predicate. |
| `std.time.daysInMonth(year, month)` | `u32` | Days in a month; returns `0` for invalid months. |
| `std.time.writeDurationNs(buffer, value)` | `Maybe<Span<u8>>` | Writes nanoseconds with an `ns` suffix into caller storage. |
| `std.time.writeDurationMs(buffer, value)` | `Maybe<Span<u8>>` | Writes whole milliseconds with an `ms` suffix into caller storage. |
| `std.time.writeDurationSeconds(buffer, value)` | `Maybe<Span<u8>>` | Writes whole seconds with an `s` suffix into caller storage. |

Current limits:

- Target-specific clock availability diagnostics.
- Timer handles and fake-clock handles are not public APIs.

Metadata labels:

- effects: time
- allocation behavior: no allocation
- target support: duration math is target-neutral; clock reads and sleep require a time-capable target
- error behavior: infallible helpers; RFC 3339 validators return `Bool` and the epoch parser returns its fallback for invalid text
- ownership notes: no ownership transfer
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let a: Duration = std.time.ms(250)
    let b: Duration = std.time.seconds(1)
    let total: Duration = std.time.add(a, b)
    let span: Duration = std.time.between(std.time.seconds(2), std.time.ms(250))
    let deadline: Duration = std.time.deadlineAfter(std.time.seconds(10), std.time.ms(500))
    let remaining: Duration = std.time.remainingUntil(deadline, std.time.seconds(10))
    let slept: Bool = std.time.sleep(std.time.zero())
    var text_storage: [32]u8 = [0_u8; 32]
    let text: Maybe<Span<u8>> = std.time.writeDurationMs(text_storage, total)
    if slept && std.time.asMsFloor(total) == 1250 && std.time.asMsFloor(span) == 1750 && (std.time.asMsFloor(remaining) == 500 && text.has) {
        check world.out.write("duration ok\n")
    }
}
```

RFC 3339 validation includes the exact leap-second rule: `seconds == 60` is
valid only when the time normalized by its numeric offset equals `23:59:60`
UTC, wrapping modulo 24 hours. `00:29:60+00:30` is valid because it normalizes
to `23:59:60` UTC on the previous day, while `23:59:60-01:00` is invalid
because it normalizes to `00:59:60` UTC.

```zero
pub fn main(world: World) -> Void raises {
    let wrapped: Bool = std.time.isRfc3339Time("00:29:60+00:30")
    let not_leap: Bool = std.time.isRfc3339Time("23:59:60-01:00")
    let epoch: i64 = std.time.parseRfc3339DateTimeOr("2000-01-01T00:00:00Z", -1)
    if wrapped && !not_leap && epoch == 946684800 && std.time.daysInMonth(2024, 2) == 29 {
        check world.out.write("rfc3339 ok\n")
    }
}
```

## Design Notes

Time is an effect when it observes or waits on the outside world.

Pure duration math can stay allocation-free and target-independent.
Timer and fake-clock APIs are not exposed in the current public surface.
