## Status

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.http.parseMethod(text)` | `HttpMethod` | Parses a small HTTP method token. |
| `std.http.client(net)` | `HttpClient` | Creates hosted client metadata from a network capability. |
| `std.http.server(net, address)` | `HttpServer` | Creates hosted server metadata from a network capability and address. |
| `std.http.fetch(client, request, response, timeout)` | `HttpResult` | Performs a hosted HTTP(S) request with a `Duration` timeout and writes response metadata, headers, and body into caller-owned storage. |
| `std.http.resultOk(result)` | `Bool` | True when transport succeeded and the status is 2xx. |
| `std.http.resultStatus(result)` | `u16` | Reads the HTTP status, or `0` when no status was available. |
| `std.http.resultBodyLen(result)` | `usize` | Reads the number of response body bytes written into the response buffer. |
| `std.http.resultError(result)` | `HttpError` | Reads the transport/provider error. |
| `std.http.errorNone()` | `HttpError` | Transport succeeded. |
| `std.http.errorInvalidUrl()` | `HttpError` | The request URL was invalid. |
| `std.http.errorUnsupportedProtocol()` | `HttpError` | The URL protocol is not supported. |
| `std.http.errorDns()` | `HttpError` | DNS lookup failed. |
| `std.http.errorConnect()` | `HttpError` | Connecting to the remote host failed. |
| `std.http.errorTls()` | `HttpError` | TLS verification or connection setup failed. |
| `std.http.errorTimeout()` | `HttpError` | The request timed out. |
| `std.http.errorTooLarge()` | `HttpError` | The response did not fit in the caller-owned buffer. |
| `std.http.errorProviderUnavailable()` | `HttpError` | The hosted HTTP provider is unavailable. |
| `std.http.errorIo()` | `HttpError` | The provider reported an I/O failure. |
| `std.http.errorInvalidRequest()` | `HttpError` | The request envelope was invalid. |
| `std.http.responseLen(response)` | `usize` | Reads the response byte count written after the internal metadata prefix. |
| `std.http.responseHeadersLen(response)` | `usize` | Reads the raw response header byte count. |
| `std.http.responseBodyOffset(response)` | `usize` | Reads the body start offset within the response buffer. |
| `std.http.headerValue(response, name)` | `HttpHeaderValue` | Locates a response header value by case-insensitive header name. |
| `std.http.headerFound(value)` | `Bool` | True when `headerValue` found a matching header. |
| `std.http.headerOffset(value)` | `usize` | Reads the header value byte offset within the response buffer. |
| `std.http.headerLen(value)` | `usize` | Reads the header value byte length. |
| `std.http.tlsBoundary()` | `String` | Names the platform or C-library TLS boundary. |

Metadata labels:

- effects: net, memory, parse, or none for named error constants
- allocation behavior: metadata helpers do not allocate; `fetch` writes into a
  caller-owned response buffer and uses the provider runtime internally
- target support: parsing helpers are target-neutral; client/server require a
  net-capable target; `fetch` runs on supported Darwin arm64 and Linux x64 host
  executable targets
- error behavior: metadata helpers are infallible; `fetch` returns status,
  body length, and error metadata so non-2xx responses are distinguishable from
  transport failures
- ownership notes: HTTP helpers borrow network capability metadata and write
  only to caller-owned buffers
- examples: `conformance/native/pass/std-http-metadata-neutral.0`,
  `conformance/native/pass/std-http-fetch.0`,
  `conformance/native/pass/std-http-errors.0`,
  `conformance/native/pass/std-http-response-helpers.0`,
  `examples/std-http-json.0`,
  `examples/std-http-request.0`,
  `examples/std-http-headers.0`

## Example

Metadata helpers:

```zero
pub fun main(world: World) -> Void raises {
    let net = std.net.host()
    let addr = std.net.address("localhost", 8080_u16)
    let _client = std.http.client(net)
    let _server = std.http.server(net, addr)
    let method = std.http.parseMethod("GET")
    if method == std.http.parseMethod("GET") &&
        std.mem.len(std.mem.span("body")) == 4 {
        check world.out.write("http ok\n")
    }
}
```

GET request:

```zero
pub fun main(world: World) -> Void raises {
    let net = std.net.host()
    let client = std.http.client(net)
    let mut response: [512]u8 = [0_u8; 512]
    let request = std.mem.span("GET https://example.com\n\n")
    let result = std.http.fetch(client, request, response, std.time.ms(1000))
    if std.http.resultOk(result) {
        check world.out.write("http get ok\n")
        return
    }
    check world.err.write("http get failed\n")
}
```

Request with headers and body:

```zero
pub fun main(world: World) -> Void raises {
    let net = std.net.host()
    let client = std.http.client(net)
    let request = std.mem.span("POST https://example.com/api\ncontent-type: application/json\n\n{\"ping\":1}")
    let mut response: [512]u8 = [0_u8; 512]
    let result = std.http.fetch(client, request, response, std.time.ms(1000))
    if std.http.resultOk(result) {
        check world.out.write("http post ok\n")
        return
    }
    check world.err.write("http post failed\n")
}
```

Response bytes:

```zero
pub fun main(world: World) -> Void raises {
    let maybe_request = std.args.get(1)
    if maybe_request.has == false {
        check world.err.write("usage: pass HTTP request envelope\n")
        return
    }
    let net = std.net.host()
    let client = std.http.client(net)
    let mut response: [512]u8 = [0_u8; 512]
    let result = std.http.fetch(client, std.mem.span(maybe_request.value), response, std.time.ms(5000))
    let body_len = std.http.resultBodyLen(result)
    let body_offset = std.http.responseBodyOffset(response)
    let bytes = response[body_offset..body_offset + body_len]
    let mut arena_buf: [16]u8 = [0_u8; 16]
    let mut arena = std.mem.fixedBufAlloc(arena_buf)
    let parsed = std.json.parseBytes(arena, bytes)
    if std.http.resultOk(result) && parsed.has {
        check world.out.write("http response json ok\n")
        return
    }
    check world.err.write("http response json failed\n")
}
```

## Design Notes

`std.http.fetch` is the outbound HTTP client primitive. The request argument is
one byte envelope: `METHOD URL`, followed by zero or more `Header-Name: value`
lines, a blank line, and optional request body bytes. The timeout is a
`Duration`, typically built with `std.time.ms(...)`.

`fetch` supports `http://` and `https://` URLs on supported host executable
targets, uses the C-library curl provider, does not follow redirects, and
verifies TLS through the provider. TLS verification is always enabled.
`ZERO_HTTP_TEST_CA_BUNDLE` can point the provider at an explicit CA bundle
while keeping certificate verification on.

The response buffer starts with internal metadata, followed by raw response
headers and then the response body. Use `responseHeadersLen` and
`responseBodyOffset` rather than hard-coding offsets. `headerValue` scans the
response buffer and returns packed offset/length metadata for the matching
value; `headerFound`, `headerOffset`, and `headerLen` inspect that metadata
without allocating.

Compare `resultError` with the named `HttpError` helpers rather than raw
numbers. `errorNone` means the transport succeeded; HTTP non-2xx statuses still
carry `errorNone` and can be inspected with `resultStatus`.

The module does not expose raw socket read/write APIs, structured header
collections, streaming bodies, redirects, or a heap-allocated response object.
