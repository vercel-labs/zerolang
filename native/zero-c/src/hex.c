#include "hex.h"

#include <string.h>

static int hex_digit_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

bool z_hex_read(const char* hex, size_t byte_len, unsigned char* out) {
  size_t text_len = strlen(hex ? hex : "");
  if (text_len != byte_len * 2) return false;
  for (size_t i = 0; i < byte_len; i++) {
    int hi = hex_digit_value(hex[i * 2]);
    int lo = hex_digit_value(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return true;
}

void z_hex_write(const unsigned char* bytes, size_t len, char* out) {
  static const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = digits[(bytes[i] >> 4) & 0xf];
    out[i * 2 + 1] = digits[bytes[i] & 0xf];
  }
  out[len * 2] = 0;
}
