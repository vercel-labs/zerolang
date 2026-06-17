## Graph-Backed Library Surface

The Zerolang standard library is graph-backed. The compiler uses binary `std/*.graph`
stores. Sibling `std/*.0` files are human-readable projections for review, not
the normal compile path.

Agents should learn the callable surface from the installed compiler:

```sh
zero skills get stdlib
```

Humans should use this page to decide which module to ask for.

## Expected Usage

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "make a small json http api"
    },
    {
      "role": "assistant",
      "text": "I’ll use the HTTP and JSON helpers and run a couple route checks."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero skills get stdlib",
          "output": "stdlib helpers: std.http.writeJsonOk, std.http.requestIsGet, std.json.field, ..."
        },
        {
          "command": "zero query --find handle",
          "output": "fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>"
        }
      ]
    }
  ]
}
```

## Module Groups

Core data and memory:

- `std.mem`: spans, byte equality, copy/fill, allocators, byte buffers, and fixed-capacity vectors.
- `std.collections`: fixed-capacity collection operations over caller-owned storage.
- `std.search`: scalar span search and binary search.
- `std.sort`: in-place sorting over caller-owned scalar storage.
- `std.ascii`, `std.text`, `std.str`: byte-backed text helpers.
- `std.unicode`: strict UTF-8 codepoint decode/encode iteration and codepoint classes.
- `std.parse`, `std.fmt`, `std.codec`, `std.math`: parsers, formatters, codecs, and numeric helpers.
- `std.regex`: compile-once regular expression matching for a documented subset.
- `std.inet`: IPv4, IPv6, and hostname literal validation and parsing.

Program surfaces:

- `std.args`, `std.cli`, `std.env`: command-line and environment helpers.
- `std.io`, `std.fs`, `std.path`: caller-buffer I/O, hosted filesystem helpers, and lexical paths.
- `std.json`, `std.toml`, `std.url`, `std.csv`, `std.log`: data formats and structured output.
- `std.testing`: test-block predicates.

Runtime and web:

- `std.time`, `std.rand`, `std.proc`, `std.term`, `std.crypto`: hosted/runtime helper surfaces, terminal sequences, key decoding, and terminal metadata.
- `std.net`, `std.http`: network metadata, HTTP client/server metadata, request parsing, response writing, hosted fetch, and local listen support.

## Inspect What A Program Uses

```sh
zero inspect --json examples/crm-api
zero size --json examples/crm-api
zero mem --json examples/allocator-collections.graph
```

Useful JSON fields include:

- `usedStdlibHelpers`
- `stdlibHelpers`
- `effects`
- `allocationBehavior`
- `targetSupport`
- `errorBehavior`
- `ownershipNotes`
- `apiStability`

## Allocation And Capability Rule

Standard library helpers should make ownership, effects, and target support
visible. Hosted APIs such as filesystem, process, time, random, network, and
HTTP require target capabilities. Buffer-oriented helpers should write into
caller-owned storage rather than silently allocating.

## Projections In Module Pages

Module pages include `.0` snippets because humans need readable examples. Treat
those snippets as projection examples. Agents should patch user programs
through graph commands and use `zero export` only when a human asks to review
the resulting projection.
