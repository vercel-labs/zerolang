## When To Use std.term

In Zerolang, use `std.term` when terminal code needs ANSI output sequences or
when a terminal UI needs to decode key bytes already read from input.

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
| `std.term.keyNone()` | `u32` | Sentinel returned for incomplete or unsupported key bytes. |
| `std.term.keyEscape()` | `u32` | Escape key code. |
| `std.term.keyEnter()` | `u32` | Enter key code for `\r` or `\n`. |
| `std.term.keyTab()` | `u32` | Tab key code. |
| `std.term.keyBackspace()` | `u32` | Backspace key code for `0x7f` or `0x08`. |
| `std.term.keyCtrlC()` | `u32` | Ctrl-C key code. |
| `std.term.keyArrowUp()` | `u32` | Up-arrow key code above the Unicode scalar range. |
| `std.term.keyArrowDown()` | `u32` | Down-arrow key code above the Unicode scalar range. |
| `std.term.keyArrowRight()` | `u32` | Right-arrow key code above the Unicode scalar range. |
| `std.term.keyArrowLeft()` | `u32` | Left-arrow key code above the Unicode scalar range. |
| `std.term.keyDelete()` | `u32` | Delete key code above the Unicode scalar range. |
| `std.term.keyCode(bytes)` | `u32` | Decodes one key from caller-provided bytes, returning Unicode scalar values for printable UTF-8 and named constants for control keys. |
| `std.term.keyByteLen(bytes)` | `usize` | Returns the decoded key width in bytes, or `0` for incomplete or unsupported input. |

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

Key decoding is target-neutral: it parses bytes the caller already has. Raw
mode, terminal size, and reading terminal input are separate hosted terminal
capabilities.
