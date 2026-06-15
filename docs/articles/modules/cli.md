## When To Use std.cli

In Zerolang, use `std.cli` for hosted command-line flag and option helpers that sit one
level above raw `std.args` access.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.cli.argEquals(index, expected)` | `Bool` | Checks one argument against an exact string. |
| `std.cli.command()` | `Maybe<String>` | Returns argument 1 as the command name. |
| `std.cli.commandOr(fallback)` | `String` | Returns the command name or a fallback. |
| `std.cli.commandEquals(expected)` | `Bool` | Checks argument 1 against an exact command string. |
| `std.cli.argOr(index, fallback)` | `String` | Returns an argument or a fallback. |
| `std.cli.argU32Or(index, fallback)` | `u32` | Parses an argument as `u32` or returns a fallback. |
| `std.cli.hasFlag(name)` | `Bool` | Reports whether an exact flag is present. |
| `std.cli.optionValue(name)` | `Maybe<String>` | Returns the value immediately after an option name. |
| `std.cli.optionValueOr(name, fallback)` | `String` | Returns the option value or a fallback. |
| `std.cli.optionBool(name)` | `Maybe<Bool>` | Parses an option value as `Bool`. |
| `std.cli.optionBoolOr(name, fallback)` | `Bool` | Parses an option value as `Bool` or returns a fallback. |
| `std.cli.optionI32(name)` | `Maybe<i32>` | Parses an option value as `i32`. |
| `std.cli.optionI32Or(name, fallback)` | `i32` | Parses an option value as `i32` or returns a fallback. |
| `std.cli.optionU32(name)` | `Maybe<u32>` | Parses an option value as `u32`. |
| `std.cli.optionU32Or(name, fallback)` | `u32` | Parses an option value as `u32` or returns a fallback. |
| `std.cli.optionUsize(name)` | `Maybe<usize>` | Parses an option value as `usize`. |
| `std.cli.optionUsizeOr(name, fallback)` | `usize` | Parses an option value as `usize` or returns a fallback. |
| `std.cli.successExitCode()` | `i32` | Returns the conventional success exit code. |
| `std.cli.usageExitCode()` | `i32` | Returns the conventional command-line usage error code. |

Current limits:

- Help generation.
- Subcommand tables.
- Structured diagnostic rendering.

## Example

```zero
pub fn main(world: World) -> Void raises {
    if std.cli.commandEquals("hello") {
        let name: String = std.cli.argOr(2, "world")
        check world.out.write("hello ")
        check world.out.write(name)
        check world.out.write("\n")
        return
    }
    check world.err.write("usage: zero run -- hello [name]\n")
}
```

## Design Notes

`std.cli` is a thin, hosted layer over `std.args`. It keeps subcommand, fallback,
and typed argument patterns regular without hiding process arguments behind a
global parser.
