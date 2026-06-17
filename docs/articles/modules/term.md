## When To Use std.term

In Zerolang, use `std.term` when terminal output needs ANSI control sequences
for style, cursor visibility, clearing, or alternate-screen rendering.

Runnable today:

| Helper | Returns | Notes |
| --- | --- | --- |
| `std.term.reset()` | `String` | ANSI SGR reset. |
| `std.term.bold()` | `String` | ANSI SGR bold style. |
| `std.term.dim()` | `String` | ANSI SGR dim style. |
| `std.term.inverse()` | `String` | ANSI SGR inverse style. |
| `std.term.fgDefault()` | `String` | Reset foreground color. |
| `std.term.fgRed()` | `String` | Red foreground color. |
| `std.term.fgGreen()` | `String` | Green foreground color. |
| `std.term.fgYellow()` | `String` | Yellow foreground color. |
| `std.term.fgBlue()` | `String` | Blue foreground color. |
| `std.term.fgMagenta()` | `String` | Magenta foreground color. |
| `std.term.fgCyan()` | `String` | Cyan foreground color. |
| `std.term.fgWhite()` | `String` | White foreground color. |
| `std.term.clearScreen()` | `String` | Clear the full terminal screen. |
| `std.term.clearLine()` | `String` | Clear the current terminal line. |
| `std.term.cursorHome()` | `String` | Move the cursor to row 1, column 1. |
| `std.term.hideCursor()` | `String` | Hide the terminal cursor. |
| `std.term.showCursor()` | `String` | Show the terminal cursor. |
| `std.term.enterAltScreen()` | `String` | Enter the alternate screen buffer. |
| `std.term.leaveAltScreen()` | `String` | Leave the alternate screen buffer. |

Metadata labels:

- effects: none
- allocation behavior: no allocation
- target support: target-neutral
- error behavior: infallible static string views
- ownership notes: borrowed static byte views
- example: `conformance/native/pass/std-term-ansi.graph`

Example:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write(std.term.enterAltScreen())
    check world.out.write(std.term.clearScreen())
    check world.out.write(std.term.cursorHome())
    check world.out.write(std.term.bold())
    check world.out.write(std.term.fgCyan())
    check world.out.write("ready")
    check world.out.write(std.term.reset())
    check world.out.write(std.term.leaveAltScreen())
}
```

`std.term` currently only provides output sequences. Raw mode, terminal size,
and key input helpers are separate hosted terminal capabilities.
