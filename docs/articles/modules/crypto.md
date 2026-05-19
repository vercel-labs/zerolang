## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.crypto.hash32(bytes)` | `u32` | Computes the current 32-bit hash helper over bytes. |
| `std.crypto.hmac32(key, bytes)` | `u32` | Computes the current keyed 32-bit helper over bytes. |
| `std.crypto.constantTimeEql(a, b)` | `Bool` | Compares byte spans without data-dependent early exit. |
| `std.crypto.secureRandomU32()` | `u32` | Reads target entropy where the target provides it. |

Metadata labels:

- effects: codec, memory, or rand
- allocation behavior: no allocation
- target support: hash helpers are target-neutral; secure random requires a rand-capable target
- error behavior: infallible helpers
- ownership notes: borrows caller-provided byte spans
- example: `examples/std-platform.0`

## Example

```zero
pub fun main(world: World) -> Void raises {
    let hash = std.crypto.hash32(std.mem.span("message"))
    let hmac = std.crypto.hmac32(std.mem.span("key"), std.mem.span("message"))
    if hash > 0 && hmac > 0 &&
        std.crypto.constantTimeEql(std.mem.span("same"), std.mem.span("same")) {
        check world.out.write("crypto ok\n")
    }
}
```

## Design Notes

`std.crypto` is a small helper surface. It is not a full cryptography suite, TLS
stack, certificate store, or secret-management API.
