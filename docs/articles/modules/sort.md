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
| `std.sort.insertionI32(items)` | `Void` | Sorts caller-owned mutable `i32` storage in ascending order. |
| `std.sort.isSortedI32(items)` | `Bool` | Checks whether `Span<i32>` input is sorted ascending. |
| `std.sort.insertionU32(items)` | `Void` | Sorts caller-owned mutable `u32` storage in ascending order. |
| `std.sort.isSortedU32(items)` | `Bool` | Checks whether `Span<u32>` input is sorted ascending. |
| `std.sort.insertionUsize(items)` | `Void` | Sorts caller-owned mutable `usize` storage in ascending order. |
| `std.sort.isSortedUsize(items)` | `Bool` | Checks whether `Span<usize>` input is sorted ascending. |

The first sort surface is deliberately small and typed. Generic comparator
sorting should wait for stronger comparator contracts instead of smuggling an
untyped callback convention into the standard library.

## Example

```zero
pub fn main(world: World) -> Void raises {
    var values: [5]i32 = [5, 1, 4, 2, 3]
    std.sort.insertionI32(values)
    if std.sort.isSortedI32(values) && values[0] == 1 && values[4] == 5 {
        check world.out.write("sort ok\n")
    }
}
```

Effects: writes to caller-provided mutable storage.

Allocation behavior: no allocation.

Error behavior: none.

Ownership: sort helpers are typed scalar helpers and do not move owned values.

Target support: current compiler targets.
