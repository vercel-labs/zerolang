## When To Use std.search

In Zerolang, use `std.search` for scalar span lookup and binary-search helpers
over sorted caller-owned data.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.search.indexOf(items, needle)` | `usize` | Returns the first matching index or `std.mem.len(items)` when absent. |
| `std.search.lastIndexOf(items, needle)` | `usize` | Returns the last matching index or `std.mem.len(items)` when absent. |
| `std.search.lowerBoundI32(items, needle)` | `usize` | Returns the insertion point in sorted `Span<i32>` input. |
| `std.search.upperBoundI32(items, needle)` | `usize` | Returns the insertion point after existing equal values in sorted `Span<i32>` input. |
| `std.search.binaryI32(items, needle)` | `usize` | Returns the matching sorted `Span<i32>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedI32(items, needle)` | `Bool` | Checks whether sorted `Span<i32>` input contains the needle. |
| `std.search.countSortedI32(items, needle)` | `usize` | Counts equal values in sorted `Span<i32>` input. |
| `std.search.equalRangeI32(items, needle)` | `Span<i32>` | Borrows the equal-value range in sorted `Span<i32>` input. |
| `std.search.partitionPointI32(items, pivot)` | `usize` | Returns the split index where values stop being below `pivot`. |
| `std.search.lowerBoundDescI32(items, needle)` | `usize` | Returns the insertion point before equal values in descending sorted `Span<i32>` input. |
| `std.search.upperBoundDescI32(items, needle)` | `usize` | Returns the insertion point after equal values in descending sorted `Span<i32>` input. |
| `std.search.binaryDescI32(items, needle)` | `usize` | Returns the matching descending sorted `Span<i32>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedDescI32(items, needle)` | `Bool` | Checks whether descending sorted `Span<i32>` input contains the needle. |
| `std.search.countSortedDescI32(items, needle)` | `usize` | Counts equal values in descending sorted `Span<i32>` input. |
| `std.search.equalRangeDescI32(items, needle)` | `Span<i32>` | Borrows the equal-value range in descending sorted `Span<i32>` input. |
| `std.search.partitionPointDescI32(items, pivot)` | `usize` | Returns the split index where descending values stop being above `pivot`. |
| `std.search.minI32(items)` | `Maybe<i32>` | Returns the minimum value in `Span<i32>` input or `null` for an empty span. |
| `std.search.maxI32(items)` | `Maybe<i32>` | Returns the maximum value in `Span<i32>` input or `null` for an empty span. |
| `std.search.minIndexI32(items)` | `usize` | Returns the first minimum-value index in `Span<i32>` input or `std.mem.len(items)` for an empty span. |
| `std.search.maxIndexI32(items)` | `usize` | Returns the first maximum-value index in `Span<i32>` input or `std.mem.len(items)` for an empty span. |
| `std.search.lowerBoundU32(items, needle)` | `usize` | Returns the insertion point in sorted `Span<u32>` input. |
| `std.search.upperBoundU32(items, needle)` | `usize` | Returns the insertion point after existing equal values in sorted `Span<u32>` input. |
| `std.search.binaryU32(items, needle)` | `usize` | Returns the matching sorted `Span<u32>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedU32(items, needle)` | `Bool` | Checks whether sorted `Span<u32>` input contains the needle. |
| `std.search.countSortedU32(items, needle)` | `usize` | Counts equal values in sorted `Span<u32>` input. |
| `std.search.equalRangeU32(items, needle)` | `Span<u32>` | Borrows the equal-value range in sorted `Span<u32>` input. |
| `std.search.partitionPointU32(items, pivot)` | `usize` | Returns the split index where values stop being below `pivot`. |
| `std.search.lowerBoundDescU32(items, needle)` | `usize` | Returns the insertion point before equal values in descending sorted `Span<u32>` input. |
| `std.search.upperBoundDescU32(items, needle)` | `usize` | Returns the insertion point after equal values in descending sorted `Span<u32>` input. |
| `std.search.binaryDescU32(items, needle)` | `usize` | Returns the matching descending sorted `Span<u32>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedDescU32(items, needle)` | `Bool` | Checks whether descending sorted `Span<u32>` input contains the needle. |
| `std.search.countSortedDescU32(items, needle)` | `usize` | Counts equal values in descending sorted `Span<u32>` input. |
| `std.search.equalRangeDescU32(items, needle)` | `Span<u32>` | Borrows the equal-value range in descending sorted `Span<u32>` input. |
| `std.search.partitionPointDescU32(items, pivot)` | `usize` | Returns the split index where descending values stop being above `pivot`. |
| `std.search.minU32(items)` | `Maybe<u32>` | Returns the minimum value in `Span<u32>` input or `null` for an empty span. |
| `std.search.maxU32(items)` | `Maybe<u32>` | Returns the maximum value in `Span<u32>` input or `null` for an empty span. |
| `std.search.minIndexU32(items)` | `usize` | Returns the first minimum-value index in `Span<u32>` input or `std.mem.len(items)` for an empty span. |
| `std.search.maxIndexU32(items)` | `usize` | Returns the first maximum-value index in `Span<u32>` input or `std.mem.len(items)` for an empty span. |
| `std.search.lowerBoundUsize(items, needle)` | `usize` | Returns the insertion point in sorted `Span<usize>` input. |
| `std.search.upperBoundUsize(items, needle)` | `usize` | Returns the insertion point after existing equal values in sorted `Span<usize>` input. |
| `std.search.binaryUsize(items, needle)` | `usize` | Returns the matching sorted `Span<usize>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedUsize(items, needle)` | `Bool` | Checks whether sorted `Span<usize>` input contains the needle. |
| `std.search.countSortedUsize(items, needle)` | `usize` | Counts equal values in sorted `Span<usize>` input. |
| `std.search.equalRangeUsize(items, needle)` | `Span<usize>` | Borrows the equal-value range in sorted `Span<usize>` input. |
| `std.search.partitionPointUsize(items, pivot)` | `usize` | Returns the split index where values stop being below `pivot`. |
| `std.search.lowerBoundDescUsize(items, needle)` | `usize` | Returns the insertion point before equal values in descending sorted `Span<usize>` input. |
| `std.search.upperBoundDescUsize(items, needle)` | `usize` | Returns the insertion point after equal values in descending sorted `Span<usize>` input. |
| `std.search.binaryDescUsize(items, needle)` | `usize` | Returns the matching descending sorted `Span<usize>` index or `std.mem.len(items)` when absent. |
| `std.search.containsSortedDescUsize(items, needle)` | `Bool` | Checks whether descending sorted `Span<usize>` input contains the needle. |
| `std.search.countSortedDescUsize(items, needle)` | `usize` | Counts equal values in descending sorted `Span<usize>` input. |
| `std.search.equalRangeDescUsize(items, needle)` | `Span<usize>` | Borrows the equal-value range in descending sorted `Span<usize>` input. |
| `std.search.partitionPointDescUsize(items, pivot)` | `usize` | Returns the split index where descending values stop being above `pivot`. |
| `std.search.minUsize(items)` | `Maybe<usize>` | Returns the minimum value in `Span<usize>` input or `null` for an empty span. |
| `std.search.maxUsize(items)` | `Maybe<usize>` | Returns the maximum value in `Span<usize>` input or `null` for an empty span. |
| `std.search.minIndexUsize(items)` | `usize` | Returns the first minimum-value index in `Span<usize>` input or `std.mem.len(items)` for an empty span. |
| `std.search.maxIndexUsize(items)` | `usize` | Returns the first maximum-value index in `Span<usize>` input or `std.mem.len(items)` for an empty span. |

Generic equality search supports the same non-owned scalar item types as
`std.mem.contains`.

## Example

```zero
pub fn main(world: World) -> Void raises {
    let values: [5]i32 = [1, 2, 3, 5, 8]
    let found: usize = std.search.binaryI32(values, 5)
    let present: Bool = std.search.containsSortedI32(values, 5)
    let after_five: usize = std.search.upperBoundI32(values, 5)
    let fives: usize = std.search.countSortedI32(values, 5)
    let five_range: Span<i32> = std.search.equalRangeI32(values, 5)
    let partition_point: usize = std.search.partitionPointI32(values, 5)
    let missing: usize = std.search.indexOf(values, 13)
    let minimum: Maybe<i32> = std.search.minI32(values)
    let maximum: Maybe<i32> = std.search.maxI32(values)
    let max_index: usize = std.search.maxIndexI32(values)
    let descending: [5]i32 = [9, 5, 5, 3, 1]
    let descending_found: usize = std.search.binaryDescI32(descending, 5)
    let descending_count: usize = std.search.countSortedDescI32(descending, 5)
    if found == 3 && present && after_five == 4 && fives == 1 && std.mem.len(five_range) == 1 && partition_point == 3 && missing == std.mem.len(values) && minimum.has && minimum.value == 1 && maximum.has && maximum.value == 8 && max_index == 4 && descending_found == 1 && descending_count == 2 {
        check world.out.write("search ok\n")
    }
}
```

Effects: none.

Allocation behavior: no allocation.

Error behavior: absent values return the input length; sorted-count helpers
return `0`; min/max helpers return `null` for empty input; min/max index
helpers return the input length for empty input.

Ownership: generic equality search rejects owned item elements.

Target support: current compiler targets.
