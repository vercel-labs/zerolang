## When To Use std.url

In Zerolang, use `std.url` for lexical URL splitting, percent encoding, decoded
query lookup, form-urlencoded bodies, and query appending.

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
| `std.url.fragment(url)` | `Maybe<Span<u8>>` | Borrows the raw fragment if present. |
| `std.url.queryValue(query, key)` | `Maybe<Span<u8>>` | Borrows a raw query parameter value by key. |
| `std.url.queryValueDecoded(buffer, query, key)` | `Maybe<Span<u8>>` | Looks up a raw or escaped query key and writes the decoded value. |
| `std.url.writeQueryParam(buffer, key, value)` | `Maybe<Span<u8>>` | Writes an escaped `key=value` query parameter. |
| `std.url.writeFormField(buffer, key, value)` | `Maybe<Span<u8>>` | Writes one application/x-www-form-urlencoded field. |
| `std.url.appendFormField(buffer, form, field)` | `Maybe<Span<u8>>` | Appends one encoded field to an existing form body. |
| `std.url.formValue(buffer, form, key)` | `Maybe<Span<u8>>` | Looks up a form field by raw or escaped key and writes the decoded value. |
| `std.url.appendQuery(buffer, base, query)` | `Maybe<Span<u8>>` | Writes a URL with an appended raw query segment. |
| `std.url.writeUrl(buffer, scheme, host, path)` | `Maybe<Span<u8>>` | Writes a `scheme://host/path` URL. |
| `std.url.appendFragment(buffer, base, fragment)` | `Maybe<Span<u8>>` | Writes a URL with an appended raw fragment. |

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
    let url: Span<u8> = "https://example.com/path?q=zero%20lang#part"
    let host: Maybe<Span<u8>> = std.url.host(url)
    let query: Maybe<Span<u8>> = std.url.query(url)
    let fragment: Maybe<Span<u8>> = std.url.fragment(url)
    var out: [48]u8 = [0_u8; 48]
    var param_buf: [16]u8 = [0_u8; 16]
    let param: Maybe<Span<u8>> = std.url.writeQueryParam(param_buf, "q", "zero lang")
    var decoded_buf: [16]u8 = [0_u8; 16]
    var decoded: Maybe<Span<u8>> = null
    var next: Maybe<Span<u8>> = null
    if param.has {
        next = std.url.appendQuery(out, "https://example.com/path", param.value)
    }
    if query.has {
        decoded = std.url.queryValueDecoded(decoded_buf, query.value, "q")
    }
    if host.has && query.has && fragment.has && decoded.has && next.has && std.mem.eql(host.value, "example.com") && std.mem.eql(decoded.value, "zero lang") {
        check world.out.write("url ok\n")
    }
}
```

## Design Notes

URL helpers are lexical and byte-oriented. They do not resolve DNS, normalize
paths, or allocate. Decoding rejects malformed percent escapes. Form helpers use
the same encoding as query strings: spaces become `+`, and other non-unreserved
bytes are percent-escaped. URL builders expect path, query, and fragment bytes
that are already escaped for their position.
