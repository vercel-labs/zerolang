## When To Use std.mem

In Zerolang, use `std.mem` for spans, copy/fill, fixed-buffer allocators, explicit
byte buffers, and memory-budget-visible collection foundations.

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
| `std.mem.isEmpty(items)` | `Bool` | Reports whether readable contiguous scalar item storage has zero items. |
| `std.mem.prefix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item prefix view. |
| `std.mem.dropPrefix(items, count)` | `Span<T>` | Returns a clamped read-only scalar item view after the first `count` items. |
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
    let view: SliceView = SliceView { bytes: bytes, values: intSpan }
    if copied == 11 && filled == 3 && std.mem.len(view.bytes) == 11 && std.mem.eqlBytes(view.bytes, same) && std.mem.len(view.values) == 3 && std.mem.contains(view.values, 7) && std.mem.len(prefix) == 2 {
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
