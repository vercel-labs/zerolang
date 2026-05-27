---
name: stdlib
description: Use Zero standard library modules and target-gated capabilities.
---

# Zero Standard Library

Use this when an agent needs common library calls, memory helpers, hosted I/O, or target-capability guidance.

## Import

```zero
use std.mem
use std.parse
```

Call functions with their module path, such as `std.mem.len(value)`.

## Target-Neutral Helpers

- `std.mem`: spans, copy, fill, length, safe indexed `get`, fixed-buffer allocation, byte buffers, and caller-owned vectors.
- `std.math`: pure `u32` integer helpers, GCD/LCM, powers, modular power, primality, and divisor routines.
- `std.codec`: byte reads, varint sizing, CRC helpers, and byte checksums.
- `std.parse`: ASCII predicates and decimal integer parsers returning `Maybe<T>`.
- `std.time`: duration construction and conversion helpers.
- `std.rand`: explicit deterministic random sources.
- `std.crypto`: small hash and byte-oriented crypto helpers.
- `std.json`: explicit-buffer JSON parsing and string writing helpers.
- `std.str`: byte-span string helpers, including non-overlapping reverse, prefix/suffix, substring, trim, and word counts.
- `std.io`: buffered reader/writer surfaces over caller-owned storage.

Prefer `Maybe<T>` return checks over assuming an operation succeeded.

## Hosted Capabilities

These modules depend on host or runtime capabilities:

- `std.args`: process arguments
- `std.env`: process environment
- `std.fs`: hosted filesystem and explicit `Fs` or `owned<File>` handles
- `std.net`: bootstrap network handles
- `std.http`: HTTP request/response helpers
- `std.proc`: process execution helpers
- `World.out` and `World.err`: program output capabilities

Non-host targets may reject these APIs with target diagnostics. Inspect target facts before cross-building:

```sh
zero targets
zero check --target linux-musl-x64 <input>
zero graph --target linux-musl-x64 <input>
```

Add `--json` only when a tool needs exact target facts or diagnostics.

## Memory Pattern

```zero
use std.mem

pub fn main Void world World !
  let bytes Span<u8> std.mem.span "zero"
  if == (std.mem.len bytes) 4
    check world.out.write "memory ok\n"
```

For writable buffers, use caller-owned fixed arrays and `MutSpan<T>`:

```zero
mut storage [8]u8 [0, 0, 0, 0, 0, 0, 0, 0]
let writable MutSpan<u8> storage
let copied std.mem.copy writable (std.mem.span "zero")
```

String helpers are byte-oriented and allocation-free. `std.str.reverse` writes
into caller storage and requires that destination storage does not overlap the
input text:

```zero
mut reversed [4]u8 [0, 0, 0, 0]
let out std.str.reverse reversed "zero"
if out.has
  expect (std.mem.eql out.value "orez")
```

## Maybe Pattern

```zero
let first std.args.get 1
if first.has
  check world.out.write first.value
```

Use `check maybeValue` only when absence should propagate as a failure in a fallible function.

## Resource Pattern

Hosted file APIs can use explicit handles:

```zero
let fs std.fs.host()
mut file owned<File> check std.fs.createOrRaise fs ".zero/out/log.txt"
check std.fs.writeAllOrRaise (&mut file) (std.mem.span "hello\n")
```

Owned resources are deterministic. Do not invent hidden heap, global logger, or ambient filesystem APIs.
