## When To Use std.sort

In Zerolang, use `std.sort` for in-place sorting and scalar ordering helpers
over caller-owned storage.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.sort.insertionI32(items)` | `Void` | Sorts caller-owned mutable `i32` storage in ascending order. |
| `std.sort.insertionDescI32(items)` | `Void` | Sorts caller-owned mutable `i32` storage in descending order. |
| `std.sort.stableI32(items)` | `Void` | Stable ascending sort over caller-owned mutable `i32` storage. |
| `std.sort.unstableI32(items)` | `Void` | Unstable ascending sort over caller-owned mutable `i32` storage. |
| `std.sort.stableDescI32(items)` | `Void` | Stable descending sort over caller-owned mutable `i32` storage. |
| `std.sort.unstableDescI32(items)` | `Void` | Unstable descending sort over caller-owned mutable `i32` storage. |
| `std.sort.reverseI32(items)` | `Void` | Reverses caller-owned mutable `i32` storage in place. |
| `std.sort.swapI32(items, left, right)` | `Bool` | Swaps two in-bounds `i32` elements and returns `false` for invalid indexes. |
| `std.sort.rotateLeftI32(items, amount)` | `Void` | Rotates caller-owned mutable `i32` storage left by `amount` positions. |
| `std.sort.rotateRightI32(items, amount)` | `Void` | Rotates caller-owned mutable `i32` storage right by `amount` positions. |
| `std.sort.isSortedI32(items)` | `Bool` | Checks whether `Span<i32>` input is sorted ascending. |
| `std.sort.isSortedDescI32(items)` | `Bool` | Checks whether `Span<i32>` input is sorted descending. |
| `std.sort.partitionI32(items, pivot)` | `usize` | Moves values below `pivot` before the rest and returns the split index. |
| `std.sort.partitionDescI32(items, pivot)` | `usize` | Moves values above `pivot` before the rest and returns the split index. |
| `std.sort.isPartitionedI32(items, pivot)` | `Bool` | Checks whether all values below `pivot` are before the remaining `i32` values. |
| `std.sort.isPartitionedDescI32(items, pivot)` | `Bool` | Checks whether all values above `pivot` are before the remaining `i32` values. |
| `std.sort.dedupeSortedI32(items)` | `usize` | Compacts sorted mutable `i32` storage in place and returns the unique prefix length. |
| `std.sort.selectNthI32(items, index)` | `Bool` | Partially reorders mutable `i32` storage so `items[index]` contains the nth ascending value. |
| `std.sort.selectNthDescI32(items, index)` | `Bool` | Partially reorders mutable `i32` storage so `items[index]` contains the nth descending value. |
| `std.sort.mergeSortedI32(dst, left, right)` | `usize` | Merges ascending sorted `i32` inputs into caller storage and returns the written count. |
| `std.sort.mergeSortedDescI32(dst, left, right)` | `usize` | Merges descending sorted `i32` inputs into caller storage and returns the written count. |
| `std.sort.insertionU32(items)` | `Void` | Sorts caller-owned mutable `u32` storage in ascending order. |
| `std.sort.insertionDescU32(items)` | `Void` | Sorts caller-owned mutable `u32` storage in descending order. |
| `std.sort.stableU32(items)` | `Void` | Stable ascending sort over caller-owned mutable `u32` storage. |
| `std.sort.unstableU32(items)` | `Void` | Unstable ascending sort over caller-owned mutable `u32` storage. |
| `std.sort.stableDescU32(items)` | `Void` | Stable descending sort over caller-owned mutable `u32` storage. |
| `std.sort.unstableDescU32(items)` | `Void` | Unstable descending sort over caller-owned mutable `u32` storage. |
| `std.sort.reverseU32(items)` | `Void` | Reverses caller-owned mutable `u32` storage in place. |
| `std.sort.swapU32(items, left, right)` | `Bool` | Swaps two in-bounds `u32` elements and returns `false` for invalid indexes. |
| `std.sort.rotateLeftU32(items, amount)` | `Void` | Rotates caller-owned mutable `u32` storage left by `amount` positions. |
| `std.sort.rotateRightU32(items, amount)` | `Void` | Rotates caller-owned mutable `u32` storage right by `amount` positions. |
| `std.sort.isSortedU32(items)` | `Bool` | Checks whether `Span<u32>` input is sorted ascending. |
| `std.sort.isSortedDescU32(items)` | `Bool` | Checks whether `Span<u32>` input is sorted descending. |
| `std.sort.partitionU32(items, pivot)` | `usize` | Moves values below `pivot` before the rest and returns the split index. |
| `std.sort.partitionDescU32(items, pivot)` | `usize` | Moves values above `pivot` before the rest and returns the split index. |
| `std.sort.isPartitionedU32(items, pivot)` | `Bool` | Checks whether all values below `pivot` are before the remaining `u32` values. |
| `std.sort.isPartitionedDescU32(items, pivot)` | `Bool` | Checks whether all values above `pivot` are before the remaining `u32` values. |
| `std.sort.dedupeSortedU32(items)` | `usize` | Compacts sorted mutable `u32` storage in place and returns the unique prefix length. |
| `std.sort.selectNthU32(items, index)` | `Bool` | Partially reorders mutable `u32` storage so `items[index]` contains the nth ascending value. |
| `std.sort.selectNthDescU32(items, index)` | `Bool` | Partially reorders mutable `u32` storage so `items[index]` contains the nth descending value. |
| `std.sort.mergeSortedU32(dst, left, right)` | `usize` | Merges ascending sorted `u32` inputs into caller storage and returns the written count. |
| `std.sort.mergeSortedDescU32(dst, left, right)` | `usize` | Merges descending sorted `u32` inputs into caller storage and returns the written count. |
| `std.sort.insertionUsize(items)` | `Void` | Sorts caller-owned mutable `usize` storage in ascending order. |
| `std.sort.insertionDescUsize(items)` | `Void` | Sorts caller-owned mutable `usize` storage in descending order. |
| `std.sort.stableUsize(items)` | `Void` | Stable ascending sort over caller-owned mutable `usize` storage. |
| `std.sort.unstableUsize(items)` | `Void` | Unstable ascending sort over caller-owned mutable `usize` storage. |
| `std.sort.stableDescUsize(items)` | `Void` | Stable descending sort over caller-owned mutable `usize` storage. |
| `std.sort.unstableDescUsize(items)` | `Void` | Unstable descending sort over caller-owned mutable `usize` storage. |
| `std.sort.reverseUsize(items)` | `Void` | Reverses caller-owned mutable `usize` storage in place. |
| `std.sort.swapUsize(items, left, right)` | `Bool` | Swaps two in-bounds `usize` elements and returns `false` for invalid indexes. |
| `std.sort.rotateLeftUsize(items, amount)` | `Void` | Rotates caller-owned mutable `usize` storage left by `amount` positions. |
| `std.sort.rotateRightUsize(items, amount)` | `Void` | Rotates caller-owned mutable `usize` storage right by `amount` positions. |
| `std.sort.isSortedUsize(items)` | `Bool` | Checks whether `Span<usize>` input is sorted ascending. |
| `std.sort.isSortedDescUsize(items)` | `Bool` | Checks whether `Span<usize>` input is sorted descending. |
| `std.sort.partitionUsize(items, pivot)` | `usize` | Moves values below `pivot` before the rest and returns the split index. |
| `std.sort.partitionDescUsize(items, pivot)` | `usize` | Moves values above `pivot` before the rest and returns the split index. |
| `std.sort.isPartitionedUsize(items, pivot)` | `Bool` | Checks whether all values below `pivot` are before the remaining `usize` values. |
| `std.sort.isPartitionedDescUsize(items, pivot)` | `Bool` | Checks whether all values above `pivot` are before the remaining `usize` values. |
| `std.sort.dedupeSortedUsize(items)` | `usize` | Compacts sorted mutable `usize` storage in place and returns the unique prefix length. |
| `std.sort.selectNthUsize(items, index)` | `Bool` | Partially reorders mutable `usize` storage so `items[index]` contains the nth ascending value. |
| `std.sort.selectNthDescUsize(items, index)` | `Bool` | Partially reorders mutable `usize` storage so `items[index]` contains the nth descending value. |
| `std.sort.mergeSortedUsize(dst, left, right)` | `usize` | Merges ascending sorted `usize` inputs into caller storage and returns the written count. |
| `std.sort.mergeSortedDescUsize(dst, left, right)` | `usize` | Merges descending sorted `usize` inputs into caller storage and returns the written count. |

The first sort surface is deliberately small and typed. Generic comparator
sorting should wait for stronger comparator contracts instead of smuggling an
untyped callback convention into the standard library.

## Example

```zero
pub fn main(world: World) -> Void raises {
    var values: [5]i32 = [5, 1, 4, 2, 3]
    std.sort.stableI32(values)
    let unique_len: usize = std.sort.dedupeSortedI32(values)
    std.sort.unstableI32(values)
    std.sort.reverseI32(values)
    let swapped: Bool = std.sort.swapI32(values, 0_usize, 4_usize)
    std.sort.rotateLeftI32(values, 2_usize)
    std.sort.rotateRightI32(values, 2_usize)
    let high_len: usize = std.sort.partitionDescI32(values, 2)
    let high_partitioned: Bool = std.sort.isPartitionedDescI32(values, 2)
    std.sort.stableDescI32(values)
    std.sort.unstableDescI32(values)
    var selected: [5]i32 = [9, 1, 4, 7, 2]
    let selected_ok: Bool = std.sort.selectNthI32(selected, 2_usize)
    let left_sorted: [2]i32 = [1, 3]
    let right_sorted: [3]i32 = [2, 4, 5]
    var merged: [5]i32 = [0, 0, 0, 0, 0]
    let merged_len: usize = std.sort.mergeSortedI32(merged, left_sorted, right_sorted)
    if std.sort.isSortedDescI32(values) && swapped && unique_len == 5 && high_len == 3 && high_partitioned && selected_ok && selected[2] == 4 && merged_len == 5 && values[0] == 5 && values[4] == 1 {
        check world.out.write("sort ok\n")
    }
}
```

Effects: writes to caller-provided mutable storage.

Allocation behavior: no allocation.

Error behavior: none.

Ownership: sort helpers are typed scalar helpers and do not move owned values.

Target support: current compiler targets.
