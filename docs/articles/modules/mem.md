## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.mem.copy(dst, src)` | `usize` | Copies from `Span<u8>` into caller-owned `MutSpan<u8>` storage and returns the copied byte count. |
| `std.mem.fill(dst, value)` | `usize` | Fills caller-owned `MutSpan<u8>` storage with a `u8` byte and returns the filled byte count. |
| `std.mem.eql(a, b)` | `Bool` | Compares string-backed byte inputs. |
| `std.mem.span(value)` | `Span<u8>` | Builds a native `Span<u8>` view over a string literal. |
| `std.mem.len(bytes)` | `usize` | Returns the length of a fixed array, `Span<T>`, or `MutSpan<T>`. |
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
| `std.mem.vecLen(&vec)` | `usize` | Reports current vector length. |
| `std.mem.vecCapacity(&vec)` | `usize` | Reports caller-provided vector capacity. |
| `std.mem.mapEmpty()` / `std.mem.setEmpty()` | `Map` / `Set` | Empty fixed metadata values with no allocation. |
| `std.mem.mapLen(&map)` / `std.mem.setLen(&set)` | `usize` | Reports `0` for the current empty metadata values. |

## Example

```zero
shape SliceView {
    bytes: Span<u8>,
    values: Span<i32>,
}

pub fun main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero-memory")
    let same = std.mem.span("zero-memory")
    let mut scratch: [11]u8 = [0_u8; 11]
    let copied = std.mem.copy(scratch, bytes)
    let mut ints: [3]i32 = [1, 2, 3]
    let intSpan: MutSpan<i32> = ints
    intSpan[1] = 20
    let view = SliceView { bytes: bytes, values: intSpan }
    if copied == 11 && std.mem.len(view.bytes) == 11 && std.mem.eqlBytes(view.bytes, same) &&
        std.mem.len(view.values) == 3 && std.mem.eqlBytes(view.values, intSpan) {
        check world.out.write("memory type forms runnable\n")
    }
}
```

## Allocator Example

```zero
pub fun main(world: World) -> Void raises {
    let mut storage: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    let mut alloc: FixedBufAlloc = std.mem.fixedBufAlloc(storage)
    let bytes = std.mem.allocBytes(alloc, 4)

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

Target support: current compiler targets.

## Reporting Contract

`zero mem --json <input>` reports the allocator contract in machine-readable form:

- `memoryBudgets`: stack, static, heap, arena, fixed-buffer, collection-capacity, allocator-capacity, requested-allocation, and linear-memory floor budgets.
- `allocatorFacts`: `NullAlloc`, `FixedBufAlloc`, `Arena`, `PageAlloc`, and
  `GeneralAlloc` usage, capacity, failure behavior, and
  hidden-global-allocator status.
- `allocationInstrumentation`: pay-as-used hooks for attempts, successes, failures, requested bytes, granted bytes, and peak live bytes.
- `collectionFacts`: fixed-capacity `Vec`, owned `ByteBuf`, and empty `Map`/`Set` metadata, including growth/failure/cleanup behavior.

All heap budgets are explicit. A program that only uses `std.mem` remains at
`heapBytes: 0`, `globalHeapBytes: 0`, and `hiddenHeapAllocation: false` unless
an allocator API documents otherwise.

## Design Notes

No standard collection may silently allocate from a global heap. Heap-owning APIs
will require an allocator capability and document ownership, capacity, and
cleanup.
