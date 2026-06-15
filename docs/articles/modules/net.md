## When To Use std.net

In Zerolang, use `std.net` for network capability metadata, local address construction,
timeouts, and bootstrap client/listener handles.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.net.host()` | `Net` | Creates the hosted network capability. |
| `std.net.address(host, port)` | `Address` | Builds an address value without allocation. |
| `std.net.dnsName(address)` | `String` | Reads the address host name. |
| `std.net.withTimeout(address, duration)` | `Address` | Returns address metadata with a timeout. |
| `std.net.localhost(port)` | `Address` | Builds a `localhost` address with the requested port. |
| `std.net.loopback(port)` | `Address` | Builds a `127.0.0.1` loopback address with the requested port. |
| `std.net.connect(net, address)` | `Maybe<Conn>` | Returns a bootstrap connection handle when available. |
| `std.net.listen(net, address)` | `Maybe<Listener>` | Returns a bootstrap listener handle when available. |

Metadata labels:

- effects: net
- allocation behavior: no allocation
- target support: address helpers are target-neutral; host/connect/listen require a net-capable target
- error behavior: connection helpers return `Maybe`
- ownership notes: no stream ownership transfer in the current handle model
- example: `conformance/native/pass/std-net-http-breadth.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let net: Net = std.net.host()
    let addr: Address = std.net.withTimeout(std.net.localhost(8080_u16), std.time.ms(250))
    let loopback: Address = std.net.loopback(8080_u16)
    let conn: Maybe<Conn> = std.net.connect(net, addr)
    if conn.has && std.mem.eql(std.net.dnsName(addr), "localhost") && std.mem.eql(std.net.dnsName(loopback), "127.0.0.1") {
        check world.out.write("net ok\n")
    }
}
```

## Design Notes

`std.net` exposes network capability metadata and bootstrap handles. Current
fixtures expect connection and listener handles to be absent. It does not
provide socket read/write APIs in the current public surface. Outbound HTTP is
exposed through `std.http.fetch(...)` rather than through raw sockets.
