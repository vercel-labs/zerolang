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
| `std.inet.writeIpv4(buffer, value)` | `Maybe<Span<u8>>` | Writes a packed IPv4 value as dotted-quad text into caller storage. |
| `std.inet.isIpv4Unspecified(value)` | `Bool` | Checks `0.0.0.0/32`. |
| `std.inet.isIpv4Loopback(value)` | `Bool` | Checks `127.0.0.0/8`. |
| `std.inet.isIpv4Private(value)` | `Bool` | Checks RFC 1918 private ranges. |
| `std.inet.isIpv4LinkLocal(value)` | `Bool` | Checks `169.254.0.0/16`. |
| `std.inet.isIpv4Multicast(value)` | `Bool` | Checks `224.0.0.0/4`. |
| `std.inet.isIpv6(text)` | `Bool` | Validates an RFC 4291 IPv6 literal, including `::` compression and embedded IPv4. |
| `std.inet.parseIpv6(buffer, text)` | `Maybe<Span<u8>>` | Parses an IPv6 literal into 16 network-order bytes in a caller buffer. |
| `std.inet.isIp(text)` | `Bool` | Validates either a strict IPv4 literal or an RFC 4291 IPv6 literal. |
| `std.inet.parseIp(buffer, text)` | `Maybe<Span<u8>>` | Parses IPv4 into 4 bytes or IPv6 into 16 bytes in caller storage. |
| `std.inet.isIpv6Unspecified(bytes)` | `Bool` | Checks `::/128` over a 16-byte IPv6 span. |
| `std.inet.isIpv6Loopback(bytes)` | `Bool` | Checks `::1/128` over a 16-byte IPv6 span. |
| `std.inet.isIpv6Multicast(bytes)` | `Bool` | Checks `ff00::/8`. |
| `std.inet.isIpv6LinkLocal(bytes)` | `Bool` | Checks `fe80::/10`. |
| `std.inet.isIpv6Private(bytes)` | `Bool` | Checks the `fc00::/7` unique-local range. |
| `std.inet.isIpv6UniqueLocal(bytes)` | `Bool` | Alias for the `fc00::/7` unique-local range. |
| `std.inet.isIpv6MappedIpv4(bytes)` | `Bool` | Checks `::ffff:0:0/96` IPv4-mapped addresses. |
| `std.inet.ipv6MappedIpv4(bytes)` | `Maybe<u32>` | Extracts the packed IPv4 value from an IPv4-mapped IPv6 span. |
| `std.inet.isHostname(text)` | `Bool` | Validates an RFC 1123 hostname: dot-separated 1-63 byte alphanumeric/hyphen labels, 253 bytes total. |

## Example

```zero
pub fn main(world: World) -> Void raises {
    var storage: [16]u8 = [0; 16]
    let buffer: MutSpan<u8> = storage
    let quad: Maybe<u32> = std.inet.parseIpv4("192.168.0.1")
    let mapped: Maybe<Span<u8>> = std.inet.parseIpv6(buffer, "::ffff:192.168.1.1")
    if std.inet.isHostname("example.com") && (quad.has && mapped.has) && (std.inet.isIpv4Private(quad.value) && std.inet.isIpv6MappedIpv4(mapped.value)) {
        check world.out.write("inet ok\n")
    }
}
```

Effects: none.

Allocation behavior: `parseIp` and `parseIpv6` write into caller buffers;
`writeIpv4` writes into caller storage. The other helpers allocate nothing.

Error behavior: validators return `Bool`; parsers return `null` for invalid
literals or undersized buffers.

Target support: current compiler targets; no network capability required.
