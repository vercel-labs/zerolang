## Graph Surface

This module is graph-backed. The compiler uses its standard-library graph store,
while the Zero snippets below show the human-readable projection that agents may
export for review. Agents should discover helpers with `zero skills get stdlib`,
inspect user packages with `zero query [graph-input]` or
`zero inspect [graph-input]`, and patch user code through the graph instead of
hand-editing `.0` files.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.url.percentEncode(buffer, bytes)` | `Maybe<Span<u8>>` | Percent-encodes bytes into caller storage. |
| `std.url.percentDecode(buffer, bytes)` | `Maybe<Span<u8>>` | Percent-decodes bytes into caller storage. |
| `std.url.queryEscape(buffer, bytes)` | `Maybe<Span<u8>>` | Query-escapes bytes, using `+` for spaces. |
| `std.url.queryUnescape(buffer, bytes)` | `Maybe<Span<u8>>` | Query-unescapes bytes, converting `+` back to space. |
| `std.url.scheme(url)` | `Maybe<Span<u8>>` | Borrows the URL scheme if present. |
| `std.url.authority(url)` | `Maybe<Span<u8>>` | Borrows the URL authority if present. |
| `std.url.host(url)` | `Maybe<Span<u8>>` | Borrows the host from the URL authority. |
| `std.url.path(url)` | `Span<u8>` | Borrows the path or an empty suffix. |
| `std.url.query(url)` | `Maybe<Span<u8>>` | Borrows the raw query string if present. |
| `std.url.queryValue(query, key)` | `Maybe<Span<u8>>` | Borrows a raw query parameter value by key. |
| `std.url.writeQueryParam(buffer, key, value)` | `Maybe<Span<u8>>` | Writes an escaped `key=value` query parameter. |
| `std.url.appendQuery(buffer, base, query)` | `Maybe<Span<u8>>` | Writes a URL with an appended raw query segment. |

Metadata labels:

- effects: parse
- allocation behavior: no allocation; writers use caller storage
- target support: target-neutral
- error behavior: `Maybe` helpers return null on malformed input or insufficient storage
- ownership notes: borrowed slices point into the input; encoded output points into caller storage
- examples: `conformance/native/pass/std-codec-json-url.graph`

## Example

```zero
pub fn main(world: World) -> Void raises {
    let url: Span<u8> = "https://example.com/path?q=zero%20lang"
    let host: Maybe<Span<u8>> = std.url.host(url)
    let query: Maybe<Span<u8>> = std.url.query(url)
    var out: [48]u8 = [0_u8; 48]
    var param_buf: [16]u8 = [0_u8; 16]
    let param: Maybe<Span<u8>> = std.url.writeQueryParam(param_buf, "q", "zero lang")
    var next: Maybe<Span<u8>> = null
    if param.has {
        next = std.url.appendQuery(out, "https://example.com/path", param.value)
    }
    if host.has && query.has && next.has && std.mem.eql(host.value, "example.com") {
        check world.out.write("url ok\n")
    }
}
```

## Design Notes

URL helpers are lexical and byte-oriented. They do not resolve DNS, normalize
paths, or allocate. Decoding rejects malformed percent escapes.
