## When To Use std.proc

In Zerolang, use `std.proc` for hosted process helpers behind explicit process
capability boundaries. The surface is host-only and supports both status-style
helpers and owned child handles for incremental I/O.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.proc.spawn(command)` | `ProcStatus` | Creates a process status through the explicit proc capability surface. |
| `std.proc.spawnInherit(command)` | `ProcStatus` | Runs an argv-style command while inheriting stdin, stdout, and stderr from the parent process. |
| `std.proc.spawnInheritArgs(program, args, cwd, env)` | `ProcStatus` | Runs a program path plus newline-separated argv entries while inheriting stdio, using a working directory and newline-separated `KEY=value` environment bindings. |
| `std.proc.exitCode(status)` | `i32` | Reads the process status code. |
| `std.proc.succeeded(status)` | `Bool` | Reports whether the status exit code is `0`. |
| `std.proc.failed(status)` | `Bool` | Reports whether the status exit code is nonzero. |
| `std.proc.runOk(command)` | `Bool` | Spawns a hosted command and reports whether the resulting status succeeded. |
| `std.proc.runCode(command)` | `i32` | Spawns a hosted command and returns its exit code. |
| `std.proc.capture(command, buffer)` | `Maybe<usize>` | Runs an argv-style command and captures stdout into caller storage. Returns `null` on parse failure, spawn failure, nonzero exit, unsupported target, or output truncation. |
| `std.proc.captureArgs(program, args, buffer)` | `Maybe<usize>` | Runs a program path plus newline-separated argv entries and captures stdout into caller storage. |
| `std.proc.captureFiles(command, stdoutPath, stderrPath)` | `ProcStatus` | Runs an argv-style command and writes stdout and stderr to hosted paths. Returns `127` when the command cannot be parsed, spawned, waited on, or the output files cannot be opened. |
| `std.proc.captureFilesArgs(program, args, stdoutPath, stderrPath)` | `ProcStatus` | Runs a program path plus newline-separated argv entries and redirects stdout and stderr to hosted paths. |
| `std.proc.spawnChild(command)` | `ProcChild` | Starts a hosted child process with piped stdin, stdout, and stderr. Returns an invalid handle when the process cannot be created. |
| `std.proc.spawnChildIn(command, cwd)` | `ProcChild` | Starts a hosted child process in a working directory with piped stdin, stdout, and stderr. Returns an invalid handle when the cwd is invalid or the process cannot be created. |
| `std.proc.spawnChildInEnv(command, cwd, env)` | `ProcChild` | Starts a hosted child process in a working directory with piped stdin/stdout/stderr and explicit newline-separated `KEY=value` environment bindings. |
| `std.proc.spawnChildArgs(program, args, cwd, env)` | `ProcChild` | Starts a hosted child process from a program path plus newline-separated argv entries, working directory, and newline-separated `KEY=value` environment bindings. |
| `std.proc.childValid(child)` | `Bool` | Reports whether the handle currently names an open child slot. |
| `std.proc.running(child)` | `Bool` | Polls the child process without blocking. |
| `std.proc.wait(child)` | `ProcStatus` | Waits for process exit and returns its status. |
| `std.proc.kill(child)` | `Bool` | Sends the child a termination signal on supported hosts. |
| `std.proc.interrupt(child)` | `Bool` | Sends the child an interrupt signal on supported hosts. |
| `std.proc.close(child)` | `Bool` | Closes the handle and any remaining pipes. |
| `std.proc.closeStdin(child)` | `Bool` | Closes the child stdin pipe while keeping stdout, stderr, and status available. |
| `std.proc.pid(child)` | `i32` | Returns the hosted process id for a child handle, or `0` when unavailable. |
| `std.proc.pidRunning(pid)` | `Bool` | Reports whether a hosted process id appears to be running. |
| `std.proc.killPid(pid)` | `Bool` | Sends a termination signal to a hosted process id on supported hosts. |
| `std.proc.interruptPid(pid)` | `Bool` | Sends an interrupt signal to a hosted process id on supported hosts. |
| `std.proc.readStdout(child, buffer)` | `Maybe<usize>` | Nonblocking read from the child's stdout pipe into caller storage. Returns `null` when no bytes are currently available or the stream is closed. |
| `std.proc.readStderr(child, buffer)` | `Maybe<usize>` | Nonblocking read from the child's stderr pipe into caller storage. Returns `null` when no bytes are currently available or the stream is closed. |
| `std.proc.writeStdin(child, bytes)` | `Maybe<usize>` | Nonblocking write to the child's stdin pipe. Returns `null` when the stream is not writable. |

Metadata labels:

- effects: proc
- allocation behavior: no allocation
- target support: host
- error behavior: `spawn`, `spawnInherit`, `captureFiles`, and `wait` return `ProcStatus`; `exitCode` is infallible; `capture` and child I/O helpers return `null` when they cannot produce a complete result
- ownership notes: `ProcChild` values name runtime-owned process slots; call `wait` when process status matters and `close` when the handle is no longer needed
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let status: ProcStatus = std.proc.spawn("zero-noop")
    var storage: [64]u8 = [0_u8; 64]
    let captured: Maybe<usize> = std.proc.capture("zero-noop", storage)
    let files: ProcStatus = std.proc.captureFiles("zero-noop", ".zero/proc.out", ".zero/proc.err")
    if std.proc.succeeded(status) && std.proc.succeeded(files) && std.proc.runOk("zero-noop") && std.proc.runCode("zero-noop") == 0 && captured.has {
        check world.out.write("proc ok\n")
    }
}
```

Incremental child I/O uses caller-owned buffers:

```zero
pub fn main(world: World) -> Void raises {
    let child: ProcChild = std.proc.spawnChild("zero-noop")
    var stdoutStorage: [64]u8 = [0_u8; 64]
    let out: Maybe<usize> = std.proc.readStdout(child, stdoutStorage)
    if out.has {
        check world.out.write("child stdout available\n")
    }
    let pid: i32 = std.proc.pid(child)
    let status: ProcStatus = std.proc.wait(child)
    let closed: Bool = std.proc.close(child)
    if pid > 0 && std.proc.succeeded(status) && closed {
        check world.out.write("child ok\n")
    }
}
```

## Design Notes

`std.proc` is host-only. Cross targets without process support must reject
process helpers before code generation.

They should not compile a placeholder process implementation.

`capture` and `captureFiles` do not invoke a shell. They split simple
argv-style command text and support quoted arguments. `capture` captures stdout
only into caller storage. `captureFiles` redirects stdout and stderr to
separate hosted paths.

`spawnInherit` uses the same argv-style parser and leaves stdin, stdout, and
stderr connected to the parent process. `spawnInheritArgs` uses an explicit
program plus newline-separated argv entries, working directory, and environment
block. Use inherited stdio for editor, pager, and terminal program launches
where captured pipes would be the wrong interface.

Child handles use the same command parser when created from command text. The
`*Args` helpers avoid command-text parsing: `program` is argv[0], and each
non-empty line in `args` becomes one following argv entry with spaces preserved.
`captureArgs` captures stdout into caller storage, `captureFilesArgs` redirects
stdout and stderr to hosted paths, and `spawnChildArgs` returns nonblocking
pipes so event loops can poll process state and terminal input without owning
threads.

`spawnChildInEnv`, `spawnInheritArgs`, and `spawnChildArgs` accept a
newline-separated env block such as `"TOKEN=...\nMODE=batch"`. Empty lines are
ignored. Invalid entries or oversized env blocks make the helper fail: status
helpers return an error status and child helpers return an invalid handle.
