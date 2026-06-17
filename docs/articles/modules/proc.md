## When To Use std.proc

In Zerolang, use `std.proc` for hosted process helpers behind explicit process
capability boundaries. The surface is intentionally small and host-only.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.proc.spawn(command)` | `ProcStatus` | Creates a process status through the explicit proc capability surface. |
| `std.proc.exitCode(status)` | `i32` | Reads the process status code. |
| `std.proc.succeeded(status)` | `Bool` | Reports whether the status exit code is `0`. |
| `std.proc.failed(status)` | `Bool` | Reports whether the status exit code is nonzero. |
| `std.proc.runOk(command)` | `Bool` | Spawns a hosted command and reports whether the resulting status succeeded. |
| `std.proc.runCode(command)` | `i32` | Spawns a hosted command and returns its exit code. |
| `std.proc.capture(command, buffer)` | `Maybe<usize>` | Runs an argv-style command and captures stdout into caller storage. Returns `null` on parse failure, spawn failure, nonzero exit, unsupported target, or output truncation. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation
- target support: host
- error behavior: `spawn` returns `ProcStatus`; `exitCode` is infallible; `capture` returns `null` when it cannot produce complete stdout
- ownership notes: no ownership transfer in the current status model
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let status: ProcStatus = std.proc.spawn("zero-noop")
    var storage: [64]u8 = [0_u8; 64]
    let captured: Maybe<usize> = std.proc.capture("zero-noop", storage)
    if std.proc.succeeded(status) && std.proc.runOk("zero-noop") && std.proc.runCode("zero-noop") == 0 && captured.has {
        check world.out.write("proc ok\n")
    }
}
```

## Design Notes

`std.proc` is host-only. Cross targets without process support must reject
process helpers before code generation.

They should not compile a placeholder process implementation.

`capture` does not invoke a shell. It splits simple argv-style command text,
supports quoted arguments, and captures stdout only. Process command builders
with cwd, environment override, stdin, stderr plumbing, and streaming handles
are not exposed yet.
