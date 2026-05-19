## Status

| API | Return | Notes |
| --- | --- | --- |
| `std.proc.run(argv, stdin, out, err, timeout)` | `ProcResult` | Executes an argv vector (no shell), captures stdout/stderr, enforces a timeout. |
| `std.proc.exitCode(result)` | `i32` | Child exit code. `128 + signal` if the child was signalled; `-1` on timeout/spawn failure. |
| `std.proc.outLen(result)` | `usize` | Bytes the child wrote to stdout (may exceed the `out` buffer; output is truncated to the buffer). |
| `std.proc.errLen(result)` | `usize` | Bytes the child wrote to stderr (truncated to the `err` buffer). |
| `std.proc.timedOut(result)` | `Bool` | `true` when the child was killed because `timeout` elapsed. |
| `std.proc.spawn(command)` | `ProcStatus` | Legacy status-only surface retained for source compatibility. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation in the caller; the host runtime allocates a
  transient argv table that it frees before returning
- target support: host
- error behavior: `run` returns `ProcResult`; inspect `std.proc.timedOut` and
  `std.proc.exitCode`. `spawn` returns `ProcStatus`; `exitCode` is infallible
- ownership notes: no ownership transfer; caller owns the `out`/`err` buffers
- example: `conformance/native/pass/std-proc-run.0`

## `std.proc.run`

```
std.proc.run(
  argv: Span<u8>,      // length-prefixed argv blob (see encoding below)
  stdin: Span<u8>,     // bytes written to the child's stdin
  out: MutSpan<u8>,    // captured stdout, truncated to capacity
  err: MutSpan<u8>,    // captured stderr, truncated to capacity
  timeout: Duration,   // <= 0 means no timeout; otherwise SIGKILL on expiry
) -> ProcResult
```

`argv` is a single flat byte blob so the runtime boundary stays a plain byte
view (no `Span<String>` aggregate is required):

```
u32  argc                    (little-endian)
repeat argc times:
  u32  arg_len               (little-endian)
  u8   arg_bytes[arg_len]    (raw, not NUL-terminated)
```

`argv[0]` is resolved through `execvp` (honours `PATH`); there is no shell
interpretation, so arguments are passed verbatim and are safe by construction.
The child runs with three pipes; stdout and stderr are drained without blocking
while `stdin` is written, and the child is sent `SIGKILL` if `timeout` elapses.

## Example

```zero
pub fun main(world: World) -> Void raises {
    let argv: [23]u8 = [
        2, 0, 0, 0,
        9, 0, 0, 0,
        47, 98, 105, 110, 47, 101, 99, 104, 111,
        2, 0, 0, 0,
        104, 105,
    ]
    let mut out: [64]u8 = [0_u8; 64]
    let mut err: [64]u8 = [0_u8; 64]
    let result = std.proc.run(argv, std.mem.span(""), out, err, std.time.seconds(5))
    if std.proc.exitCode(result) == 0 && std.proc.outLen(result) == 3 {
        check world.out.write("proc run ok\n")
    }
}
```

## Design Notes

`std.proc` is host-only. Cross targets without process support must reject
process helpers before code generation. They must not compile a placeholder
process implementation.

The runtime entry point takes nine register-width arguments (four byte views
plus the timeout). The direct native-exe backends currently cap calls at the
available argument registers, so `std.proc.run` reports `CGEN004` on the
native-exe path until the extended calling convention is available. The host
runtime, type surface, and conformance coverage are complete; the conformance
fixture accepts the `CGEN004` outcome today and exercises the real process path
automatically once the calling-convention support is in place.
