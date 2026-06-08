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
| `std.args.len()` | `usize` | Returns the process argument count. |
| `std.args.get(index)` | `Maybe<String>` | Returns the argument at `index` when present. |
| `std.args.has(index)` | `Bool` | Reports whether `index` has an argument. |
| `std.args.getOr(index, fallback)` | `String` | Returns the argument or a caller-provided fallback. |
| `std.args.find(name)` | `Maybe<usize>` | Finds the first exact argument match after the executable path. |
| `std.args.valueAfter(name)` | `Maybe<String>` | Returns the argument immediately after a matched option name. |
| `std.args.parseU32(index)` | `Maybe<u32>` | Parses an indexed argument as `u32`. |

Current limits:

- Iterator-style argument APIs.
- Target diagnostics for platforms without process arguments.

## Example

```zero
pub fn main(world: World) -> Void raises {
    let count: usize = std.args.len()
    let first: String = std.args.getOr(1, "default")
    let maybe_count: Maybe<u32> = std.args.parseU32(2)
    if count > 2 && maybe_count.has {
        check world.out.write(first)
        check world.out.write("\n")
    }
}
```

## Design Notes

The module is hosted-only. Freestanding, edge, and embedded targets should
reject it unless they explicitly provide an argument capability.

On native Windows-style targets, `std.args` is byte-oriented process input. It
is not a Unicode argv normalization layer.

Programs that need portable argument semantics should keep target-specific
decoding outside the target-neutral core.
