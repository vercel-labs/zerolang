## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.rand.seed(value)` | `RandSource` | Creates a deterministic test source. |
| `std.rand.nextU32(&mut source)` | `u32` | Advances an explicit random source. |
| `std.rand.entropyU32()` | `u32` | Reads target entropy where the target provides it. |

Metadata labels:

- effects: rand
- allocation behavior: no allocation
- target support: deterministic source is target-neutral; entropy requires a rand-capable target
- error behavior: infallible helpers
- ownership notes: `nextU32` mutates the caller-owned source
- example: `examples/std-platform.0`

## Example

```zero
pub fun main(world: World) -> Void raises {
    let mut rng = std.rand.seed(7_u32)
    let first = std.rand.nextU32(&mut rng)
    let second = std.rand.nextU32(&mut rng)
    if first != second {
        check world.out.write("rand ok\n")
    }
}
```

## Design Notes

Zero keeps random sources explicit. Deterministic tests use `std.rand.seed`.

Production entropy stays target-capability-gated.
