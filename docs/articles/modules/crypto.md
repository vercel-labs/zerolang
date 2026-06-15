## When To Use std.crypto

In Zerolang, use `std.crypto` for small hash, keyed hash, constant-time equality, and target
entropy helpers with explicit capability boundaries.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.crypto.hash32(bytes)` | `u32` | Computes the current 32-bit hash helper over bytes. |
| `std.crypto.hmac32(key, bytes)` | `u32` | Computes the current keyed 32-bit helper over bytes. |
| `std.crypto.constantTimeEql(a, b)` | `Bool` | Compares byte spans without data-dependent early exit. |
| `std.crypto.secureRandomU32()` | `u32` | Reads target entropy where the target provides it. |
| `std.crypto.fixedHex32(buffer, value)` | `Maybe<Span<u8>>` | Writes an 8-byte lowercase hex value into caller storage. |
| `std.crypto.hashHex32(buffer, bytes)` | `Maybe<Span<u8>>` | Writes the 32-bit hash as fixed-width lowercase hex. |
| `std.crypto.hmacHex32(buffer, key, bytes)` | `Maybe<Span<u8>>` | Writes the keyed 32-bit helper as fixed-width lowercase hex. |
| `std.crypto.stableId32(buffer, bytes)` | `Maybe<Span<u8>>` | Writes a deterministic 8-byte ID from input bytes. |
| `std.crypto.randomId32(buffer)` | `Maybe<Span<u8>>` | Writes an 8-byte random ID from target entropy. |

Metadata labels:

- effects: codec, memory, or rand
- allocation behavior: no allocation; text helpers write caller-provided buffers
- target support: hash helpers are target-neutral; secure random requires a rand-capable target
- error behavior: infallible helpers
- ownership notes: borrows caller-provided byte spans
- example: `examples/std-platform.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let hash: u32 = std.crypto.hash32(std.mem.span("message"))
    let hmac: u32 = std.crypto.hmac32(std.mem.span("key"), std.mem.span("message"))
    var id_buf: [8]u8 = [0_u8; 8]
    let id: Maybe<Span<u8>> = std.crypto.stableId32(id_buf, std.mem.span("message"))
    if hash > 0 && hmac > 0 && id.has && std.crypto.constantTimeEql(std.mem.span("same"), std.mem.span("same")) {
        check world.out.write("crypto ok\n")
    }
}
```

## Design Notes

`std.crypto` is a small helper surface. The fixed-width ID helpers are useful
for deterministic labels, cache keys, fixtures, and examples. They are not a
full cryptography suite, TLS stack, certificate store, password hashing API, or
secret-management API.
