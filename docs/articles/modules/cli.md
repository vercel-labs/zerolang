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
| `std.cli.optionU32(name)` | `Maybe<u32>` | Parses an option value as `u32`. |
| `std.cli.successExitCode()` | `i32` | Returns the conventional success exit code. |
| `std.cli.usageExitCode()` | `i32` | Returns the conventional command-line usage error code. |
| `std.cli.isHelp(command)` | `Bool` | Recognizes `help`, `--help`, and `-h`. |
| `std.cli.needsHelp()` | `Bool` | Reports whether the current invocation has no command or asks for help. |
| `std.cli.commandIn2(command, first, second)` | `Bool` | Checks a command against two accepted command names. |
| `std.cli.commandIn3(command, first, second, third)` | `Bool` | Checks a command against three accepted command names. |
| `std.cli.formatUsage(buffer, program, syntax)` | `Maybe<Span<u8>>` | Writes `usage: <program> <syntax>` into caller storage. |
| `std.cli.formatCommand(buffer, name, syntax, summary)` | `Maybe<Span<u8>>` | Writes one indented command help row. |
| `std.cli.formatOption(buffer, name, valueName, summary)` | `Maybe<Span<u8>>` | Writes one indented option help row. |
| `std.cli.formatError(buffer, message)` | `Maybe<Span<u8>>` | Writes an `error: ...` line. |
| `std.cli.formatUnknownCommand(buffer, command)` | `Maybe<Span<u8>>` | Writes a conventional unknown-command error. |
| `std.cli.formatMissingOperand(buffer, operand)` | `Maybe<Span<u8>>` | Writes a conventional missing-operand error. |
| `std.cli.formatInvalidOption(buffer, option)` | `Maybe<Span<u8>>` | Writes a conventional invalid-option error. |

Current limits:

- Table-driven command schemas.
- Bool, signed integer, and `usize` option shortcuts; compose `optionValue` with `std.parse`.
- Process exit from inside `std.cli`; use the returned exit-code constants with host process handling.

## Example

```zero
pub fn main(world: World) -> Void raises {
    if std.cli.needsHelp() {
        var usage_storage: [64]u8 = [0_u8; 64]
        let usage: Maybe<Span<u8>> = std.cli.formatUsage(usage_storage, "zero-test", "hello [name]")
        if usage.has {
            check world.out.write(usage.value)
            check world.out.write("\n")
        }
        return
    }
    let command: String = std.cli.commandOr("")
    if std.mem.eql(command, "hello") {
        let name: String = std.cli.argOr(2, "world")
        check world.out.write("hello ")
        check world.out.write(name)
        check world.out.write("\n")
        return
    }
    var error_storage: [64]u8 = [0_u8; 64]
    let error: Maybe<Span<u8>> = std.cli.formatUnknownCommand(error_storage, command)
    if error.has {
        check world.err.write(error.value)
        check world.err.write("\n")
    }
}
```

## Design Notes

`std.cli` is a thin, hosted layer over `std.args`. It keeps subcommand, fallback,
typed argument, help row, and usage error patterns regular without hiding
process arguments behind a global parser or allocating command tables.
