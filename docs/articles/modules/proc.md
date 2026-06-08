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
| `std.proc.spawn(command)` | `ProcStatus` | Creates a process status through the explicit proc capability surface. |
| `std.proc.exitCode(status)` | `i32` | Reads the process status code. |
| `std.proc.succeeded(status)` | `Bool` | Reports whether the status exit code is `0`. |
| `std.proc.failed(status)` | `Bool` | Reports whether the status exit code is nonzero. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation
- target support: host
- error behavior: `spawn` returns `ProcStatus`; `exitCode` is infallible
- ownership notes: no ownership transfer in the current status model
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let status: ProcStatus = std.proc.spawn("zero-noop")
    if std.proc.succeeded(status) {
        check world.out.write("proc ok\n")
    }
}
```

## Design Notes

`std.proc` is host-only. Cross targets without process support must reject
process helpers before code generation.

They should not compile a placeholder process implementation.

Process command builders with cwd, environment override, stdin, stdout, and
stderr plumbing are not exposed yet.
