#include "zero_runtime.h"

#include <errno.h>

#if defined(_WIN32)
#include <io.h>
typedef int ZeroWriteResult;
#define ZERO_RUNTIME_WRITE _write
#else
#include <unistd.h>
typedef ssize_t ZeroWriteResult;
#define ZERO_RUNTIME_WRITE write
#endif

int zero_world_write(int fd, const char *buf, unsigned len) {
  if (len == 0) return 0;
  if (!buf) return 1;
  const char *cursor = buf;
  unsigned remaining = len;
  while (remaining > 0) {
    ZeroWriteResult written = ZERO_RUNTIME_WRITE(fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      return 1;
    }
    if (written == 0) return 1;
    cursor += (unsigned)written;
    remaining -= (unsigned)written;
  }
  return 0;
}

typedef struct {
  const unsigned char *ptr;
  size_t len;
  size_t pos;
  size_t tokens;
} ZeroJsonScanner;

static void zero_json_skip_ws(ZeroJsonScanner *scanner) {
  while (scanner->pos < scanner->len) {
    unsigned char ch = scanner->ptr[scanner->pos];
    if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') return;
    scanner->pos++;
  }
}

static int zero_json_parse_value(ZeroJsonScanner *scanner, unsigned depth);

static int zero_json_parse_string(ZeroJsonScanner *scanner) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '"') return 0;
  scanner->pos++;
  while (scanner->pos < scanner->len) {
    unsigned char ch = scanner->ptr[scanner->pos++];
    if (ch == '"') return 1;
    if (ch < 0x20u) return 0;
    if (ch != '\\') continue;
    if (scanner->pos >= scanner->len) return 0;
    unsigned char esc = scanner->ptr[scanner->pos++];
    if (esc == '"' || esc == '\\' || esc == '/' || esc == 'b' || esc == 'f' || esc == 'n' || esc == 'r' || esc == 't') continue;
    if (esc != 'u' || scanner->pos + 4 > scanner->len) return 0;
    for (unsigned i = 0; i < 4; i++) {
      unsigned char hex = scanner->ptr[scanner->pos++];
      int ok = (hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F');
      if (!ok) return 0;
    }
  }
  return 0;
}

static int zero_json_match_literal(ZeroJsonScanner *scanner, const char *literal) {
  size_t start = scanner->pos;
  for (size_t i = 0; literal[i]; i++) {
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != (unsigned char)literal[i]) {
      scanner->pos = start;
      return 0;
    }
    scanner->pos++;
  }
  return 1;
}

static int zero_json_parse_number(ZeroJsonScanner *scanner) {
  size_t start = scanner->pos;
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '-') scanner->pos++;
  if (scanner->pos >= scanner->len) {
    scanner->pos = start;
    return 0;
  }
  if (scanner->ptr[scanner->pos] == '0') {
    scanner->pos++;
  } else if (scanner->ptr[scanner->pos] >= '1' && scanner->ptr[scanner->pos] <= '9') {
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
  } else {
    scanner->pos = start;
    return 0;
  }
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '.') {
    scanner->pos++;
    size_t digits = scanner->pos;
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
    if (scanner->pos == digits) {
      scanner->pos = start;
      return 0;
    }
  }
  if (scanner->pos < scanner->len && (scanner->ptr[scanner->pos] == 'e' || scanner->ptr[scanner->pos] == 'E')) {
    scanner->pos++;
    if (scanner->pos < scanner->len && (scanner->ptr[scanner->pos] == '+' || scanner->ptr[scanner->pos] == '-')) scanner->pos++;
    size_t digits = scanner->pos;
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
    if (scanner->pos == digits) {
      scanner->pos = start;
      return 0;
    }
  }
  return 1;
}

static int zero_json_parse_array(ZeroJsonScanner *scanner, unsigned depth) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '[') return 0;
  scanner->tokens++;
  scanner->pos++;
  zero_json_skip_ws(scanner);
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == ']') {
    scanner->pos++;
    return 1;
  }
  for (;;) {
    if (!zero_json_parse_value(scanner, depth + 1)) return 0;
    zero_json_skip_ws(scanner);
    if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == ']') {
      scanner->pos++;
      return 1;
    }
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ',') return 0;
    scanner->pos++;
    zero_json_skip_ws(scanner);
  }
}

static int zero_json_parse_object(ZeroJsonScanner *scanner, unsigned depth) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '{') return 0;
  scanner->tokens++;
  scanner->pos++;
  zero_json_skip_ws(scanner);
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '}') {
    scanner->pos++;
    return 1;
  }
  for (;;) {
    if (!zero_json_parse_string(scanner)) return 0;
    scanner->tokens++;
    zero_json_skip_ws(scanner);
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ':') return 0;
    scanner->pos++;
    if (!zero_json_parse_value(scanner, depth + 1)) return 0;
    zero_json_skip_ws(scanner);
    if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '}') {
      scanner->pos++;
      return 1;
    }
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ',') return 0;
    scanner->pos++;
    zero_json_skip_ws(scanner);
  }
}

static int zero_json_parse_value(ZeroJsonScanner *scanner, unsigned depth) {
  if (depth > 64) return 0;
  zero_json_skip_ws(scanner);
  if (scanner->pos >= scanner->len) return 0;
  unsigned char ch = scanner->ptr[scanner->pos];
  if (ch == '{') return zero_json_parse_object(scanner, depth);
  if (ch == '[') return zero_json_parse_array(scanner, depth);
  if (ch == '"') {
    scanner->tokens++;
    return zero_json_parse_string(scanner);
  }
  if (ch == 't') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "true");
  }
  if (ch == 'f') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "false");
  }
  if (ch == 'n') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "null");
  }
  if (ch == '-' || (ch >= '0' && ch <= '9')) {
    scanner->tokens++;
    return zero_json_parse_number(scanner);
  }
  return 0;
}

int64_t zero_json_parse_bytes(ZeroByteView input) {
  if (!input.ptr || input.len == 0) return -1;
  ZeroJsonScanner scanner = {input.ptr, input.len, 0, 0};
  if (!zero_json_parse_value(&scanner, 0)) return -1;
  zero_json_skip_ws(&scanner);
  if (scanner.pos != scanner.len || scanner.tokens > INT64_MAX) return -1;
  return (int64_t)scanner.tokens;
}

static int zero_http_header_token_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
         ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
         ch == '`' || ch == '|' || ch == '~';
}

static unsigned char zero_http_header_lower(unsigned char ch) {
  return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

static int zero_http_header_name_equal(const unsigned char *ptr, size_t len, ZeroByteView name) {
  if (!ptr || !name.ptr || len != name.len || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char a = ptr[i];
    unsigned char b = name.ptr[i];
    if (!zero_http_header_token_char(b)) return 0;
    if (zero_http_header_lower(a) != zero_http_header_lower(b)) return 0;
  }
  return 1;
}

static uint64_t zero_http_header_pack(size_t offset, size_t len) {
  if (offset > 0x7fffffffu || len > 0xffffffffu) return 0;
  return (1ull << 63) | ((uint64_t)(uint32_t)offset << 32) | (uint64_t)(uint32_t)len;
}

static uint32_t zero_http_read_u32le(const unsigned char *ptr) {
  return (uint32_t)ptr[0] |
         ((uint32_t)ptr[1] << 8) |
         ((uint32_t)ptr[2] << 16) |
         ((uint32_t)ptr[3] << 24);
}

static int zero_http_response_has_meta(ZeroByteView response) {
  if (!response.ptr || response.len < ZERO_HTTP_RESPONSE_META_BYTES) return 0;
  return response.ptr[0] == 'Z' &&
         response.ptr[1] == 'H' &&
         response.ptr[2] == 'R' &&
         response.ptr[3] == '1';
}

static size_t zero_http_raw_header_len(ZeroByteView response) {
  if (!response.ptr || response.len == 0) return 0;
  size_t pos = 0;
  while (pos < response.len) {
    size_t line_start = pos;
    while (pos < response.len && response.ptr[pos] != '\n') pos++;
    size_t line_end = pos;
    if (line_end > line_start && response.ptr[line_end - 1] == '\r') line_end--;
    size_t next = pos < response.len ? pos + 1 : pos;
    if (line_end == line_start) return next;
    pos = next;
  }
  return response.len;
}

uint32_t zero_http_response_len(ZeroByteView response) {
  if (!zero_http_response_has_meta(response)) {
    return response.len > 0xffffffffu ? 0xffffffffu : (uint32_t)response.len;
  }
  uint32_t headers_len = zero_http_read_u32le(response.ptr + 8);
  uint32_t body_len = zero_http_read_u32le(response.ptr + 12);
  if (headers_len > 0xffffffffu - body_len) return 0xffffffffu;
  return headers_len + body_len;
}

uint32_t zero_http_response_headers_len(ZeroByteView response) {
  if (zero_http_response_has_meta(response)) return zero_http_read_u32le(response.ptr + 8);
  size_t len = zero_http_raw_header_len(response);
  return len > 0xffffffffu ? 0xffffffffu : (uint32_t)len;
}

uint32_t zero_http_response_body_offset(ZeroByteView response) {
  if (zero_http_response_has_meta(response)) {
    uint32_t headers_len = zero_http_read_u32le(response.ptr + 8);
    return ZERO_HTTP_RESPONSE_META_BYTES + headers_len;
  }
  size_t offset = zero_http_raw_header_len(response);
  return offset > 0xffffffffu ? 0xffffffffu : (uint32_t)offset;
}

uint64_t zero_http_header_value(ZeroByteView headers, ZeroByteView name) {
  if (!headers.ptr || !name.ptr || headers.len == 0 || name.len == 0) return 0;
  for (size_t i = 0; i < name.len; i++) {
    if (!zero_http_header_token_char(name.ptr[i])) return 0;
  }

  size_t header_offset = 0;
  size_t header_len = headers.len;
  if (zero_http_response_has_meta(headers)) {
    header_offset = ZERO_HTTP_RESPONSE_META_BYTES;
    header_len = zero_http_read_u32le(headers.ptr + 8);
    if (header_offset > headers.len || header_len > headers.len - header_offset) return 0;
  }

  size_t pos = header_offset;
  size_t limit = header_offset + header_len;
  while (pos < limit) {
    size_t line_start = pos;
    while (pos < limit && headers.ptr[pos] != '\n') pos++;
    size_t line_end = pos;
    if (line_end > line_start && headers.ptr[line_end - 1] == '\r') line_end--;
    if (pos < limit) pos++;

    if (line_end == line_start) break;
    size_t colon = line_start;
    while (colon < line_end && headers.ptr[colon] != ':') colon++;
    if (colon == line_start || colon >= line_end) continue;
    if (!zero_http_header_name_equal(headers.ptr + line_start, colon - line_start, name)) continue;

    size_t value_start = colon + 1;
    while (value_start < line_end && (headers.ptr[value_start] == ' ' || headers.ptr[value_start] == '\t')) value_start++;
    size_t value_end = line_end;
    while (value_end > value_start && (headers.ptr[value_end - 1] == ' ' || headers.ptr[value_end - 1] == '\t')) value_end--;
    return zero_http_header_pack(value_start, value_end - value_start);
  }
  return 0;
}

uint32_t zero_http_header_found(uint64_t value) {
  return (value >> 63) ? 1u : 0u;
}

uint32_t zero_http_header_offset(uint64_t value) {
  return zero_http_header_found(value) ? (uint32_t)((value >> 32) & 0x7fffffffu) : 0u;
}

uint32_t zero_http_header_len(uint64_t value) {
  return zero_http_header_found(value) ? (uint32_t)(value & 0xffffffffu) : 0u;
}

uint32_t zero_http_result_ok(uint64_t result) {
  uint32_t status = zero_http_result_status(result);
  return zero_http_result_error(result) == ZERO_HTTP_OK && status >= 200 && status < 300 ? 1u : 0u;
}

uint32_t zero_http_result_status(uint64_t result) {
  return (uint32_t)((result >> 32) & 0xffffu);
}

uint32_t zero_http_result_body_len(uint64_t result) {
  return (uint32_t)(result & 0xffffffffu);
}

uint32_t zero_http_result_error(uint64_t result) {
  return (uint32_t)((result >> 48) & 0xffffu);
}

int64_t zero_hex_encode(ZeroMutByteView destination, ZeroByteView source) {
  if (destination.len < source.len * 2) return -1;
  static const char hex_chars[] = "0123456789abcdef";
  for (size_t i = 0; i < source.len; i++) {
    unsigned char val = source.ptr[i];
    destination.ptr[i * 2] = hex_chars[val >> 4];
    destination.ptr[i * 2 + 1] = hex_chars[val & 0x0f];
  }
  return (int64_t)(source.len * 2);
}

uint32_t zero_utf8_valid(ZeroByteView bytes) {
  if (!bytes.ptr) return 0;
  size_t i = 0;
  while (i < bytes.len) {
    unsigned char c = bytes.ptr[i];
    if (c <= 0x7f) {
      i++;
    } else if ((c & 0xe0) == 0xc0) {
      if (i + 1 >= bytes.len) return 0;
      if ((bytes.ptr[i + 1] & 0xc0) != 0x80) return 0;
      if (c < 0xc2) return 0;
      i += 2;
    } else if ((c & 0xf0) == 0xe0) {
      if (i + 2 >= bytes.len) return 0;
      if ((bytes.ptr[i + 1] & 0xc0) != 0x80) return 0;
      if ((bytes.ptr[i + 2] & 0xc0) != 0x80) return 0;
      if (c == 0xe0 && bytes.ptr[i + 1] < 0xa0) return 0;
      if (c == 0xed && bytes.ptr[i + 1] >= 0xa0) return 0;
      i += 3;
    } else if ((c & 0xf8) == 0xf0) {
      if (i + 3 >= bytes.len) return 0;
      if ((bytes.ptr[i + 1] & 0xc0) != 0x80) return 0;
      if ((bytes.ptr[i + 2] & 0xc0) != 0x80) return 0;
      if ((bytes.ptr[i + 3] & 0xc0) != 0x80) return 0;
      if (c == 0xf0 && bytes.ptr[i + 1] < 0x90) return 0;
      if (c == 0xf4 && bytes.ptr[i + 1] >= 0x90) return 0;
      if (c > 0xf4) return 0;
      i += 4;
    } else {
      return 0;
    }
  }
  return 1;
}
