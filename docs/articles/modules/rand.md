## When To Use std.rand

In Zerolang, use `std.rand` for deterministic random sources and target-gated entropy.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.rand.seed(value)` | `RandSource` | Creates a deterministic test source. |
| `std.rand.nextU32(&mut source)` | `u32` | Advances an explicit random source. |
| `std.rand.nextBool(&mut source)` | `Bool` | Advances an explicit random source and returns one random bit. |
| `std.rand.nextBelow(&mut source, bound)` | `Maybe<u32>` | Returns a value in `[0, bound)` using rejection sampling, or null when bound is zero. |
| `std.rand.rangeU32(&mut source, low, high)` | `Maybe<u32>` | Returns a value in `[low, high)` using rejection sampling, or null for an empty range. |
| `std.rand.entropyU32()` | `u32` | Reads target entropy where the target provides it. |
| `std.rand.entropySeed()` | `RandSource` | Creates a `RandSource` from target entropy where available. |
| `std.rand.entropyHex32(buffer)` | `Maybe<Span<u8>>` | Writes an 8-byte lowercase entropy ID into caller storage. |

Metadata labels:

- effects: rand
- allocation behavior: no allocation; `entropyHex32` writes caller-provided storage
- target support: deterministic source is target-neutral; entropy requires a rand-capable target
- error behavior: infallible helpers
- ownership notes: deterministic helpers mutate the caller-owned source
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var rng: RandSource = std.rand.seed(7_u32)
    let first: u32 = std.rand.nextU32(&mut rng)
    let second: Bool = std.rand.nextBool(&mut rng)
    let bounded: Maybe<u32> = std.rand.nextBelow(&mut rng, 10_u32)
    let ranged: Maybe<u32> = std.rand.rangeU32(&mut rng, 40_u32, 50_u32)
    var id_buf: [8]u8 = [0_u8; 8]
    let entropy_id: Maybe<Span<u8>> = std.rand.entropyHex32(id_buf)
    if first == 1025555898_u32 && second && bounded.has && ranged.has && entropy_id.has {
        check world.out.write("rand ok\n")
    }
}
```

## Design Notes

Zero keeps random sources explicit. Deterministic tests use `std.rand.seed`;
bounded helpers use rejection sampling so the range is not modulo-biased.
Caller-facing IDs use `entropyHex32` when target entropy is available.

Production entropy stays target-capability-gated.
