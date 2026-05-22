## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.env.get(name)` | `Maybe<String>` | Returns a hosted process environment value when present. |

Current limits:

- Dotenv/source composition.
- Typed decoding helpers.
- Secret redaction metadata.
- Rich diagnostics for missing keys, invalid values, and source precedence.

## Example

```zero
pub fn main Void world World !
  let mode std.env.get "ZERO_MODE"
  if mode.has
    check world.out.write mode.value
    check world.out.write "\n"
  else
    check world.out.write "default\n"
```

## Design Notes

Environment access is a hosted capability. Non-host targets reject `std.env`
unless they explicitly provide an environment capability.

Diagnostics name the selected target context.
