## When To Use std.mem

In Zerolang, use `std.mem` for spans, clamped span views, copy/fill,
fixed-buffer allocators, explicit byte buffers, and memory-budget-visible
collection foundations.

Runnable today:

The item-generic helpers currently support these direct-backed scalar element
types: `Bool`, `u8`, `u16`, `usize`, `i32`, `u32`, `i64`, and `u64`.

| API | Return | Notes |
| --- | --- | --- |
| `std.mem.copy(dst, src)` | `usize` | Copies from `Span<u8>` into caller-owned `MutSpan<u8>` storage and returns the copied byte count. |
| `std.mem.copyItems(dst, src)` | `usize` | Copies matching scalar `Span<T>` items into caller-owned mutable item storage and returns the copied item count. |
| `std.mem.fill(dst, value)` | `usize` | Fills caller-owned `MutSpan<u8>` storage with a `u8` byte and returns the filled byte count. |
| `std.mem.fillItems(dst, value)` | `usize` | Fills caller-owned mutable scalar item storage with a matching `T` value and returns the filled item count. |
| `std.mem.eql(a, b)` | `Bool` | Compares string-backed byte inputs. |
| `std.mem.span(value)` | `Span<u8>` | Builds a native `Span<u8>` view over a string literal. |
| `std.mem.contains(items, needle)` | `Bool` | Searches readable contiguous non-owned scalar `T` storage for a matching value. |
| `std.mem.compareI32(left, right)` | `i32` | Lexicographically compares two `Span<i32>` values and returns `-1`, `0`, or `1`. |
| `std.mem.compareU8(left, right)` | `i32` | Lexicographically compares two `Span<u8>` values and returns `-1`, `0`, or `1`. |
| `std.mem.compareBytes(left, right)` | `i32` | Alias-style byte lexicographic comparison for `Span<u8>` values. |
| `std.mem.compareU32(left, right)` | `i32` | Lexicographically compares two `Span<u32>` values and returns `-1`, `0`, or `1`. |
| `std.mem.compareUsize(left, right)` | `i32` | Lexicographically compares two `Span<usize>` values and returns `-1`, `0`, or `1`. |
| `std.mem.startsWith(items, prefix)` | `Bool` | Checks whether a scalar span begins with a matching prefix span. |
| `std.mem.endsWith(items, suffix)` | `Bool` | Checks whether a scalar span ends with a matching suffix span. |
| `std.mem.splitBefore(items, delimiter)` | `Span<T>` | Returns the read-only scalar item prefix before the first delimiter, or the full span when absent. |
| `std.mem.splitAfter(items, delimiter)` | `Span<T>` | Returns the read-only scalar item suffix after the first delimiter, or an empty span when absent. |
| `std.mem.isEmpty(items)` | `Bool` | Reports whether readable contiguous scalar item storage has zero items. |
| `std.mem.chunkCount(items, chunkSize)` | `usize` | Returns the number of non-overlapping chunks needed to cover the span, or `0` when `chunkSize` is zero. |
| `std.mem.chunk(items, chunkIndex, chunkSize)` | `Span<T>` | Returns a clamped read-only scalar item chunk by zero-based chunk index. |
| `std.mem.windowCount(items, windowSize)` | `usize` | Returns the number of overlapping fixed-size windows available in a span, or `0` when the size is zero or larger than the span. |
| `std.mem.window(items, windowIndex, windowSize)` | `Span<T>` | Returns an overlapping read-only scalar item window by zero-based window index. |
| `std.mem.advance(items, cursor, count)` | `usize` | Returns a clamped cursor advanced by at most `count` items. |
| `std.mem.cursorDone(items, cursor)` | `Bool` | Reports whether a cursor is at or past the end of a span. |
| `std.mem.remaining(items, cursor)` | `Span<T>` | Returns the clamped read-only scalar item view from `cursor` to the end. |
| `std.mem.cursorChunk(items, cursor, count)` | `Span<T>` | Returns a clamped read-only scalar item window beginning at `cursor`. |
| `std.mem.prefix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item prefix view. |
| `std.mem.dropPrefix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item view after the first `count` items. |
| `std.mem.suffix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item suffix view. |
| `std.mem.dropSuffix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item view before the last `count` items. |
| `std.mem.slice(items, start, count)` | `Span<T>` | Returns a clamped read-only scalar item window beginning at `start`. |
| `std.mem.len(bytes)` | `usize` | Returns the length of a fixed array, `Span<T>`, or `MutSpan<T>`. |
| `std.mem.get(bytes, index)` | `Maybe<T>` | Reads one indexed element from an array/span-like value when the index is in bounds. |
| `std.mem.eqlBytes(a, b)` | `Bool` | Compares two `Span<T>`/`MutSpan<T>` values with the same element type. |
| `std.mem.nullAlloc()` | `NullAlloc` | Creates an allocator that always returns `null`, useful for proving code does not allocate. |
| `std.mem.fixedBufAlloc(buffer)` | `FixedBufAlloc` | Creates a mutable fixed-buffer allocator from caller-owned `MutSpan<u8>` bytes. |
| `std.mem.arena(buffer)` | `FixedBufAlloc` | Arena-style alias over the fixed-buffer allocator model; `reset` rewinds the caller-owned storage. |
| `std.mem.pageAlloc()` | `PageAlloc` | Explicit host allocator handle metadata; it never creates an ambient global allocator. |
| `std.mem.generalAlloc()` | `GeneralAlloc` | Explicit general allocator handle metadata; callers still pass allocator state deliberately. |
| `std.mem.allocBytes(alloc, len)` | `Maybe<MutSpan<u8>>` | Allocates bytes from `NullAlloc` or a mutable `FixedBufAlloc` binding. |
| `std.mem.byteBuf(alloc, len)` | `Maybe<owned<ByteBuf>>` | Creates an owned byte buffer backed by explicit caller-provided allocator storage. |
| `std.mem.bufBytes(&buf)` | `MutSpan<u8>` | Borrows writable bytes from an owned `ByteBuf`. |
| `std.mem.bufLen(&buf)` | `usize` | Returns the live length of a `ByteBuf`. |
| `std.mem.reset(&mut arena)` | `Void` | Resets caller-owned arena/fixed-buffer allocation state. |
| `std.mem.capacity(arena)` | `usize` | Reports fixed-buffer capacity. |
| `std.mem.vec(storage)` | `Vec` | Monomorphic byte vector over caller-owned mutable storage. |
| `std.mem.vecPush(&mut vec, value)` | `Bool` | Appends one byte when capacity remains; returns `false` instead of growing implicitly. |
| `std.mem.vecBytes(&vec)` | `Span<u8>` | Borrows the live bytes in the vector without copying. |
| `std.mem.vecGet(&vec, index)` | `Maybe<u8>` | Reads one live vector byte when `index` is in bounds. |
| `std.mem.vecSet(&mut vec, index, value)` | `Bool` | Replaces one live vector byte when `index` is in bounds; returns `false` out of bounds. |
| `std.mem.vecClear(&mut vec)` | `usize` | Resets live length to zero and keeps caller-owned storage available for reuse. |
| `std.mem.vecPop(&mut vec)` | `Bool` | Removes one live byte when the vector is non-empty. |
| `std.mem.vecTruncate(&mut vec, len)` | `usize` | Shrinks the live length to at most `len` and returns the resulting length. |
| `std.mem.vecRemoveSwap(&mut vec, index)` | `Bool` | Removes one live byte by replacing it with the last live byte. Returns `false` out of bounds. |
| `std.mem.vecIndex(&vec, value)` | `usize` | Returns the first live index for `value`, or the current live length when absent. |
| `std.mem.vecContains(&vec, value)` | `Bool` | Reports whether a byte value is present in the live vector prefix. |
| `std.mem.vecInsertUnique(&mut vec, value)` | `Bool` | Appends `value` only when it is absent and capacity remains. |
| `std.mem.vecRemoveValue(&mut vec, value)` | `Bool` | Swap-removes the first matching live byte by value. |
| `std.mem.vecLen(&vec)` | `usize` | Reports current vector length. |
| `std.mem.vecCapacity(&vec)` | `usize` | Reports caller-provided vector capacity. |
| `std.mem.vecRemaining(&vec)` | `usize` | Reports remaining byte-vector capacity. Returns `0` when the vector is full. |
| `std.mem.vecIsEmpty(&vec)` | `Bool` | Reports whether the vector has no live bytes. |
| `std.mem.vecIsFull(&vec)` | `Bool` | Reports whether the vector has no remaining capacity. |

## Example

```zero
type SliceView {
    bytes: Span<u8>,
    values: Span<i32>,
}

pub fn main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero-memory")
    let same: Span<u8> = std.mem.span("zero-memory")
    var scratch: [11]u8 = [0_u8; 11]
    let copied: usize = std.mem.copy(scratch, bytes)
    var ints: [3]i32 = [1, 2, 3]
    let intSpan: MutSpan<i32> = ints
    let filled: usize = std.mem.fillItems(intSpan, 7)
    let prefix: Span<i32> = std.mem.prefix(intSpan, 2)
    let suffix: Span<i32> = std.mem.suffix(intSpan, 2)
    let middle: Span<i32> = std.mem.slice(intSpan, 1, 1)
    let before: Span<i32> = std.mem.splitBefore(intSpan, 7)
    let after: Span<i32> = std.mem.splitAfter(intSpan, 7)
    let chunk: Span<i32> = std.mem.chunk(intSpan, 1_usize, 2_usize)
    let sliding: Span<i32> = std.mem.window(intSpan, 1_usize, 2_usize)
    let cursor: usize = std.mem.advance(intSpan, 0_usize, 2_usize)
    let rest: Span<i32> = std.mem.remaining(intSpan, cursor)
    let view: SliceView = SliceView { bytes: bytes, values: intSpan }
    let ordered: Bool = std.mem.compareI32(prefix, suffix) == 0
    let starts: Bool = std.mem.startsWith(view.bytes, std.mem.span("zero"))
    let ends: Bool = std.mem.endsWith(view.bytes, std.mem.span("memory"))
    if copied == 11 && filled == 3 && ordered && starts && ends && std.mem.len(view.bytes) == 11 && std.mem.eqlBytes(view.bytes, same) && std.mem.len(view.values) == 3 && std.mem.contains(view.values, 7) && std.mem.isEmpty(before) && std.mem.len(after) == 2 && std.mem.len(prefix) == 2 && std.mem.len(suffix) == 2 && std.mem.len(middle) == 1 && std.mem.len(chunk) == 1 && std.mem.len(sliding) == 2 && std.mem.len(rest) == 1 && !std.mem.cursorDone(intSpan, cursor) {
        check world.out.write("memory type forms runnable\n")
    }
}
```

## Allocator Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    var alloc: FixedBufAlloc = std.mem.fixedBufAlloc(storage)
    let bytes: Maybe<MutSpan<u8>> = std.mem.allocBytes(alloc, 4)
    if bytes.has {
        bytes.value[0] = 90
        check world.out.write("fixed buffer allocated\n")
    }
}
```

Effects: none beyond writes performed by caller code.

Allocation behavior:

- `NullAlloc` always returns `null`.
- `FixedBufAlloc` and `Arena` return `MutSpan<u8>` views into caller-owned
  storage.
- `ByteBuf` owns a slice of explicit allocator storage and never reaches for a
  global heap.

Use `std.mem.allocBytes(alloc, capacity)` when a byte-oriented API needs
allocator-backed mutable storage directly. `std.collections.fixedSet`,
`std.collections.fixedDeque`, and `std.collections.fixedMap` can use those
returned spans as caller-owned backing storage.

Ownership: returned spans borrow from the original fixed buffer; no heap
ownership is created.

Target support: current compiler targets. Direct native builds lower
`FixedBufAlloc` locals only; `PageAlloc`, `GeneralAlloc`, and `NullAlloc`
locals type-check but fail `zero build` with a `BLD004` diagnostic that points
back to `std.mem.fixedBufAlloc`.

## Reporting Contract

`zero mem --json [graph-input]` reports the allocator contract in machine-readable form:

- `memoryBudgets`: stack, static, heap, arena, fixed-buffer, collection-capacity, allocator-capacity, requested-allocation, and linear-memory floor budgets.
- `allocatorFacts`: `NullAlloc`, `FixedBufAlloc`, `Arena`, `PageAlloc`, and
  `GeneralAlloc` usage, capacity, failure behavior, and
  hidden-global-allocator status.
- `allocationInstrumentation`: pay-as-used hooks for attempts, successes, failures, requested bytes, granted bytes, and peak live bytes.
- `collectionFacts`: fixed-capacity `Vec`, fixed-storage `std.collections`
  helpers, and owned `ByteBuf`, including growth/failure/cleanup behavior.
- `safetyFacts`: the selected profile plus the current bounds,
  initialization, aliasing, lifetime, ownership, span, MIR, literal integer
  range-check, runtime arithmetic, and unchecked-surface facts.

All heap budgets are explicit. A program that only uses `std.mem` remains at
`heapBytes: 0`, `globalHeapBytes: 0`, and `hiddenHeapAllocation: false` unless
an allocator API documents otherwise.

## Design Notes

No standard collection may silently allocate from a global heap. Heap-owning APIs
will require an allocator capability and document ownership, capacity, and
cleanup.
