## Standard Library Reference

Zero's standard library is pay-as-used and capability-aware. Importing memory
helpers does not pull in hosted filesystem helpers.

Hosted APIs report their target requirements in `zero graph` and `zero size`.

Runnable modules:

- `std.mem`: spans, byte equality, copy/fill, fixed-buffer allocators, byte buffers, and arena-style reset helpers.
- `std.io`: buffered reader/writer metadata and byte copy helpers over caller-owned storage.
- `std.args`: hosted process argument count and indexed lookup.
- `std.env`: hosted environment variable lookup.
- `std.fs`: hosted file lifecycle helpers, owned file handles, byte reads/writes, remove, rename, and close.
- `std.path`: fixed-buffer path joining.
- `std.parse`: allocation-free ASCII scanners and unsigned integer parsers.
- `std.codec`: byte-oriented integer encoding, varint length, and CRC-32 helpers.
- `std.json`: string and byte-span validation, streaming token counts, explicit-allocator parsing, and caller-buffer string writing.
- `std.time`: duration math plus target-gated monotonic and wall-clock helpers.
- `std.rand`: explicit deterministic random sources and target entropy helpers.
- `std.proc`: host process status helpers behind the process capability.
- `std.crypto`: small hash, keyed hash, constant-time equality, and entropy helpers.
- `std.net`: network capability metadata and bootstrap connection/listener handles.
- `std.http`: HTTP method, body-length, client/server metadata, TLS-boundary helpers, hosted GET conveniences, raw request-envelope helpers, raw response-header capture, and header-value lookup.

Each module page documents target support, allocation behavior, error behavior,
ownership notes, and runnable examples.

Use the CLI to inspect what a program actually retains:

| Command | Shows |
| --- | --- |
| `zero graph --json <input>` | Required capabilities and imported helpers. |
| `zero size --json <input>` | Helper metadata and retained helper cost. |
| `zero mem --json <input>` | `memoryBudgets`, `allocatorFacts`, `allocationInstrumentation`, and `collectionFacts`. |

The `stdlibHelpers` and `usedStdlibHelpers` JSON entries include `module`,
`effects`, `allocationBehavior`, `targetSupport`, `errorBehavior`,
`ownershipNotes`, `example`, and `apiStability` for each public helper.

## Metadata Contract

Public standard library symbols document the fields agents need to call them
safely:

```text
symbol: std.fs.readAllOrRaise
effects: fs
allocation behavior: caller allocator
target support: host
error behavior: `![NotFound TooLarge Io]`
ownership notes: returns owned<ByteBuf>
example: examples/readall-cli/
```

Module pages may group related symbols when their metadata is identical.

Keep these labels visible: effects, allocation behavior, target support, error
behavior, ownership notes, and example.
