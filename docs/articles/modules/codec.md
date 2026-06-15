## When To Use std.codec

In Zerolang, use `std.codec` for byte encodings: endian integer reads/writes, varints,
base64, hex, and checksums over caller-owned storage.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.codec.crc32(bytes)` | `u32` | Computes CRC-32 for a string-backed byte input. |
| `std.codec.crc32Bytes(bytes)` | `u32` | Computes CRC-32 for a span or mutable span without allocation. |
| `std.codec.encodedVarintLen(value)` | `usize` | Returns the byte length of an unsigned varint encoding. |
| `std.codec.varintEncode(buffer, value)` | `Maybe<Span<u8>>` | Writes an unsigned varint into caller storage. |
| `std.codec.varintDecode(bytes)` | `Maybe<u32>` | Decodes a bounded unsigned varint. |
| `std.codec.readU8(bytes)` | `u8` | Reads one byte. |
| `std.codec.readU16(bytes)` | `u16` | Reads two bytes as little-endian. |
| `std.codec.readU32(bytes)` | `u32` | Reads four bytes as little-endian. |
| `std.codec.writeU16(value)` | `u32` | Packs a `u16` value into the current write representation. |
| `std.codec.writeU32(value)` | `u32` | Packs a `u32` value into the current write representation. |
| `std.codec.readU16Le(bytes)` | `Maybe<u16>` | Bounds-checked little-endian read from a byte span. |
| `std.codec.readU16Be(bytes)` | `Maybe<u16>` | Bounds-checked big-endian read from a byte span. |
| `std.codec.readU32Le(bytes)` | `Maybe<u32>` | Bounds-checked little-endian read from a byte span. |
| `std.codec.readU32Be(bytes)` | `Maybe<u32>` | Bounds-checked big-endian read from a byte span. |
| `std.codec.writeU16Le(buffer, value)` | `Maybe<Span<u8>>` | Writes little-endian bytes into caller storage. |
| `std.codec.writeU16Be(buffer, value)` | `Maybe<Span<u8>>` | Writes big-endian bytes into caller storage. |
| `std.codec.writeU32Le(buffer, value)` | `Maybe<Span<u8>>` | Writes little-endian bytes into caller storage. |
| `std.codec.writeU32Be(buffer, value)` | `Maybe<Span<u8>>` | Writes big-endian bytes into caller storage. |
| `std.codec.base64EncodedLen(len)` | `usize` | Returns the encoded length for a base64 payload. |
| `std.codec.base64Encode(buffer, bytes)` | `Maybe<String>` | Writes base64 text into caller storage. |
| `std.codec.base64DecodedLen(bytes)` | `Maybe<usize>` | Validates padded base64 text and returns decoded length. |
| `std.codec.base64Decode(buffer, bytes)` | `Maybe<Span<u8>>` | Writes decoded base64 bytes into caller storage. |
| `std.codec.hexEncode(buffer, bytes)` | `Maybe<String>` | Writes lowercase hexadecimal text into caller storage. |
| `std.codec.hexDecodedLen(bytes)` | `Maybe<usize>` | Validates hex text and returns decoded length. |
| `std.codec.hexDecode(buffer, bytes)` | `Maybe<Span<u8>>` | Writes decoded hex bytes into caller storage. |
| `std.codec.utf8Valid(bytes)` | `Bool` | Validates a byte span as UTF-8. |
| `std.codec.urlEncode(buffer, text)` | `Maybe<String>` | Percent-encodes a string into caller storage. |

Current limits:

- Buffer-backed write APIs.
- Streaming encoders and decoders.

## Example

```zero
use std.codec

use std.mem

pub fn main(world: World) -> Void raises {
    let len: usize = std.codec.encodedVarintLen(300)
    let checksum: u32 = std.codec.crc32("zero")
    let bytes: Span<u8> = std.mem.span("zero")
    let byte_checksum: u32 = std.codec.crc32Bytes(bytes)
    var encoded: [5]u8 = [0_u8; 5]
    let varint: Maybe<Span<u8>> = std.codec.varintEncode(encoded, 300_u32)
    var decoded: [4]u8 = [0_u8; 4]
    let text: Maybe<Span<u8>> = std.codec.base64Decode(decoded, "emVybw==")
    if len == 2 && checksum == byte_checksum && varint.has && text.has && std.mem.eql(text.value, "zero") {
        check world.out.write("codec primitives ok\n")
    }
}
```

## Design Notes

Codec helpers are byte-oriented and allocation-free. Decoders return
`Maybe<T>` on malformed input or insufficient caller storage.
