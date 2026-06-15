## When To Use std.inet

In Zerolang, use `std.inet` to validate and parse internet address literals:
IPv4, IPv6, and RFC 1123 hostnames. These helpers are target-neutral and need
no network capability, so they work in validators and parsers on any compiler
target. Use `std.net` when a program actually opens connections or listeners.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.inet.isIpv4(text)` | `Bool` | Validates a dotted-quad IPv4 literal: four decimal octets 0-255 with no leading zeros. |
| `std.inet.parseIpv4(text)` | `Maybe<u32>` | Parses an IPv4 literal into a big-endian packed `u32`. |
| `std.inet.isIpv6(text)` | `Bool` | Validates an RFC 4291 IPv6 literal, including `::` compression and embedded IPv4. |
| `std.inet.parseIpv6(buffer, text)` | `Maybe<Span<u8>>` | Parses an IPv6 literal into 16 network-order bytes in a caller buffer. |
| `std.inet.isHostname(text)` | `Bool` | Validates an RFC 1123 hostname: dot-separated 1-63 byte alphanumeric/hyphen labels, 253 bytes total. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [16]u8 = [0; 16]
    let buffer: MutSpan<u8> = storage
    let quad: Maybe<u32> = std.inet.parseIpv4("192.168.0.1")
    let mapped: Maybe<Span<u8>> = std.inet.parseIpv6(buffer, "::ffff:192.168.1.1")
    if std.inet.isHostname("example.com") && (quad.has && mapped.has) && !std.inet.isIpv4("256.1.1.1") {
        check world.out.write("inet ok\n")
    }
}
```

Effects: none.

Allocation behavior: `parseIpv6` writes 16 bytes into the caller buffer; the
other helpers allocate nothing.

Error behavior: validators return `Bool`; parsers return `null` for invalid
literals or undersized buffers.

Target support: current compiler targets; no network capability required.
