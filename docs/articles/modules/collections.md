## When To Use std.collections

In Zerolang, use `std.collections` for fixed-capacity collection operations where the caller
owns the storage and growth must be explicit.

Runnable today:

The collection helpers operate over caller-owned fixed arrays or `MutSpan<T>`
storage plus an explicit live length. They do not allocate, grow, or retain
hidden state. Generic helpers currently support the same non-owned scalar item
types as the generic `std.mem` item helpers: `Bool`, `u8`, `u16`, `usize`,
`i32`, `u32`, `i64`, and `u64`.

Use `FixedSet<T>`, `FixedDeque<T>`, `FixedRingBuffer<T>`, or
`FixedMap<K, V>` when it is clearer to carry storage and live length as one
value. These resource values still borrow caller-owned mutable storage;
inserting, removing, clearing, or truncating values never allocates.

For byte collections, storage can come from a fixed array or from an explicit
allocator. Use `std.mem.allocBytes(alloc, capacity)` to request a
`MutSpan<u8>`, then pass that mutable span to `FixedSet<u8>`,
`FixedDeque<u8>`, or `FixedMap<u8, u8>`. The wrapper does not own the storage;
the allocator-backed span must remain live for as long as the collection value
is used.

| API | Return | Notes |
| --- | --- | --- |
| `std.collections.push(items, len, value)` | `usize` | Writes `value` at `len` when capacity remains and returns the next length. Returns the unchanged length on overflow. |
| `std.collections.append(items, len, values)` | `usize` | Copies all non-overlapping `values` into `items` at `len` when the full append fits. Returns the unchanged length on overflow or invalid length. |
| `std.collections.clear(items, len)` | `usize` | Returns `0` as the next live length. Storage contents are left unchanged. |
| `std.collections.truncate(items, len, newLen)` | `usize` | Returns the smaller of the current live length and `newLen`, after clamping invalid `len` to storage capacity. Storage contents are left unchanged. |
| `std.collections.first(items, len)` | `Maybe<T>` | Returns the first live item, or `null` when the live prefix is empty. |
| `std.collections.last(items, len)` | `Maybe<T>` | Returns the last live item, or `null` when the live prefix is empty. |
| `std.collections.pop(items, len)` | `usize` | Returns the next live length after removing the last item. Returns `0` when empty. Storage contents are left unchanged. |
| `std.collections.dequePushBack(items, len, value)` | `usize` | Writes `value` at the back when capacity remains and returns the next length. |
| `std.collections.dequePushFront(items, len, value)` | `usize` | Inserts `value` at the front by shifting the live prefix right. Returns the unchanged length when full or invalid. |
| `std.collections.dequeFront(items, len)` | `Maybe<T>` | Returns the first live deque item, or `null` when empty. |
| `std.collections.dequeBack(items, len)` | `Maybe<T>` | Returns the last live deque item, or `null` when empty. |
| `std.collections.dequePopBack(items, len)` | `usize` | Returns the next live length after removing the back item. Storage contents are left unchanged. |
| `std.collections.dequePopFront(items, len)` | `usize` | Removes the front item by shifting the live suffix left and returns the next length. Returns the unchanged length when empty or invalid. |
| `std.collections.fixedDeque(items, len)` | `FixedDeque<T>` | Builds a fixed-capacity deque value over caller-owned mutable storage and clamps the initial live length to capacity. |
| `std.collections.fixedDequeBack(deque)` | `Maybe<T>` | Returns the last live deque item, or `null` when empty. |
| `std.collections.fixedDequeClear(deque)` | `usize` | Clears a `FixedDeque<T>` and returns `0` as the next live length. |
| `std.collections.fixedDequeFront(deque)` | `Maybe<T>` | Returns the first live deque item, or `null` when empty. |
| `std.collections.fixedDequeIsFull(deque)` | `Bool` | Reports whether a `FixedDeque<T>` has no remaining capacity. |
| `std.collections.fixedDequeLen(deque)` | `usize` | Returns the current live length of a `FixedDeque<T>`. |
| `std.collections.fixedDequePopBack(deque)` | `Maybe<T>` | Removes and returns the back item when present. |
| `std.collections.fixedDequePopFront(deque)` | `Maybe<T>` | Removes and returns the front item when present, shifting the live suffix left. |
| `std.collections.fixedDequePushBack(deque, value)` | `Bool` | Appends `value` at the back when capacity remains. Returns whether the live deque changed. |
| `std.collections.fixedDequePushFront(deque, value)` | `Bool` | Inserts `value` at the front when capacity remains. Returns whether the live deque changed. |
| `std.collections.fixedDequeRemaining(deque)` | `usize` | Returns remaining storage capacity for a `FixedDeque<T>`. |
| `std.collections.fixedDequeTruncate(deque, newLen)` | `usize` | Updates a `FixedDeque<T>` live prefix to the smaller of the current live length and `newLen`, clamped to storage. |
| `std.collections.fixedDequeView(deque)` | `Span<T>` | Returns a read-only prefix view over live `FixedDeque<T>` items. |
| `std.collections.fixedRingBuffer(items, head, len)` | `FixedRingBuffer<T>` | Builds a fixed-capacity ring buffer over caller-owned mutable storage, normalizing `head` and clamping `len` to capacity. |
| `std.collections.fixedRingBufferBack(ring)` | `Maybe<T>` | Returns the last live logical item, or `null` when empty. |
| `std.collections.fixedRingBufferCapacity(ring)` | `usize` | Returns the storage capacity. |
| `std.collections.fixedRingBufferClear(ring)` | `usize` | Clears a `FixedRingBuffer<T>`, resets its head to `0`, and returns `0` as the next live length. |
| `std.collections.fixedRingBufferFront(ring)` | `Maybe<T>` | Returns the first live logical item, or `null` when empty. |
| `std.collections.fixedRingBufferGet(ring, index)` | `Maybe<T>` | Reads a logical index through wrap-around storage, or returns `null` outside the live length. |
| `std.collections.fixedRingBufferIsFull(ring)` | `Bool` | Reports whether a `FixedRingBuffer<T>` has no remaining capacity. |
| `std.collections.fixedRingBufferLen(ring)` | `usize` | Returns the current live length. |
| `std.collections.fixedRingBufferPopBack(ring)` | `Maybe<T>` | Removes and returns the back logical item when present. |
| `std.collections.fixedRingBufferPopFront(ring)` | `Maybe<T>` | Removes and returns the front logical item when present, advancing the stored head. |
| `std.collections.fixedRingBufferPushBack(ring, value)` | `Bool` | Appends `value` at the logical back when capacity remains. |
| `std.collections.fixedRingBufferPushFront(ring, value)` | `Bool` | Inserts `value` at the logical front when capacity remains. |
| `std.collections.fixedRingBufferRemaining(ring)` | `usize` | Returns remaining storage capacity. |
| `std.collections.fixedRingBufferTruncate(ring, newLen)` | `usize` | Updates the live logical prefix to the smaller of the current live length and `newLen`, clamped to storage. |
| `std.collections.fill(items, len, value)` | `Bool` | Writes `value` across the live prefix. Returns `false` for invalid length. |
| `std.collections.insertAt(items, len, index, value)` | `usize` | Inserts `value` at `index` by shifting the live suffix right. Returns the unchanged length when full or invalid. |
| `std.collections.replaceAt(items, len, index, value)` | `Bool` | Replaces the live item at `index`. Returns `false` for invalid length or index. |
| `std.collections.swapAt(items, len, left, right)` | `Bool` | Swaps two live items. Returns `false` for invalid length or index. |
| `std.collections.insertUnique(items, len, value)` | `usize` | Treats the live prefix as a fixed-capacity set. Appends `value` only when absent and capacity remains. |
| `std.collections.setClear(items, len)` | `usize` | Returns `0` as the next live set length. Storage contents are left unchanged. |
| `std.collections.setContains(items, len, value)` | `Bool` | Reports whether the live prefix contains `value` as a fixed-capacity set. |
| `std.collections.setInsert(items, len, value)` | `usize` | Appends `value` only when absent and capacity remains. |
| `std.collections.setRemaining(items, len)` | `usize` | Returns remaining fixed-capacity set storage. Returns `0` when `len` is at or past capacity. |
| `std.collections.setIsFull(items, len)` | `Bool` | Reports whether the fixed-capacity set has no remaining storage. |
| `std.collections.setRemove(items, len, value)` | `usize` | Removes the first matching set value with swap-remove and returns the next length. |
| `std.collections.setTruncate(items, len, newLen)` | `usize` | Returns the smaller of the current live set length and `newLen`, after clamping invalid `len` to storage capacity. |
| `std.collections.setView(items, len)` | `Span<T>` | Returns a clamped read-only prefix view over the live fixed-capacity set items. |
| `std.collections.fixedSet(items, len)` | `FixedSet<T>` | Builds a fixed-capacity set value over caller-owned mutable storage and clamps the initial live length to capacity. |
| `std.collections.fixedSetClear(set)` | `usize` | Clears a `FixedSet<T>` and returns `0` as the next live length. |
| `std.collections.fixedSetContains(set, value)` | `Bool` | Reports whether a `FixedSet<T>` contains `value` in its live prefix. |
| `std.collections.fixedSetInsert(set, value)` | `Bool` | Inserts `value` when absent and capacity remains. Returns whether the live set changed. |
| `std.collections.fixedSetIsFull(set)` | `Bool` | Reports whether a `FixedSet<T>` has no remaining capacity. |
| `std.collections.fixedSetLen(set)` | `usize` | Returns the current live length of a `FixedSet<T>`. |
| `std.collections.fixedSetRemaining(set)` | `usize` | Returns remaining storage capacity for a `FixedSet<T>`. |
| `std.collections.fixedSetRemove(set, value)` | `Bool` | Removes `value` with swap-remove when present. Returns whether the live set changed. |
| `std.collections.fixedSetTruncate(set, newLen)` | `usize` | Updates a `FixedSet<T>` live prefix to the smaller of the current live length and `newLen`, clamped to storage. |
| `std.collections.fixedSetView(set)` | `Span<T>` | Returns a read-only prefix view over live `FixedSet<T>` items. |
| `std.collections.fixedMap(keys, values, len)` | `FixedMap<K, V>` | Builds a fixed-capacity map value over caller-owned mutable key/value storage and clamps the initial live length to the shorter storage. |
| `std.collections.fixedMapClear(map)` | `usize` | Clears a `FixedMap<K, V>` and returns `0` as the next live length. |
| `std.collections.fixedMapContains(map, key)` | `Bool` | Reports whether a `FixedMap<K, V>` contains `key` in its live key prefix. |
| `std.collections.fixedMapGet(map, key)` | `Maybe<V>` | Returns the value for `key` when present. |
| `std.collections.fixedMapIndex(map, key)` | `usize` | Searches live keys and returns the matching index, or the live length when absent. |
| `std.collections.fixedMapIsFull(map)` | `Bool` | Reports whether a `FixedMap<K, V>` has no remaining capacity in the shorter storage. |
| `std.collections.fixedMapKeys(map)` | `Span<K>` | Returns a read-only prefix view over live `FixedMap<K, V>` keys. |
| `std.collections.fixedMapLen(map)` | `usize` | Returns the current live length of a `FixedMap<K, V>`. |
| `std.collections.fixedMapPut(map, key, value)` | `Bool` | Updates an existing key's value or appends a key/value pair when capacity remains. Returns whether the key exists afterward. |
| `std.collections.fixedMapRemaining(map)` | `usize` | Returns remaining storage capacity in the shorter key/value storage. |
| `std.collections.fixedMapRemove(map, key)` | `Bool` | Removes a key/value pair with swap-remove when present. Returns whether the live map changed. |
| `std.collections.fixedMapTruncate(map, newLen)` | `usize` | Updates the live prefix to the smaller of the current live length and `newLen`, clamped to storage. |
| `std.collections.fixedMapValues(map)` | `Span<V>` | Returns a read-only prefix view over live `FixedMap<K, V>` values. |
| `std.collections.mapClear(keys, values, len)` | `usize` | Returns `0` as the next live map length. Key/value storage contents are left unchanged. |
| `std.collections.mapContains(keys, len, key)` | `Bool` | Reports whether the live key prefix contains `key`. |
| `std.collections.mapIndex(keys, len, key)` | `usize` | Searches the live key prefix and returns the matching index. Returns the live length when absent. |
| `std.collections.mapIsFull(keys, values, len)` | `Bool` | Reports whether the shorter of key/value storage has no remaining capacity. |
| `std.collections.mapKeys(keys, len)` | `Span<K>` | Returns a clamped read-only prefix view over the live map keys. |
| `std.collections.mapGet(keys, values, len, key)` | `Maybe<V>` | Treats parallel key/value storage as a fixed-capacity map and returns the value for `key` when present. |
| `std.collections.mapPut(keys, values, len, key, value)` | `usize` | Updates an existing key's value or appends a key/value pair when capacity remains. Returns the unchanged length on overflow or invalid length. |
| `std.collections.mapRemaining(keys, values, len)` | `usize` | Returns remaining capacity in the shorter of key/value storage. |
| `std.collections.mapRemove(keys, values, len, key)` | `usize` | Removes the first matching key/value pair with swap-remove and returns the next length. Returns the unchanged length when absent or invalid. |
| `std.collections.mapTruncate(keys, values, len, newLen)` | `usize` | Returns the smaller of the live map length and `newLen`, clamped to key and value storage. |
| `std.collections.mapValues(keys, values, len)` | `Span<V>` | Returns a clamped read-only prefix view over live map values, bounded by both key and value storage. |
| `std.collections.view(items, len)` | `Span<T>` | Returns a clamped read-only prefix view over the live collection items. |
| `std.collections.remaining(items, len)` | `usize` | Returns remaining capacity after the live prefix. Returns `0` when `len` is at or past capacity. |
| `std.collections.isFull(items, len)` | `Bool` | Reports whether no capacity remains. Invalid lengths at or past capacity count as full. |
| `std.collections.contains(items, len, needle)` | `Bool` | Searches only the live prefix for `needle`. |
| `std.collections.count(items, len, needle)` | `usize` | Counts matching values in the live prefix. |
| `std.collections.removeAt(items, len, index)` | `usize` | Removes `index` by shifting the live suffix left and returns the next length. Returns the unchanged length for invalid length or index. |
| `std.collections.removeValue(items, len, value)` | `usize` | Removes the first matching live value with swap-remove and returns the next length. Returns the unchanged length when absent. |
| `std.collections.removeSwap(items, len, index)` | `usize` | Replaces `index` with the last live item and returns the next length. Returns the unchanged length for invalid length or index. |
| `std.collections.reverse(items, len)` | `Bool` | Reverses the live prefix in place. Returns `false` for invalid length. |
| `std.collections.rotateLeft(items, len, count)` | `Bool` | Rotates the live prefix left by `count`. Returns `false` for invalid length. |
| `std.collections.rotateRight(items, len, count)` | `Bool` | Rotates the live prefix right by `count`. Returns `false` for invalid length. |
| `std.collections.moveToFront(items, len, index)` | `usize` | Moves the item at `index` to the front by shifting the live prefix. Returns the unchanged length for invalid length or index. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var values: [5]i32 = [0, 0, 0, 0, 0]
    let extra: [2]i32 = [4, 1]
    var len: usize = 0
    len = std.collections.push(values, len, 3)
    len = std.collections.push(values, len, 1)
    len = std.collections.setInsert(values, len, 3)
    len = std.collections.setInsert(values, len, 2)
    let has_three: Bool = std.collections.setContains(values, len, 3)
    let set_live: Span<i32> = std.collections.setView(values, len)
    let set_remaining: usize = std.collections.setRemaining(values, len)
    len = std.collections.setRemove(values, len, 2)
    let set_truncated: usize = std.collections.setTruncate(values, len, 1)
    var fixed_storage: [4]i32 = [1, 2, 0, 0]
    var fixed_set: FixedSet<i32> = std.collections.fixedSet(fixed_storage, 2_usize)
    let fixed_inserted: Bool = std.collections.fixedSetInsert(&mut fixed_set, 3)
    let fixed_removed: Bool = std.collections.fixedSetRemove(&mut fixed_set, 1)
    let fixed_live: Span<i32> = std.collections.fixedSetView(&fixed_set)
    let fixed_remaining: usize = std.collections.fixedSetRemaining(&fixed_set)
    let fixed_len: usize = std.collections.fixedSetLen(&fixed_set)
    let fixed_truncated: usize = std.collections.fixedSetTruncate(&mut fixed_set, 1_usize)
    var fixed_keys: [3]u8 = [1_u8, 2_u8, 0_u8]
    var fixed_scores: [3]u16 = [10_u16, 20_u16, 0_u16]
    var fixed_map: FixedMap<u8, u16> = std.collections.fixedMap(fixed_keys, fixed_scores, 2_usize)
    let fixed_map_added: Bool = std.collections.fixedMapPut(&mut fixed_map, 3_u8, 30_u16)
    let fixed_map_updated: Bool = std.collections.fixedMapPut(&mut fixed_map, 2_u8, 25_u16)
    let fixed_map_score: Maybe<u16> = std.collections.fixedMapGet(&fixed_map, 2_u8)
    let fixed_map_keys: Span<u8> = std.collections.fixedMapKeys(&fixed_map)
    let fixed_map_values: Span<u16> = std.collections.fixedMapValues(&fixed_map)
    let fixed_map_index: usize = std.collections.fixedMapIndex(&fixed_map, 2_u8)
    let fixed_map_full: Bool = std.collections.fixedMapIsFull(&fixed_map)
    let fixed_map_removed: Bool = std.collections.fixedMapRemove(&mut fixed_map, 1_u8)
    let inserted_len: usize = std.collections.insertAt(values, len, 1_usize, 2)
    let replaced: Bool = std.collections.replaceAt(values, inserted_len, 1_usize, 6)
    let swapped: Bool = std.collections.swapAt(values, inserted_len, 0_usize, 1_usize)
    len = std.collections.removeAt(values, inserted_len, 1_usize)
    let first: Maybe<i32> = std.collections.first(values, len)
    let last: Maybe<i32> = std.collections.last(values, len)
    let popped_len: usize = std.collections.pop(values, len)
    len = std.collections.truncate(values, len, 2)
    len = std.collections.append(values, len, extra)
    let live: Span<i32> = std.collections.view(values, len)
    var deque_values: [4]i32 = [0, 0, 0, 0]
    var deque_len: usize = 0
    deque_len = std.collections.dequePushBack(deque_values, deque_len, 2)
    deque_len = std.collections.dequePushFront(deque_values, deque_len, 1)
    deque_len = std.collections.dequePushBack(deque_values, deque_len, 3)
    let deque_front: Maybe<i32> = std.collections.dequeFront(deque_values, deque_len)
    let deque_back: Maybe<i32> = std.collections.dequeBack(deque_values, deque_len)
    let deque_after_pop_back: usize = std.collections.dequePopBack(deque_values, deque_len)
    deque_len = std.collections.dequePopFront(deque_values, deque_after_pop_back)
    var fixed_deque_values: [4]i32 = [0, 0, 0, 0]
    var fixed_deque: FixedDeque<i32> = std.collections.fixedDeque(fixed_deque_values, 0_usize)
    let fixed_deque_pushed: Bool = std.collections.fixedDequePushBack(&mut fixed_deque, 2)
    let fixed_deque_front_pushed: Bool = std.collections.fixedDequePushFront(&mut fixed_deque, 1)
    let fixed_deque_back: Maybe<i32> = std.collections.fixedDequeBack(&fixed_deque)
    let fixed_deque_front: Maybe<i32> = std.collections.fixedDequeFront(&fixed_deque)
    let fixed_deque_live: Span<i32> = std.collections.fixedDequeView(&fixed_deque)
    let fixed_deque_removed: Maybe<i32> = std.collections.fixedDequePopFront(&mut fixed_deque)
    var fixed_ring_values: [4]i32 = [0, 0, 0, 0]
    var fixed_ring: FixedRingBuffer<i32> = std.collections.fixedRingBuffer(fixed_ring_values, 0_usize, 0_usize)
    let fixed_ring_back_pushed: Bool = std.collections.fixedRingBufferPushBack(&mut fixed_ring, 2)
    let fixed_ring_front_pushed: Bool = std.collections.fixedRingBufferPushFront(&mut fixed_ring, 1)
    let fixed_ring_middle: Maybe<i32> = std.collections.fixedRingBufferGet(&fixed_ring, 1_usize)
    let fixed_ring_front: Maybe<i32> = std.collections.fixedRingBufferPopFront(&mut fixed_ring)
    let fixed_ring_wrapped: Bool = std.collections.fixedRingBufferPushBack(&mut fixed_ring, 3)
    var transform: [4]i32 = [1, 2, 3, 4]
    let reversed: Bool = std.collections.reverse(transform, 4_usize)
    let filled: Bool = std.collections.fill(transform, 2_usize, 9)
    let rotated_left: Bool = std.collections.rotateLeft(transform, 4_usize, 1_usize)
    let rotated_right: Bool = std.collections.rotateRight(transform, 4_usize, 2_usize)
    var keys: [3]u8 = [1_u8, 2_u8, 3_u8]
    var scores: [3]u16 = [10_u16, 20_u16, 30_u16]
    var map_len: usize = 2
    map_len = std.collections.mapPut(keys, scores, map_len, 3_u8, 30_u16)
    map_len = std.collections.mapPut(keys, scores, map_len, 2_u8, 25_u16)
    let has_score: Bool = std.collections.mapContains(keys, map_len, 2_u8)
    let score: Maybe<u16> = std.collections.mapGet(keys, scores, map_len, 2_u8)
    let live_keys: Span<u8> = std.collections.mapKeys(keys, map_len)
    let live_scores: Span<u16> = std.collections.mapValues(keys, scores, map_len)
    let map_remaining: usize = std.collections.mapRemaining(keys, scores, map_len)
    let map_full: Bool = std.collections.mapIsFull(keys, scores, map_len)
    map_len = std.collections.mapRemove(keys, scores, map_len, 2_u8)
    let removed_index: usize = std.collections.mapIndex(keys, map_len, 2_u8)
    if len == 4 && inserted_len == 3 && replaced && swapped && std.collections.clear(values, len) == 0 && std.collections.setClear(values, len) == 0 && first.has && first.value == 6 && last.has && last.value == 1 && popped_len == 1 && std.collections.remaining(values, len) == 1 && !std.collections.isFull(values, len) && has_three && std.mem.len(set_live) == 3 && set_remaining == 2 && set_truncated == 1 && fixed_inserted && fixed_removed && std.mem.len(fixed_live) == 2 && fixed_remaining == 2 && fixed_len == 2 && fixed_truncated == 1 && std.collections.fixedSetClear(&mut fixed_set) == 0 && fixed_map_added && fixed_map_updated && fixed_map_score.has && fixed_map_score.value == 25_u16 && std.mem.len(fixed_map_keys) == 3 && std.mem.len(fixed_map_values) == 3 && fixed_map_index == 1 && fixed_map_full && fixed_map_removed && std.collections.fixedMapClear(&mut fixed_map) == 0 && std.collections.contains(values, len, 4) && std.collections.count(values, len, 1) == 2 && std.mem.len(live) == 4 && deque_front.has && deque_front.value == 1 && deque_back.has && deque_back.value == 3 && deque_after_pop_back == 2 && deque_len == 1 && deque_values[0] == 2 && fixed_deque_pushed && fixed_deque_front_pushed && fixed_deque_back.has && fixed_deque_back.value == 2 && fixed_deque_front.has && fixed_deque_front.value == 1 && std.mem.len(fixed_deque_live) == 2 && fixed_deque_removed.has && fixed_deque_removed.value == 1 && fixed_ring_back_pushed && fixed_ring_front_pushed && fixed_ring_middle.has && fixed_ring_middle.value == 2 && fixed_ring_front.has && fixed_ring_front.value == 1 && fixed_ring_wrapped && reversed && filled && rotated_left && rotated_right && transform[0] == 1 && transform[1] == 9 && transform[2] == 9 && transform[3] == 2 && has_score && score.has && score.value == 25_u16 && std.mem.len(live_keys) == 3 && std.mem.len(live_scores) == 3 && map_remaining == 0 && map_full && std.collections.mapClear(keys, scores, map_len) == 0 && std.collections.mapTruncate(keys, scores, map_len, 2_usize) == 2 && map_len == 2 && removed_index == 2 {
        check world.out.write("collections ok\n")
    }
}
```

## Allocator-Backed Storage

```zero
pub fn main(world: World) -> Void raises {
    var key_storage: [4]u8 = [0_u8; 4]
    var value_storage: [4]u8 = [0_u8; 4]
    var key_alloc: FixedBufAlloc = std.mem.fixedBufAlloc(key_storage)
    var value_alloc: FixedBufAlloc = std.mem.fixedBufAlloc(value_storage)
    let keys_maybe: Maybe<MutSpan<u8>> = std.mem.allocBytes(key_alloc, 4_usize)
    let values_maybe: Maybe<MutSpan<u8>> = std.mem.allocBytes(value_alloc, 4_usize)
    if keys_maybe.has && values_maybe.has {
        var map: FixedMap<u8, u8> = std.collections.fixedMap(keys_maybe.value, values_maybe.value, 0_usize)
        let stored: Bool = std.collections.fixedMapPut(&mut map, 7_u8, 42_u8)
        let value: Maybe<u8> = std.collections.fixedMapGet(&map, 7_u8)
        if stored && value.has && value.value == 42_u8 {
            check world.out.write("allocator-backed map ok\n")
        }
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
