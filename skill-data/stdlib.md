---
name: stdlib
description: Use Zero standard library modules and target-gated capabilities.
---

# Zero Standard Library

Use this when an agent needs common library calls, memory helpers, hosted I/O, or target-capability guidance.

## Import

```zero
use std.mem

use std.parse
```

Call functions with their module path, such as `std.mem.len(value)`.

## Target-Neutral Helpers

- `std.mem`: spans, byte copy/fill, non-owned scalar item copy/fill/search, scalar item slicing, length, safe indexed `get`, fixed-buffer allocation, byte buffers, and caller-owned vectors.
- `std.collections`: fixed-capacity push, append, live-prefix view, count, contains, swap-remove, and move-to-front helpers over caller-owned storage plus explicit lengths.
- `std.search`: generic scalar index search plus typed lower-bound and binary-search helpers.
- `std.sort`: in-place insertion sort and sortedness checks for `i32`, `u32`, and `usize` storage.
- `std.ascii`: ASCII byte predicates, case conversion, and digit value helpers.
- `std.fmt`: caller-buffer formatting for booleans and integer text.
- `std.text`: ASCII and UTF-8 byte-backed text validation.
- `std.math`: fixed-width min/max/clamp, checked and saturating integer arithmetic, GCD/LCM, powers, modular power, roots, combinatorics, primality, and divisor routines.
- `std.path`: target-neutral lexical path basename, dirname, extension, join, normalize, and relative helpers.
- `std.codec`: byte reads, endian reads/writes, varint sizing/encode/decode, base64/hex encode/decode, CRC helpers, and byte checksums.
- `std.parse`: byte scanners and integer/bool parsers returning `Maybe<T>`.
- `std.time`: duration construction, conversion, comparison, clamp, and target-gated clock helpers.
- `std.rand`: explicit deterministic random sources, random bits, and target entropy helpers.
- `std.crypto`: small hash and byte-oriented crypto helpers.
- `std.json`: explicit-buffer JSON validation, structured status codes, shallow field lookup, typed scalar decode, parsing, and string/object writing helpers.
- `std.toml`: no-allocation TOML validation, shallow/dotted field lookup, and typed scalar decode helpers.
- `std.url`: target-neutral URL splitting, percent/query encoding and decoding, query lookup, and query append helpers.
- `std.str`: byte-span string helpers, including non-overlapping reverse, prefix/suffix, substring, trim, and word counts.
- `std.io`: buffered reader/writer surfaces, cursor writes, line scanning, and byte copy over caller-owned storage.
- `std.testing`: Bool-returning helpers for test blocks and byte-output checks.
- `std.log`: explicit-buffer JSON Lines record formatting.

Prefer `Maybe<T>` return checks over assuming an operation succeeded.

## Hosted Capabilities

These modules depend on host or runtime capabilities:

- `std.args`: process arguments
- `std.cli`: command-line flag and option helpers over process arguments
- `std.env`: process environment
- `std.fs`: hosted filesystem and explicit `Fs` or `owned<File>` handles
- `std.net`: bootstrap network handles
- `std.http`: HTTP request/response helpers
- `std.proc`: process execution helpers
- `World.out` and `World.err`: program output capabilities

Non-host targets may reject these APIs with target diagnostics. Inspect target facts before cross-building:

```sh
zero targets
zero check --target linux-musl-x64 <input>
zero inspect --target linux-musl-x64 <input>
```

Add `--json` only when a tool needs exact target facts or diagnostics.

## Memory Pattern

```zero
use std.mem

pub fn main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero")
    if std.mem.len(bytes) == 4 {
        check world.out.write("memory ok\n")
    }
}
```

For writable buffers, use caller-owned fixed arrays and `MutSpan<T>`:

```zero
pub fn main() -> Void {
    var storage: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    let writable: MutSpan<u8> = storage
    let copied: usize = std.mem.copy(writable, std.mem.span("zero"))
}
```

For non-byte scalar item storage, use the generic item helpers. Current direct
targets support `Bool`, `u8`, `u16`, `usize`, `i32`, `u32`, `i64`, and `u64`
elements for these helpers.

```zero
pub fn main() -> Void {
    var values: [4]i32 = [1, 2, 3, 4]
    var scratch: [4]i32 = [0, 0, 0, 0]
    let copied: usize = std.mem.copyItems(scratch, values)
    let prefix: Span<i32> = std.mem.prefix(scratch, 2)
    expect copied == 4 && std.mem.contains(prefix, 1)
}
```

Fixed-capacity collection helpers keep storage and length explicit:

```zero
pub fn main() -> Void {
    var values: [4]i32 = [0, 0, 0, 0]
    var len: usize = 0
    len = std.collections.push(values, len, 3)
    len = std.collections.push(values, len, 1)
    let live: Span<i32> = std.collections.view(values, len)
    expect std.collections.contains(values, len, 3) && std.mem.len(live) == 2
}
```

Use `std.sort` and `std.search` for common scalar algorithms instead of
hand-rolling loops:

```zero
pub fn main() -> Void {
    var values: [5]i32 = [5, 1, 4, 2, 3]
    std.sort.insertionI32(values)
    expect std.sort.isSortedI32(values)
    expect std.search.binaryI32(values, 4) == 3
}
```

String helpers are byte-oriented and allocation-free. `std.str.reverse` writes
into caller storage and requires that destination storage does not overlap the
input text:

```zero
pub fn main() -> Void {
    var reversed: [4]u8 = [0, 0, 0, 0]
    let out: Maybe<Span<u8>> = std.str.reverse(reversed, "zero")
    if out.has {
        expect std.mem.eql(out.value, "orez")
    }
}
```

Use `std.parse` and `std.fmt` instead of hand-rolled decimal loops in ordinary
CLIs and examples:

```zero
pub fn main() -> Void {
    let parsed: Maybe<i32> = std.parse.parseI32("-42")
    var out: [12]u8 = [0_u8; 12]
    if parsed.has {
        let formatted: Maybe<Span<u8>> = std.fmt.i32(out, parsed.value)
        expect formatted.has && std.mem.eql(formatted.value, "-42")
    }
}
```

Use codec, JSON, and URL helpers for common wire-format work instead of
hand-rolled loops:

```zero
pub fn main() -> Void {
    var decoded: [4]u8 = [0_u8; 4]
    let text: Maybe<Span<u8>> = std.codec.base64Decode(decoded, "emVybw==")

    let input: Span<u8> = "{\"count\":42,\"ok\":true}"
    let count: Maybe<u32> = std.json.u32(input, "count")

    var url_buf: [48]u8 = [0_u8; 48]
    var param_buf: [16]u8 = [0_u8; 16]
    let param: Maybe<Span<u8>> = std.url.writeQueryParam(param_buf, "q", "zero lang")
    var url: Maybe<Span<u8>> = null
    if param.has {
        url = std.url.appendQuery(url_buf, "https://example.com/path", param.value)
    }

    expect text.has && count.has && url.has
}
```

Use `std.math` checked helpers when overflow is a normal input outcome:

```zero
pub fn main() -> Void {
    let value: Maybe<u32> = std.math.checkedMulU32(6_u32, 7_u32)
    if value.has {
        expect value.value == 42_u32
    }
    expect std.math.sqrtFloorU32(99) == 9
}
```

Keep random sources explicit and durations typed:

```zero
pub fn main() -> Void {
    var rng: RandSource = std.rand.seed(7_u32)
    let first: u32 = std.rand.nextU32(&mut rng)
    let bit: Bool = std.rand.nextBool(&mut rng)
    let delay: Duration = std.time.add(std.time.ms(250), std.time.seconds(1))
    expect first == 1025555898_u32 && bit && std.time.asMsFloor(delay) == 1250
}
```

Use `std.testing` inside `expect` when the comparison shape matters to readers
or agents:

```zero
test "output shape" {
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
}
```

Use `std.log` as a caller-buffer formatter, then write the resulting span
through an explicit output capability:

```zero
pub fn main(world: World) -> Void raises {
    var storage: [128]u8 = [0_u8; 128]
    let entry: Maybe<Span<u8>> = std.log.keyValue(storage, "info", "event", "startup")
    if entry.has {
        check world.out.write(entry.value)
    }
}
```

## Function Signatures

This catalog is generated from the compiler's standard-library signature table. Use these names exactly; helpers with `T` are generic over the concrete span or item type inferred from the call.

### std.args

```text
len() -> usize
get(arg0: usize) -> Maybe<String>
has(arg0: usize) -> Bool
getOr(arg0: usize, arg1: String) -> String
find(arg0: String) -> Maybe<usize>
valueAfter(arg0: String) -> Maybe<String>
parseU32(arg0: usize) -> Maybe<u32>
```

### std.ascii

```text
digitValue(arg0: u8) -> Maybe<u8>
hexValue(arg0: u8) -> Maybe<u8>
isAlnum(arg0: u8) -> Bool
isAlpha(arg0: u8) -> Bool
isDigit(arg0: u8) -> Bool
isHexDigit(arg0: u8) -> Bool
isLower(arg0: u8) -> Bool
isUpper(arg0: u8) -> Bool
isWhitespace(arg0: u8) -> Bool
toLower(arg0: u8) -> u8
toUpper(arg0: u8) -> u8
```

### std.cli

```text
argEquals(arg0: usize, arg1: String) -> Bool
hasFlag(arg0: String) -> Bool
optionValue(arg0: String) -> Maybe<String>
optionValueOr(arg0: String, arg1: String) -> String
optionU32(arg0: String) -> Maybe<u32>
successExitCode() -> i32
usageExitCode() -> i32
```

### std.codec

```text
crc32(arg0: String) -> u32
crc32Bytes(arg0: Span<u8>) -> u32
encodedVarintLen(arg0: u32) -> usize
hexDecodedLen(arg0: Span<u8>) -> Maybe<usize>
hexDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64DecodedLen(arg0: Span<u8>) -> Maybe<usize>
base64Decode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
readU8(arg0: String) -> u8
readU16(arg0: String) -> u16
readU32(arg0: String) -> u32
writeU16(arg0: u32) -> u32
writeU32(arg0: u32) -> u32
readU16Le(arg0: Span<u8>) -> Maybe<u16>
readU16Be(arg0: Span<u8>) -> Maybe<u16>
readU32Le(arg0: Span<u8>) -> Maybe<u32>
readU32Be(arg0: Span<u8>) -> Maybe<u32>
writeU16Le(arg0: MutSpan<u8>, arg1: u16) -> Maybe<Span<u8>>
writeU16Be(arg0: MutSpan<u8>, arg1: u16) -> Maybe<Span<u8>>
writeU32Le(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
writeU32Be(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
varintEncode(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
varintDecode(arg0: Span<u8>) -> Maybe<u32>
base64EncodedLen(arg0: usize) -> usize
base64Encode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<String>
hexEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<String>
utf8Valid(arg0: Span<u8>) -> Bool
urlEncode(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
```

### std.collections

```text
append(storage: MutSpan<T>, len: usize, values: Span<T>) -> usize
contains(storage: Span<T>, len: usize, value: T) -> Bool
count(storage: Span<T>, len: usize, value: T) -> usize
moveToFront(storage: MutSpan<T>, len: usize, index: usize) -> usize
push(storage: MutSpan<T>, len: usize, value: T) -> usize
removeSwap(storage: MutSpan<T>, len: usize, index: usize) -> usize
view(storage: Span<T>, len: usize) -> Span<T>
```

### std.crypto

```text
hash32(arg0: Span<u8>) -> u32
hmac32(arg0: Span<u8>, arg1: Span<u8>) -> u32
constantTimeEql(arg0: Span<u8>, arg1: Span<u8>) -> Bool
secureRandomU32() -> u32
```

### std.env

```text
get(arg0: String) -> Maybe<String>
has(arg0: String) -> Bool
getOr(arg0: String, arg1: String) -> String
parseBool(arg0: String) -> Maybe<Bool>
parseU32(arg0: String) -> Maybe<u32>
```

### std.fmt

```text
bool(arg0: MutSpan<u8>, arg1: Bool) -> Maybe<Span<u8>>
hexLowerU32(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
i32(arg0: MutSpan<u8>, arg1: i32) -> Maybe<Span<u8>>
u32(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
usize(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
```

### std.fs

```text
host() -> Fs
open(arg0: Fs, arg1: String) -> Maybe<owned<File>>
openOrRaise(arg0: Fs, arg1: String) -> owned<File> raises [NotFound, TooLarge, Io]
create(arg0: Fs, arg1: String) -> Maybe<owned<File>>
createOrRaise(arg0: Fs, arg1: String) -> owned<File> raises [NotFound, TooLarge, Io]
read(arg0: String, arg1: MutSpan<u8>) -> usize
readOrRaise(arg0: mutref<File>, arg1: MutSpan<u8>) -> usize raises [NotFound, TooLarge, Io]
write(arg0: String, arg1: String) -> usize
writeAll(arg0: mutref<File>, arg1: Span<u8>) -> Bool
writeAllOrRaise(arg0: mutref<File>, arg1: Span<u8>) -> Void raises [NotFound, TooLarge, Io]
fileLenOrRaise(arg0: mutref<File>) -> usize raises [NotFound, TooLarge, Io]
readAll(allocator: Alloc, fs: Fs, path: String, max: usize) -> Maybe<owned<ByteBuf>>
readAllOrRaise(allocator: Alloc, fs: Fs, path: String, max: usize) -> owned<ByteBuf> raises [NotFound, TooLarge, Io]
exists(arg0: String) -> Bool
readBytes(arg0: String, arg1: MutSpan<u8>) -> Maybe<usize>
writeBytes(arg0: String, arg1: Span<u8>) -> Maybe<usize>
isDir(arg0: String) -> Bool
makeDir(arg0: String) -> Bool
removeDir(arg0: String) -> Bool
remove(arg0: String) -> Bool
rename(arg0: String, arg1: String) -> Bool
dirEntryCount(arg0: String) -> Maybe<usize>
tempName(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
atomicWrite(arg0: String, arg1: String, arg2: Span<u8>) -> Bool
fileLen(arg0: mutref<File>) -> Maybe<usize>
close(arg0: mutref<File>) -> Void
readFile(arg0: Fs, arg1: String, arg2: MutSpan<u8>) -> Maybe<usize>
writeFile(arg0: Fs, arg1: String, arg2: Span<u8>) -> Bool
copyFile(arg0: String, arg1: String, arg2: MutSpan<u8>) -> Bool
```

### std.http

```text
parseMethod(arg0: String) -> HttpMethod
client(arg0: Net) -> HttpClient
server(arg0: Net, arg1: Address) -> HttpServer
fetch(arg0: HttpClient, arg1: Span<u8>, arg2: MutSpan<u8>, arg3: Duration) -> HttpResult
resultOk(arg0: HttpResult) -> Bool
resultStatus(arg0: HttpResult) -> u16
resultBodyLen(arg0: HttpResult) -> usize
resultError(arg0: HttpResult) -> HttpError
errorNone() -> HttpError
errorInvalidUrl() -> HttpError
errorUnsupportedProtocol() -> HttpError
errorDns() -> HttpError
errorConnect() -> HttpError
errorTls() -> HttpError
errorTimeout() -> HttpError
errorTooLarge() -> HttpError
errorProviderUnavailable() -> HttpError
errorIo() -> HttpError
errorInvalidRequest() -> HttpError
responseLen(arg0: Span<u8>) -> usize
responseHeadersLen(arg0: Span<u8>) -> usize
responseBodyOffset(arg0: Span<u8>) -> usize
headerValue(arg0: Span<u8>, arg1: Span<u8>) -> HttpHeaderValue
headerFound(arg0: HttpHeaderValue) -> Bool
headerOffset(arg0: HttpHeaderValue) -> usize
headerLen(arg0: HttpHeaderValue) -> usize
tlsBoundary() -> String
statusReason(arg0: u16) -> String
statusIsInformational(arg0: u16) -> Bool
statusIsSuccess(arg0: u16) -> Bool
statusIsRedirect(arg0: u16) -> Bool
statusIsClientError(arg0: u16) -> Bool
statusIsServerError(arg0: u16) -> Bool
writeRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeJsonResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
requestMethodName(arg0: Span<u8>) -> Maybe<Span<u8>>
requestTarget(arg0: Span<u8>) -> Maybe<Span<u8>>
requestPath(arg0: Span<u8>) -> Maybe<Span<u8>>
requestQuery(arg0: Span<u8>) -> Maybe<Span<u8>>
requestQueryValue(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestHeader(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestBody(arg0: Span<u8>) -> Maybe<Span<u8>>
requestBodyWithin(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
requestMatches(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
headerBytes(arg0: Span<u8>, arg1: HttpHeaderValue) -> Maybe<Span<u8>>
responseBody(arg0: Span<u8>, arg1: HttpResult) -> Maybe<Span<u8>>
```

### std.io

```text
bufferedReader(arg0: MutSpan<u8>) -> BufferedReader
bufferedWriter(arg0: MutSpan<u8>) -> BufferedWriter
readerCapacity(arg0: ref<BufferedReader>) -> usize
writerCapacity(arg0: ref<BufferedWriter>) -> usize
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> usize
writeByte(arg0: MutSpan<u8>, arg1: usize, arg2: u8) -> Maybe<usize>
writeSpan(arg0: MutSpan<u8>, arg1: usize, arg2: Span<u8>) -> Maybe<usize>
written(arg0: Span<u8>, arg1: usize) -> Span<u8>
remaining(arg0: Span<u8>, arg1: usize) -> usize
nextLine(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
nextLineStart(arg0: Span<u8>, arg1: usize) -> usize
countLines(arg0: Span<u8>) -> usize
```

### std.json

```text
validate(arg0: String) -> Bool
validateBytes(arg0: Span<u8>) -> Bool
parse(allocator: Alloc, text: String) -> Maybe<JsonDoc>
parseBytes(allocator: Alloc, bytes: Span<u8>) -> Maybe<JsonDoc>
streamTokens(arg0: String) -> usize
streamTokensBytes(arg0: Span<u8>) -> usize
writeString(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
decodeBoundary() -> String
errorNone() -> u32
errorInvalid() -> u32
errorTrailing() -> u32
validateError(arg0: Span<u8>) -> u32
field(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
stringDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
string(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
u32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<u32>
bool(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Bool>
writeStringBytes(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeObject1String(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeObject1U32(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: u32) -> Maybe<Span<u8>>
writeObject1Bool(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Bool) -> Maybe<Span<u8>>
```

### std.toml

```text
validate(arg0: String) -> Bool
validateBytes(arg0: Span<u8>) -> Bool
field(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
stringDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
string(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
u32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<u32>
bool(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Bool>
```

### std.log

```text
message(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
keyValue(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
```

### std.math

```text
absI32(arg0: i32) -> u32
absI64(arg0: i64) -> u64
binomialU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedAddI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedAddU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedAddUsize(arg0: usize, arg1: usize) -> Maybe<usize>
checkedLcmU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedMulI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedMulU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedMulUsize(arg0: usize, arg1: usize) -> Maybe<usize>
checkedPowU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedSubI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedSubU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedSubUsize(arg0: usize, arg1: usize) -> Maybe<usize>
clampI32(arg0: i32, arg1: i32, arg2: i32) -> i32
clampI64(arg0: i64, arg1: i64, arg2: i64) -> i64
clampU32(arg0: u32, arg1: u32, arg2: u32) -> u32
clampU64(arg0: u64, arg1: u64, arg2: u64) -> u64
clampUsize(arg0: usize, arg1: usize, arg2: usize) -> usize
divisorCountU32(arg0: u32) -> u32
factorialU32(arg0: u32) -> Maybe<u32>
gcdU32(arg0: u32, arg1: u32) -> u32
isEvenU32(arg0: u32) -> Bool
isOddU32(arg0: u32) -> Bool
isPrimeU32(arg0: u32) -> Bool
lcmU32(arg0: u32, arg1: u32) -> u32
maxI32(arg0: i32, arg1: i32) -> i32
maxI64(arg0: i64, arg1: i64) -> i64
maxU32(arg0: u32, arg1: u32) -> u32
maxU64(arg0: u64, arg1: u64) -> u64
maxUsize(arg0: usize, arg1: usize) -> usize
minI32(arg0: i32, arg1: i32) -> i32
minI64(arg0: i64, arg1: i64) -> i64
minU32(arg0: u32, arg1: u32) -> u32
minU64(arg0: u64, arg1: u64) -> u64
minUsize(arg0: usize, arg1: usize) -> usize
modPowU32(arg0: u32, arg1: u32, arg2: u32) -> u32
powU32(arg0: u32, arg1: u32) -> u32
properDivisorSumU32(arg0: u32) -> u32
saturatingAddI32(arg0: i32, arg1: i32) -> i32
saturatingAddU32(arg0: u32, arg1: u32) -> u32
saturatingAddUsize(arg0: usize, arg1: usize) -> usize
saturatingMulI32(arg0: i32, arg1: i32) -> i32
saturatingMulU32(arg0: u32, arg1: u32) -> u32
saturatingMulUsize(arg0: usize, arg1: usize) -> usize
saturatingSubI32(arg0: i32, arg1: i32) -> i32
saturatingSubU32(arg0: u32, arg1: u32) -> u32
saturatingSubUsize(arg0: usize, arg1: usize) -> usize
sqrtFloorU32(arg0: u32) -> u32
```

### std.mem

```text
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> usize
copyItems(dst: MutSpan<T>, src: Span<T>) -> usize
fill(arg0: MutSpan<u8>, arg1: u8) -> usize
fillItems(dst: MutSpan<T>, value: T) -> usize
eql(arg0: String, arg1: String) -> Bool
span(arg0: String) -> Span<u8>
contains(items: Span<T>, value: T) -> Bool
isEmpty(items: Span<T>) -> Bool
prefix(items: Span<T>, len: usize) -> Span<T>
dropPrefix(items: Span<T>, len: usize) -> Span<T>
len(items: Span<T>) -> usize
get(items: Span<T>, index: usize) -> Maybe<T>
eqlBytes(left: Span<u8>, right: Span<u8>) -> Bool
nullAlloc() -> NullAlloc
fixedBufAlloc(arg0: MutSpan<u8>) -> FixedBufAlloc
arena(arg0: MutSpan<u8>) -> FixedBufAlloc
pageAlloc() -> PageAlloc
generalAlloc() -> GeneralAlloc
allocBytes(allocator: Alloc, len: usize) -> Maybe<MutSpan<u8>>
byteBuf(allocator: Alloc, capacity: usize) -> Maybe<owned<ByteBuf>>
vec(arg0: MutSpan<u8>) -> Vec
vecPush(arg0: mutref<Vec>, arg1: u8) -> Bool
vecLen(arg0: ref<Vec>) -> usize
vecCapacity(arg0: ref<Vec>) -> usize
bufBytes(arg0: ref<ByteBuf>) -> MutSpan<u8>
bufLen(arg0: ref<ByteBuf>) -> usize
reset(arg0: mutref<FixedBufAlloc>) -> Void
capacity(arg0: FixedBufAlloc) -> usize
```

### std.net

```text
host() -> Net
address(arg0: String, arg1: u16) -> Address
dnsName(arg0: Address) -> String
connect(arg0: Net, arg1: Address) -> Maybe<Conn>
listen(arg0: Net, arg1: Address) -> Maybe<Listener>
withTimeout(arg0: Address, arg1: Duration) -> Address
localhost(arg0: u16) -> Address
loopback(arg0: u16) -> Address
```

### std.parse

```text
isAsciiDigit(arg0: Span<u8>) -> Bool
isAsciiAlpha(arg0: Span<u8>) -> Bool
isIdentifierStart(arg0: Span<u8>) -> Bool
isWhitespace(arg0: Span<u8>) -> Bool
scanDigits(arg0: Span<u8>) -> usize
scanIdentifier(arg0: Span<u8>) -> usize
scanUntilByte(arg0: Span<u8>, arg1: u8) -> usize
scanWhitespace(arg0: Span<u8>) -> usize
tokenAscii(arg0: Span<u8>) -> Span<u8>
parseBool(arg0: Span<u8>) -> Maybe<Bool>
parseI32(arg0: Span<u8>) -> Maybe<i32>
parseU8(arg0: Span<u8>) -> Maybe<u8>
parseU16(arg0: Span<u8>) -> Maybe<u16>
parseU32(arg0: Span<u8>) -> Maybe<u32>
parseUsize(arg0: Span<u8>) -> Maybe<usize>
```

### std.path

```text
basename(arg0: String) -> String
dirname(arg0: String) -> String
extension(arg0: String) -> String
join(arg0: MutSpan<u8>, arg1: String, arg2: String) -> Maybe<String>
normalize(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
relative(arg0: MutSpan<u8>, arg1: String, arg2: String) -> Maybe<String>
```

### std.proc

```text
spawn(arg0: String) -> ProcStatus
exitCode(arg0: ProcStatus) -> i32
succeeded(arg0: ProcStatus) -> Bool
failed(arg0: ProcStatus) -> Bool
```

### std.rand

```text
seed(arg0: u32) -> RandSource
nextU32(arg0: mutref<RandSource>) -> u32
nextBool(arg0: mutref<RandSource>) -> Bool
entropyU32() -> u32
entropySeed() -> RandSource
```

### std.search

```text
binaryI32(arg0: Span<i32>, arg1: i32) -> usize
binaryU32(arg0: Span<u32>, arg1: u32) -> usize
binaryUsize(arg0: Span<usize>, arg1: usize) -> usize
indexOf(items: Span<T>, value: T) -> usize
lastIndexOf(items: Span<T>, value: T) -> usize
lowerBoundI32(arg0: Span<i32>, arg1: i32) -> usize
lowerBoundU32(arg0: Span<u32>, arg1: u32) -> usize
lowerBoundUsize(arg0: Span<usize>, arg1: usize) -> usize
```

### std.sort

```text
insertionI32(arg0: MutSpan<i32>) -> Void
insertionU32(arg0: MutSpan<u32>) -> Void
insertionUsize(arg0: MutSpan<usize>) -> Void
isSortedI32(arg0: Span<i32>) -> Bool
isSortedU32(arg0: Span<u32>) -> Bool
isSortedUsize(arg0: Span<usize>) -> Bool
```

### std.str

```text
contains(arg0: Span<u8>, arg1: Span<u8>) -> Bool
concat(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
count(arg0: Span<u8>, arg1: Span<u8>) -> usize
countByte(arg0: Span<u8>, arg1: u8) -> usize
eqlIgnoreAsciiCase(arg0: Span<u8>, arg1: Span<u8>) -> Bool
endsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
indexOf(arg0: Span<u8>, arg1: Span<u8>) -> usize
lastIndexOf(arg0: Span<u8>, arg1: Span<u8>) -> usize
repeat(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
reverse(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
startsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
toLowerAscii(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
toUpperAscii(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
trimAscii(arg0: Span<u8>) -> Span<u8>
trimEndAscii(arg0: Span<u8>) -> Span<u8>
trimStartAscii(arg0: Span<u8>) -> Span<u8>
wordCountAscii(arg0: Span<u8>) -> usize
```

### std.testing

```text
isTrue(arg0: Bool) -> Bool
isFalse(arg0: Bool) -> Bool
equalBool(arg0: Bool, arg1: Bool) -> Bool
equalUsize(arg0: usize, arg1: usize) -> Bool
equalU32(arg0: u32, arg1: u32) -> Bool
equalI32(arg0: i32, arg1: i32) -> Bool
equalBytes(arg0: Span<u8>, arg1: Span<u8>) -> Bool
containsBytes(arg0: Span<u8>, arg1: Span<u8>) -> Bool
startsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
endsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
```

### std.text

```text
isAscii(arg0: Span<u8>) -> Bool
utf8Len(arg0: Span<u8>) -> Maybe<usize>
utf8Valid(arg0: Span<u8>) -> Bool
```

### std.time

```text
ns(arg0: i64) -> Duration
us(arg0: i64) -> Duration
ms(arg0: i32) -> Duration
seconds(arg0: i32) -> Duration
minutes(arg0: i32) -> Duration
hours(arg0: i32) -> Duration
zero() -> Duration
add(arg0: Duration, arg1: Duration) -> Duration
monotonic() -> Duration
wallSeconds() -> i64
sub(arg0: Duration, arg1: Duration) -> Duration
asNs(arg0: Duration) -> i64
asUsFloor(arg0: Duration) -> i64
asMsFloor(arg0: Duration) -> i32
asSecondsFloor(arg0: Duration) -> i64
min(arg0: Duration, arg1: Duration) -> Duration
max(arg0: Duration, arg1: Duration) -> Duration
clamp(arg0: Duration, arg1: Duration, arg2: Duration) -> Duration
lessThan(arg0: Duration, arg1: Duration) -> Bool
isZero(arg0: Duration) -> Bool
```

### std.url

```text
percentEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
percentDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
queryEscape(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
queryUnescape(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
scheme(arg0: Span<u8>) -> Maybe<Span<u8>>
authority(arg0: Span<u8>) -> Maybe<Span<u8>>
host(arg0: Span<u8>) -> Maybe<Span<u8>>
path(arg0: Span<u8>) -> Span<u8>
query(arg0: Span<u8>) -> Maybe<Span<u8>>
queryValue(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeQueryParam(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
appendQuery(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
```

## Maybe Pattern

```zero
pub fn main(world: World) -> Void raises {
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        check world.out.write(first.value)
    }
}
```

Use the CLI helpers for exact flag and option conventions before writing a
custom argument loop:

```zero
pub fn main(world: World) -> Void raises {
    let name: String = std.cli.optionValueOr("--name", "zero")
    let count: Maybe<u32> = std.cli.optionU32("--count")
    if std.cli.hasFlag("--json") && count.has {
        check world.out.write(name)
    }
}
```

Use `check maybeValue` only when absence should propagate as a failure in a fallible function.
Read `maybeValue.value` only inside a visible `if maybeValue.has { ... }` guard.

## HTTP Pattern

Use the request/response envelope helpers instead of hand-building byte
headers when possible. `std.http.writeRequest` and
`std.http.writeJsonRequest` take a start line such as `"GET /health"` or
`"POST https://example.com/api"` and write into caller-owned storage.

```zero
pub fn main() -> Void {
    var request_buf: [128]u8 = [0_u8; 128]
    let request: Maybe<Span<u8>> = std.http.writeJsonRequest(request_buf, "POST /users", "{\"id\":7}")
    expect request.has
}
```

For API-style handlers, parse the request envelope with `std.http.requestMatches`,
`std.http.requestQueryValue`, `std.http.requestHeader`, and
`std.http.requestBodyWithin`, then write responses with
`std.http.writeJsonResponse`:

```zero
pub fn main() -> Void {
    let request: Span<u8> = "POST /users?tenant=demo\ncontent-type: application/json\n\n{\"id\":7}"
    var response_buf: [192]u8 = [0_u8; 192]
    let body: Maybe<Span<u8>> = std.http.requestBodyWithin(request, 64)
    let tenant: Maybe<Span<u8>> = std.http.requestQueryValue(request, "tenant")
    if std.http.requestMatches(request, "POST", "/users") && tenant.has && body.has {
        let response: Maybe<Span<u8>> = std.http.writeJsonResponse(response_buf, 201_u16, "{\"created\":true}")
        expect response.has
    }
}
```

For hosted client calls, keep the network capability explicit and read response
bytes through `std.http.responseBody`. Use `std.http.headerBytes` when a
header value from `std.http.headerValue` must be borrowed as a span.

## Resource Pattern

Hosted file APIs can use explicit handles:

```zero
pub fn main(world: World) -> Void raises {
    let fs: Fs = std.fs.host()
    if std.fs.writeFile(fs, ".zero/out/log.txt", "hello\n") {
        check world.out.write("wrote\n")
    }
}
```

Owned resources are deterministic. Do not invent hidden heap, global logger, or ambient filesystem APIs.
