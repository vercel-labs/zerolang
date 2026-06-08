## Graph Surface

This module is graph-backed. The compiler uses its standard-library graph store,
while the Zero snippets below show the human-readable projection that agents may
export for review. Agents should discover helpers with `zero skills get stdlib`,
inspect user packages with `zero query [graph-input]` or
`zero inspect [graph-input]`, and patch user code through the graph instead of
hand-editing `.0` files.

Runnable today:

The collection helpers operate over caller-owned fixed arrays or `MutSpan<T>`
storage plus an explicit live length. They do not allocate, grow, or retain
hidden state. Generic helpers currently support the same non-owned scalar item
types as the generic `std.mem` item helpers: `Bool`, `u8`, `u16`, `usize`,
`i32`, `u32`, `i64`, and `u64`.

| API | Return | Notes |
| --- | --- | --- |
| `std.collections.push(items, len, value)` | `usize` | Writes `value` at `len` when capacity remains and returns the next length. Returns the unchanged length on overflow. |
| `std.collections.append(items, len, values)` | `usize` | Copies all non-overlapping `values` into `items` at `len` when the full append fits. Returns the unchanged length on overflow or invalid length. |
| `std.collections.view(items, len)` | `Span<T>` | Returns a clamped read-only prefix view over the live collection items. |
| `std.collections.contains(items, len, needle)` | `Bool` | Searches only the live prefix for `needle`. |
| `std.collections.count(items, len, needle)` | `usize` | Counts matching values in the live prefix. |
| `std.collections.removeSwap(items, len, index)` | `usize` | Replaces `index` with the last live item and returns the next length. Returns the unchanged length for invalid length or index. |
| `std.collections.moveToFront(items, len, index)` | `usize` | Moves the item at `index` to the front by shifting the live prefix. Returns the unchanged length for invalid length or index. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var values: [5]i32 = [0, 0, 0, 0, 0]
    let extra: [2]i32 = [4, 1]
    var len: usize = 0
    len = std.collections.push(values, len, 3)
    len = std.collections.push(values, len, 1)
    len = std.collections.append(values, len, extra)
    let live: Span<i32> = std.collections.view(values, len)
    if len == 4 && std.collections.contains(values, len, 4) && std.collections.count(values, len, 1) == 2 && std.mem.len(live) == 4 {
        check world.out.write("collections ok\n")
    }
}
```

Effects: writes to caller-provided mutable storage.

Allocation behavior: no allocation.

Error behavior: capacity and index failures are value-level. Helpers return the
unchanged length instead of growing or raising.

`append` rejects source spans that the checker can prove overlap the destination
storage. Use separate storage when copying a live prefix back into the same
collection.

Ownership: helpers reject owned item elements; move or transfer owned values
explicitly.

Target support: current compiler targets.
