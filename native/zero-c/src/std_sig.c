#include "std_sig.h"

#include <stdio.h>
#include <string.h>

const ZStdHelperInfo z_std_helpers[] = {
  {"std.args.len", "usize", 0, {NULL}, {NULL}, "args", "host", "borrows process argv", true},
  {"std.args.get", "Maybe<String>", 1, {"usize"}, {NULL}, "args", "host", "borrows process argv", true},
  {"std.env.get", "Maybe<String>", 1, {"String"}, {NULL}, "env", "host", "borrows process environment", true},
  {"std.path.basename", "String", 1, {"String"}, {NULL}, "path", "target-neutral", "borrows input path", true},
  {"std.path.dirname", "String", 1, {"String"}, {NULL}, "path", "target-neutral", "borrows/static path view", true},
  {"std.path.extension", "String", 1, {"String"}, {NULL}, "path", "target-neutral", "borrows input path", true},
  {"std.path.join", "Maybe<String>", 3, {"MutSpan<u8>", "String", "String"}, {NULL}, "path", "target-neutral", "writes caller buffer", true},
  {"std.path.normalize", "Maybe<String>", 2, {"MutSpan<u8>", "String"}, {NULL}, "path", "target-neutral", "writes caller buffer", true},
  {"std.path.relative", "Maybe<String>", 3, {"MutSpan<u8>", "String", "String"}, {NULL}, "path", "target-neutral", "writes caller buffer", true},
  {"std.io.bufferedReader", "BufferedReader", 1, {"MutSpan<u8>"}, {NULL}, "memory", "target-neutral", "uses caller buffer", true},
  {"std.io.bufferedWriter", "BufferedWriter", 1, {"MutSpan<u8>"}, {NULL}, "memory", "target-neutral", "uses caller buffer", true},
  {"std.io.readerCapacity", "usize", 1, {"ref<BufferedReader>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.io.writerCapacity", "usize", 1, {"ref<BufferedWriter>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.io.copy", "usize", 2, {"MutSpan<u8>", "Span<u8>"}, {NULL}, "memory", "target-neutral", "writes caller buffer", true},
  {"std.codec.crc32", "u32", 1, {"String"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.codec.crc32Bytes", "u32", 1, {"Span<u8>"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.codec.encodedVarintLen", "usize", 1, {"u32"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.codec.readU8", "u8", 1, {"String"}, {NULL}, "codec", "target-neutral", "little-endian byte read", true},
  {"std.codec.readU16", "u16", 1, {"String"}, {NULL}, "codec", "target-neutral", "little-endian byte read", true},
  {"std.codec.readU32", "u32", 1, {"String"}, {NULL}, "codec", "target-neutral", "little-endian byte read", true},
  {"std.codec.writeU16", "u32", 1, {"u32"}, {NULL}, "codec", "target-neutral", "little-endian byte write primitive", true},
  {"std.codec.writeU32", "u32", 1, {"u32"}, {NULL}, "codec", "target-neutral", "little-endian byte write primitive", true},
  {"std.codec.base64EncodedLen", "usize", 1, {"usize"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.codec.base64Encode", "Maybe<String>", 2, {"MutSpan<u8>", "Span<u8>"}, {NULL}, "codec", "target-neutral", "writes caller buffer", true},
  {"std.codec.hexEncode", "Maybe<String>", 2, {"MutSpan<u8>", "Span<u8>"}, {NULL}, "codec", "target-neutral", "writes caller buffer", true},
  {"std.codec.utf8Valid", "Bool", 1, {"Span<u8>"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.codec.urlEncode", "Maybe<String>", 2, {"MutSpan<u8>", "String"}, {NULL}, "codec", "target-neutral", "writes caller buffer", true},
  {"std.mem.copy", "usize", 2, {"MutSpan<u8>", "Span<u8>"}, {NULL}, "memory", "target-neutral", "writes caller buffer", true},
  {"std.mem.fill", "usize", 2, {"MutSpan<u8>", "u8"}, {NULL}, "memory", "target-neutral", "writes caller buffer", true},
  {"std.mem.eql", "Bool", 2, {"String", "String"}, {NULL}, "memory", "target-neutral", "no allocation", true},
  {"std.mem.span", "Span<u8>", 1, {"String"}, {NULL}, "memory", "target-neutral", "borrows input bytes", true},
  {"std.mem.len", "usize", 1, {NULL}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.get", "Maybe<T>", 2, {NULL, "usize"}, {NULL}, "memory", "target-neutral", "bounds-checked indexed read", false},
  {"std.mem.eqlBytes", "Bool", 2, {NULL}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.nullAlloc", "NullAlloc", 0, {NULL}, {NULL}, "alloc", "target-neutral", "never allocates", true},
  {"std.mem.fixedBufAlloc", "FixedBufAlloc", 1, {"MutSpan<u8>"}, {NULL}, "alloc", "target-neutral", "uses caller buffer", true},
  {"std.mem.arena", "FixedBufAlloc", 1, {"MutSpan<u8>"}, {NULL}, "alloc", "target-neutral", "bulk allocation over caller buffer", true},
  {"std.mem.pageAlloc", "PageAlloc", 0, {NULL}, {NULL}, "alloc", "host", "explicit page allocator handle", false},
  {"std.mem.generalAlloc", "GeneralAlloc", 0, {NULL}, {NULL}, "alloc", "host", "explicit general allocator handle", false},
  {"std.mem.allocBytes", "Maybe<MutSpan<u8>>", 2, {NULL, "usize"}, {NULL}, "alloc", "target-neutral", "uses explicit allocator only", true},
  {"std.mem.byteBuf", "Maybe<owned<ByteBuf>>", 2, {NULL, "usize"}, {NULL}, "alloc", "target-neutral", "uses explicit allocator only", true},
  {"std.mem.vec", "Vec", 1, {"MutSpan<u8>"}, {NULL}, "memory", "target-neutral", "uses caller storage", true},
  {"std.mem.vecPush", "Bool", 2, {"mutref<Vec>", "u8"}, {NULL}, "memory", "target-neutral", "writes caller storage", true},
  {"std.mem.vecLen", "usize", 1, {"ref<Vec>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.vecCapacity", "usize", 1, {"ref<Vec>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.bufBytes", "MutSpan<u8>", 1, {"ref<ByteBuf>"}, {NULL}, "memory", "target-neutral", "borrows owned buffer", false},
  {"std.mem.bufLen", "usize", 1, {"ref<ByteBuf>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.reset", "Void", 1, {"mutref<FixedBufAlloc>"}, {NULL}, "alloc", "target-neutral", "resets explicit allocator", true},
  {"std.mem.capacity", "usize", 1, {"FixedBufAlloc"}, {NULL}, "alloc", "target-neutral", "no allocation", false},
  {"std.mem.mapEmpty", "Map", 0, {NULL}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.mapLen", "usize", 1, {"ref<Map>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.setEmpty", "Set", 0, {NULL}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.mem.setLen", "usize", 1, {"ref<Set>"}, {NULL}, "memory", "target-neutral", "no allocation", false},
  {"std.parse.isAsciiDigit", "Bool", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.isAsciiAlpha", "Bool", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.isIdentifierStart", "Bool", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.isWhitespace", "Bool", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.scanDigits", "usize", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.scanIdentifier", "usize", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.parseU8", "Maybe<u8>", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.parseU16", "Maybe<u16>", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.parse.parseU32", "Maybe<u32>", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.json.validate", "Bool", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.json.validateBytes", "Bool", 1, {"Span<u8>"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.json.parse", "Maybe<JsonDoc>", 2, {NULL, "String"}, {NULL}, "alloc", "target-neutral", "uses explicit allocator only", true},
  {"std.json.parseBytes", "Maybe<JsonDoc>", 2, {NULL, "Span<u8>"}, {NULL}, "alloc", "target-neutral", "uses explicit allocator only", true},
  {"std.json.streamTokens", "usize", 1, {"String"}, {NULL}, "parse", "target-neutral", "streaming token count", true},
  {"std.json.streamTokensBytes", "usize", 1, {"Span<u8>"}, {NULL}, "parse", "target-neutral", "streaming token count", true},
  {"std.json.writeString", "Maybe<String>", 2, {"MutSpan<u8>", "String"}, {NULL}, "parse", "target-neutral", "writes caller buffer", true},
  {"std.json.decodeBoundary", "String", 0, {NULL}, {NULL}, "parse", "target-neutral", "typed decode boundary metadata", false},
  {"std.time.ms", "Duration", 1, {"i32"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.seconds", "Duration", 1, {"i32"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.add", "Duration", 2, {"Duration", "Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.monotonic", "Duration", 0, {NULL}, {NULL}, "time", "host", "no allocation", true},
  {"std.time.wallSeconds", "i64", 0, {NULL}, {NULL}, "time", "host", "no allocation", true},
  {"std.time.sub", "Duration", 2, {"Duration", "Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.asMsFloor", "i32", 1, {"Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.min", "Duration", 2, {"Duration", "Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.max", "Duration", 2, {"Duration", "Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.time.lessThan", "Bool", 2, {"Duration", "Duration"}, {NULL}, "time", "target-neutral", "no allocation", true},
  {"std.rand.seed", "RandSource", 1, {"u32"}, {NULL}, "rand", "target-neutral", "deterministic test source", true},
  {"std.rand.nextU32", "u32", 1, {"mutref<RandSource>"}, {NULL}, "rand", "target-neutral", "updates explicit source", true},
  {"std.rand.entropyU32", "u32", 0, {NULL}, {NULL}, "rand", "host", "target entropy source", true},
  {"std.proc.spawn", "ProcStatus", 1, {"String"}, {NULL}, "proc", "host", "explicit process capability", true},
  {"std.proc.exitCode", "i32", 1, {"ProcStatus"}, {NULL}, "proc", "host", "no allocation", false},
  {"std.crypto.hash32", "u32", 1, {"Span<u8>"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.crypto.hmac32", "u32", 2, {"Span<u8>", "Span<u8>"}, {NULL}, "codec", "target-neutral", "no allocation", true},
  {"std.crypto.constantTimeEql", "Bool", 2, {"Span<u8>", "Span<u8>"}, {NULL}, "memory", "target-neutral", "no allocation", true},
  {"std.crypto.secureRandomU32", "u32", 0, {NULL}, {NULL}, "rand", "host", "target entropy source", true},
  {"std.net.host", "Net", 0, {NULL}, {NULL}, "net", "host", "explicit network capability handle", false},
  {"std.net.address", "Address", 2, {"String", "u16"}, {NULL}, "net", "target-neutral", "no allocation", true},
  {"std.net.dnsName", "String", 1, {"Address"}, {NULL}, "net", "target-neutral", "borrows address host", false},
  {"std.net.connect", "Maybe<Conn>", 2, {"Net", "Address"}, {NULL}, "net", "host", "no allocation; returns unopened bootstrap handle", true},
  {"std.net.listen", "Maybe<Listener>", 2, {"Net", "Address"}, {NULL}, "net", "host", "no allocation; returns unopened bootstrap handle", true},
  {"std.net.withTimeout", "Address", 2, {"Address", "Duration"}, {NULL}, "net", "target-neutral", "no allocation", true},
  {"std.http.parseMethod", "HttpMethod", 1, {"String"}, {NULL}, "parse", "target-neutral", "no allocation", true},
  {"std.http.client", "HttpClient", 1, {"Net"}, {NULL}, "net", "host", "borrows network capability", true},
  {"std.http.server", "HttpServer", 2, {"Net", "Address"}, {NULL}, "net", "host", "borrows network capability", true},
  {"std.http.fetch", "HttpResult", 4, {"HttpClient", "Span<u8>", "MutSpan<u8>", "Duration"}, {NULL}, "net", "host-runtime", "writes caller response buffer from raw HTTP request envelope", true},
  {"std.http.resultOk", "Bool", 1, {"HttpResult"}, {NULL}, "net", "host-runtime", "inspects HTTP result metadata", true},
  {"std.http.resultStatus", "u16", 1, {"HttpResult"}, {NULL}, "net", "host-runtime", "reads HTTP status metadata", true},
  {"std.http.resultBodyLen", "usize", 1, {"HttpResult"}, {NULL}, "net", "host-runtime", "reads response body length metadata", true},
  {"std.http.resultError", "HttpError", 1, {"HttpResult"}, {NULL}, "net", "host-runtime", "reads transport error metadata", true},
  {"std.http.errorNone", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorInvalidUrl", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorUnsupportedProtocol", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorDns", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorConnect", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorTls", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorTimeout", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorTooLarge", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorProviderUnavailable", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorIo", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.errorInvalidRequest", "HttpError", 0, {NULL}, {NULL}, "none", "target-neutral", "no allocation", false},
  {"std.http.responseLen", "usize", 1, {"Span<u8>"}, {NULL}, "memory", "host-runtime", "reads response bytes written into caller storage", true},
  {"std.http.responseHeadersLen", "usize", 1, {"Span<u8>"}, {NULL}, "memory", "host-runtime", "reads response header byte length", true},
  {"std.http.responseBodyOffset", "usize", 1, {"Span<u8>"}, {NULL}, "memory", "host-runtime", "reads body start offset within caller response storage", true},
  {"std.http.headerValue", "HttpHeaderValue", 2, {"Span<u8>", "Span<u8>"}, {NULL}, "memory", "host-runtime", "locates a response header value by name", true},
  {"std.http.headerFound", "Bool", 1, {"HttpHeaderValue"}, {NULL}, "memory", "host-runtime", "inspects header-value metadata", true},
  {"std.http.headerOffset", "usize", 1, {"HttpHeaderValue"}, {NULL}, "memory", "host-runtime", "reads header-value offset metadata", true},
  {"std.http.headerLen", "usize", 1, {"HttpHeaderValue"}, {NULL}, "memory", "host-runtime", "reads header-value length metadata", true},
  {"std.http.tlsBoundary", "String", 0, {NULL}, {NULL}, "net", "host", "declares platform-or-C-library TLS boundary", false},
  {"std.fs.host", "Fs", 0, {NULL}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.open", "Maybe<owned<File>>", 2, {"Fs", "String"}, {NULL}, "fs", "host", "owned file handle", true},
  {"std.fs.openOrRaise", "owned<File>", 2, {"Fs", "String"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "owned file handle", true},
  {"std.fs.create", "Maybe<owned<File>>", 2, {"Fs", "String"}, {NULL}, "fs", "host", "owned file handle", true},
  {"std.fs.createOrRaise", "owned<File>", 2, {"Fs", "String"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "owned file handle", true},
  {"std.fs.read", "usize", 2, {"String", "MutSpan<u8>"}, {NULL}, "fs", "host", "writes caller buffer", true},
  {"std.fs.readOrRaise", "usize", 2, {"mutref<File>", "MutSpan<u8>"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "writes caller buffer", true},
  {"std.fs.write", "usize", 2, {"String", "String"}, {NULL}, "fs", "host", "writes bytes to path", true},
  {"std.fs.writeAll", "Bool", 2, {"mutref<File>", "Span<u8>"}, {NULL}, "fs", "host", "writes file resource bytes", true},
  {"std.fs.writeAllOrRaise", "Void", 2, {"mutref<File>", "Span<u8>"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "no allocation", true},
  {"std.fs.fileLenOrRaise", "usize", 1, {"mutref<File>"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "no allocation", true},
  {"std.fs.readAll", "Maybe<owned<ByteBuf>>", 4, {NULL, "Fs", "String", "usize"}, {NULL}, "fs", "host", "uses explicit allocator only", true},
  {"std.fs.readAllOrRaise", "owned<ByteBuf>", 4, {NULL, "Fs", "String", "usize"}, {"NotFound", "TooLarge", "Io"}, "fs", "host", "uses explicit allocator only", true},
  {"std.fs.exists", "Bool", 1, {"String"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.readBytes", "Maybe<usize>", 2, {"String", "MutSpan<u8>"}, {NULL}, "fs", "host", "writes caller buffer", true},
  {"std.fs.writeBytes", "Maybe<usize>", 2, {"String", "Span<u8>"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.isDir", "Bool", 1, {"String"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.makeDir", "Bool", 1, {"String"}, {NULL}, "fs", "host", "creates a directory", true},
  {"std.fs.removeDir", "Bool", 1, {"String"}, {NULL}, "fs", "host", "removes a directory", true},
  {"std.fs.remove", "Bool", 1, {"String"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.rename", "Bool", 2, {"String", "String"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.dirEntryCount", "Maybe<usize>", 1, {"String"}, {NULL}, "fs", "host", "directory traversal", true},
  {"std.fs.tempName", "Maybe<String>", 2, {"MutSpan<u8>", "String"}, {NULL}, "fs", "host", "writes caller buffer", true},
  {"std.fs.atomicWrite", "Bool", 3, {"String", "String", "Span<u8>"}, {NULL}, "fs", "host", "uses caller-provided temp path", true},
  {"std.fs.fileLen", "Maybe<usize>", 1, {"mutref<File>"}, {NULL}, "fs", "host", "no allocation", true},
  {"std.fs.close", "Void", 1, {"mutref<File>"}, {NULL}, "fs", "host", "closes owned file handle", true},
  {NULL, NULL, 0, {NULL}, {NULL}, NULL, NULL, NULL, false},
};

const ZStdHelperInfo *z_std_helper_find(const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; z_std_helpers[i].name; i++) {
    if (strcmp(z_std_helpers[i].name, name) == 0) return &z_std_helpers[i];
  }
  return NULL;
}

const char *z_std_helper_arg_type(const char *name, size_t index) {
  if (index >= Z_STD_HELPER_MAX_ARGS) return NULL;
  const ZStdHelperInfo *helper = z_std_helper_find(name);
  return helper ? helper->arg_types[index] : NULL;
}

const char *z_std_helper_error_name(const ZStdHelperInfo *helper, size_t index) {
  if (!helper || index >= Z_STD_HELPER_MAX_ERRORS) return NULL;
  return helper->error_names[index];
}

bool z_std_helper_is_fallible(const ZStdHelperInfo *helper) {
  return z_std_helper_error_name(helper, 0) != NULL;
}

void z_std_helper_error_set_text(const ZStdHelperInfo *helper, char *buf, size_t cap) {
  if (!buf || cap == 0) return;
  snprintf(buf, cap, "![");
  size_t used = strlen(buf);
  for (size_t i = 0; i < Z_STD_HELPER_MAX_ERRORS; i++) {
    const char *error_name = z_std_helper_error_name(helper, i);
    if (!error_name) break;
    if (used >= cap - 1) break;
    snprintf(buf + used, cap - used, "%s%s", used > 2 ? " " : "", error_name);
    used = strlen(buf);
  }
  if (used < cap - 1) snprintf(buf + used, cap - used, "]");
  else buf[cap - 1] = '\0';
}

int z_std_helper_index(const char *name, size_t max_helpers) {
  if (!name) return -1;
  for (size_t i = 0; z_std_helpers[i].name && i < max_helpers; i++) {
    if (strcmp(z_std_helpers[i].name, name) == 0) return (int)i;
  }
  return -1;
}
