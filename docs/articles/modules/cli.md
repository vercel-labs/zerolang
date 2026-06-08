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
| `std.cli.argEquals(index, expected)` | `Bool` | Checks one argument against an exact string. |
| `std.cli.hasFlag(name)` | `Bool` | Reports whether an exact flag is present. |
| `std.cli.optionValue(name)` | `Maybe<String>` | Returns the value immediately after an option name. |
| `std.cli.optionValueOr(name, fallback)` | `String` | Returns the option value or a fallback. |
| `std.cli.optionU32(name)` | `Maybe<u32>` | Parses an option value as `u32`. |
| `std.cli.successExitCode()` | `i32` | Returns the conventional success exit code. |
| `std.cli.usageExitCode()` | `i32` | Returns the conventional command-line usage error code. |

Current limits:

- Help generation.
- Subcommand tables.
- Structured diagnostic rendering.

## Example

```zero
pub fn main(world: World) -> Void raises {
    let name: String = std.cli.optionValueOr("--name", "zero")
    let count: Maybe<u32> = std.cli.optionU32("--count")
    if std.cli.hasFlag("--json") && count.has {
        check world.out.write(name)
        check world.out.write("\n")
    }
}
```

## Design Notes

`std.cli` is a thin, hosted layer over `std.args`. It does not hide process
arguments behind a global parser, and it does not allocate.
