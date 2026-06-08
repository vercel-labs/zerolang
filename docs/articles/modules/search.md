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
| `std.search.indexOf(items, needle)` | `usize` | Returns the first matching index or `std.mem.len(items)` when absent. |
| `std.search.lastIndexOf(items, needle)` | `usize` | Returns the last matching index or `std.mem.len(items)` when absent. |
| `std.search.lowerBoundI32(items, needle)` | `usize` | Returns the insertion point in sorted `Span<i32>` input. |
| `std.search.binaryI32(items, needle)` | `usize` | Returns the matching sorted `Span<i32>` index or `std.mem.len(items)` when absent. |
| `std.search.lowerBoundU32(items, needle)` | `usize` | Returns the insertion point in sorted `Span<u32>` input. |
| `std.search.binaryU32(items, needle)` | `usize` | Returns the matching sorted `Span<u32>` index or `std.mem.len(items)` when absent. |
| `std.search.lowerBoundUsize(items, needle)` | `usize` | Returns the insertion point in sorted `Span<usize>` input. |
| `std.search.binaryUsize(items, needle)` | `usize` | Returns the matching sorted `Span<usize>` index or `std.mem.len(items)` when absent. |

Generic equality search supports the same non-owned scalar item types as
`std.mem.contains`.

## Example

```zero
pub fn main(world: World) -> Void raises {
    let values: [5]i32 = [1, 2, 3, 5, 8]
    let found: usize = std.search.binaryI32(values, 5)
    let missing: usize = std.search.indexOf(values, 13)
    if found == 3 && missing == std.mem.len(values) {
        check world.out.write("search ok\n")
    }
}
```

Effects: none.

Allocation behavior: no allocation.

Error behavior: absent values return the input length.

Ownership: generic equality search rejects owned item elements.

Target support: current compiler targets.
