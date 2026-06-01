## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.rand.seed(value)` | `RandSource` | Creates a deterministic test source. |
| `std.rand.nextU32(&mut source)` | `u32` | Advances an explicit random source. |
| `std.rand.nextBool(&mut source)` | `Bool` | Advances an explicit random source and returns one random bit. |
| `std.rand.entropyU32()` | `u32` | Reads target entropy where the target provides it. |
| `std.rand.entropySeed()` | `RandSource` | Creates a `RandSource` from target entropy where available. |

Metadata labels:

- effects: rand
- allocation behavior: no allocation
- target support: deterministic source is target-neutral; entropy requires a rand-capable target
- error behavior: infallible helpers
- ownership notes: `nextU32` and `nextBool` mutate the caller-owned source
- example: `examples/std-platform.0`

## Example

```zero
pub fn main(world: World) -> Void raises {
    var rng: RandSource = std.rand.seed(7_u32)
    let first: u32 = std.rand.nextU32(&mut rng)
    let second: Bool = std.rand.nextBool(&mut rng)
    if first == 1025555898_u32 && second {
        check world.out.write("rand ok\n")
    }
}
```

## Design Notes

Zero keeps random sources explicit. Deterministic tests use `std.rand.seed`.

Production entropy stays target-capability-gated.
