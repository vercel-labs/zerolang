## When To Use std.env

In Zerolang, use `std.env` for hosted environment variable lookup and simple typed
configuration values.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.env.get(name)` | `Maybe<String>` | Returns a hosted process environment value when present. |
| `std.env.has(name)` | `Bool` | Reports whether a hosted environment variable exists. |
| `std.env.getOr(name, fallback)` | `String` | Returns the environment value or a caller-provided fallback. |
| `std.env.equals(name, expected)` | `Bool` | Compares an environment value with expected text without exposing a missing value as success. |
| `std.env.parseBool(name)` | `Maybe<Bool>` | Parses an environment value as `Bool`. |
| `std.env.parseBoolOr(name, fallback)` | `Bool` | Parses a boolean environment value or returns a caller-provided fallback. |
| `std.env.parseI32(name)` | `Maybe<i32>` | Parses an environment value as `i32`. |
| `std.env.parseI32Or(name, fallback)` | `i32` | Parses an `i32` environment value or returns a caller-provided fallback. |
| `std.env.parseU32(name)` | `Maybe<u32>` | Parses an environment value as `u32`. |
| `std.env.parseU32Or(name, fallback)` | `u32` | Parses a `u32` environment value or returns a caller-provided fallback. |
| `std.env.parseUsize(name)` | `Maybe<usize>` | Parses an environment value as `usize`. |
| `std.env.parseUsizeOr(name, fallback)` | `usize` | Parses a `usize` environment value or returns a caller-provided fallback. |

Current limits:

- Dotenv/source composition.
- Secret redaction metadata.
- Rich diagnostics for missing keys, invalid values, and source precedence.

## Example

```zero
pub fn main(world: World) -> Void raises {
    let mode: String = std.env.getOr("ZERO_MODE", "default")
    let verbose: Bool = std.env.parseBoolOr("ZERO_VERBOSE", false)
    let delta: i32 = std.env.parseI32Or("ZERO_DELTA", 0)
    let limit: u32 = std.env.parseU32Or("ZERO_LIMIT", 10)
    let workers: usize = std.env.parseUsizeOr("ZERO_WORKERS", 1)
    if std.env.equals("ZERO_MODE", "debug") && verbose && delta >= 0 && limit > 0_u32 && workers > 0 {
        check world.out.write(mode)
        check world.out.write("\n")
    } else {
        check world.out.write("default\n")
    }
}
```

## Design Notes

Environment access is a hosted capability. Non-host targets reject `std.env`
unless they explicitly provide an environment capability.

Diagnostics name the selected target context.
