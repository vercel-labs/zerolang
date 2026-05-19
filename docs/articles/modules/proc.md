## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.proc.spawn(command)` | `ProcStatus` | Creates a process status through the explicit proc capability surface. |
| `std.proc.exitCode(status)` | `i32` | Reads the process status code. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation
- target support: host
- error behavior: `spawn` returns `ProcStatus`; `exitCode` is infallible
- ownership notes: no ownership transfer in the current status model
- example: `examples/std-platform.0`

## Example

```zero
pub fun main(world: World) -> Void raises {
    let status = std.proc.spawn("zero-noop")
    if std.proc.exitCode(status) == 0 {
        check world.out.write("proc ok\n")
    }
}
```

## Design Notes

`std.proc` is host-only. Cross targets without process support must reject
process helpers before code generation.

They should not compile a placeholder process implementation.
