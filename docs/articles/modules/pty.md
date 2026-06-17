## When To Use std.pty

In Zerolang, use `std.pty` for hosted child processes that need terminal
semantics instead of plain pipes. PTY children are useful for interactive CLIs,
REPLs, shells, editors, pagers, and tools that change behavior when stdout is a
terminal.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.pty.spawn(command)` | `ProcChild` | Starts an argv-style command attached to a pseudoterminal. |
| `std.pty.spawnIn(command, cwd)` | `ProcChild` | Starts a PTY child in a hosted working directory. |
| `std.pty.spawnInEnv(command, cwd, env)` | `ProcChild` | Starts a PTY child with a working directory and newline-separated `KEY=value` environment bindings. |
| `std.pty.spawnArgs(program, args, cwd, env)` | `ProcChild` | Starts a program path plus newline-separated argv entries attached to a pseudoterminal. |
| `std.pty.valid(child)` | `Bool` | Reports whether the handle currently names an open child slot. |
| `std.pty.childValid(child)` | `Bool` | Alias for `std.pty.valid`. |
| `std.pty.running(child)` | `Bool` | Polls the PTY child without blocking. |
| `std.pty.wait(child)` | `ProcStatus` | Waits for process exit and returns its status. |
| `std.pty.kill(child)` | `Bool` | Sends the child a termination signal on supported hosts. |
| `std.pty.interrupt(child)` | `Bool` | Sends the child an interrupt signal on supported hosts. |
| `std.pty.close(child)` | `Bool` | Closes the handle and remaining terminal resources. |
| `std.pty.pid(child)` | `i32` | Returns the hosted process id for the child, or `0` when unavailable. |
| `std.pty.read(child, buffer)` | `Maybe<usize>` | Nonblocking read from the PTY master into caller storage. |
| `std.pty.write(child, bytes)` | `Maybe<usize>` | Nonblocking write to the PTY master. |
| `std.pty.resize(child, columns, rows)` | `Bool` | Sets the child terminal size on supported hosts. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation
- target support: host
- error behavior: spawn helpers return an invalid `ProcChild` handle on failure; `read` and `write` return `null` when no bytes can be transferred
- ownership notes: `ProcChild` values name runtime-owned process slots; call `wait` when process status matters and `close` when the handle is no longer needed

## Example

```zero
pub fn main(world: World) -> Void raises {
    let child: ProcChild = std.pty.spawnArgs("printf", "hello pty\n", ".", "")
    let resized: Bool = std.pty.resize(child, 80_usize, 24_usize)

    var storage: [64]u8 = [0_u8; 64]
    var saw_output: Bool = false
    var attempts: usize = 0
    while attempts < 20_usize && !saw_output {
        let read: Maybe<usize> = std.pty.read(child, storage)
        if read.has {
            let bytes: Span<u8> = std.io.written(storage, read.value)
            saw_output = std.mem.contains(bytes, "hello pty")
        }
        if !saw_output {
            let slept: Bool = std.time.sleep(std.time.ms(10))
        }
        attempts = attempts + 1_usize
    }

    let status: ProcStatus = std.pty.wait(child)
    if resized && saw_output && std.proc.succeeded(status) {
        check world.out.write("pty ok\n")
    }
    let closed: Bool = std.pty.close(child)
}
```

## Design Notes

`std.pty` returns the same `ProcChild` handle shape used by `std.proc`, but the
underlying child is connected to a pseudoterminal master. PTY output is a single
terminal byte stream: stderr is merged by the terminal, and `std.pty.read`
reads from that stream.
For short-lived programs, drain the PTY with `std.pty.read` before `wait`; some
hosts report the terminal as closed once the child exits.

Use `std.proc.spawnChild*` for programs where separate stdout and stderr pipes
matter. Use `std.pty.spawn*` for programs where terminal behavior matters.
