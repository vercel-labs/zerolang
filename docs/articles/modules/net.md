## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.net.host()` | `Net` | Creates the hosted network capability. |
| `std.net.address(host, port)` | `Address` | Builds an address value without allocation. |
| `std.net.dnsName(address)` | `String` | Reads the address host name. |
| `std.net.withTimeout(address, duration)` | `Address` | Returns address metadata with a timeout. |
| `std.net.connect(net, address)` | `Maybe<Conn>` | Returns a bootstrap connection handle when available. |
| `std.net.listen(net, address)` | `Maybe<Listener>` | Returns a bootstrap listener handle when available. |

Metadata labels:

- effects: net
- allocation behavior: no allocation
- target support: address helpers are target-neutral; host/connect/listen require a net-capable target
- error behavior: connection helpers return `Maybe`
- ownership notes: no stream ownership transfer in the current handle model
- example: `conformance/native/pass/std-net-http-breadth.0`

## Example

```zero
pub fun main(world: World) -> Void raises {
    let net = std.net.host()
    let addr = std.net.withTimeout(std.net.address("localhost", 8080_u16), std.time.ms(250))
    let conn = std.net.connect(net, addr)
    if conn.has && std.mem.eql(std.net.dnsName(addr), "localhost") {
        check world.out.write("net ok\n")
    }
}
```

## Design Notes

`std.net` exposes network capability metadata and bootstrap handles. Current
fixtures expect connection and listener handles to be absent. It does not
provide socket read/write APIs in the current public surface. Outbound HTTP is
exposed through `std.http.fetch(...)` rather than through raw sockets.
