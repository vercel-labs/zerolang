#include "zero_runtime.h"

#include <errno.h>
#include <string.h>

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

uint64_t zero_parse_u32(ZeroByteView text) {
  if ((!text.ptr && text.len > 0) || text.len == 0) return 0;
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (byte < '0' || byte > '9') return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > (UINT32_MAX - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  return (1ull << 32) | total;
}

uint64_t zero_parse_i32(ZeroByteView text) {
  if ((!text.ptr && text.len > 0) || text.len == 0) return 0;
  size_t index = 0;
  uint32_t limit = 2147483647u;
  uint32_t negative = 0;
  if (text.ptr[0] == '-') {
    negative = 1;
    limit = 2147483648u;
    index = 1;
  } else if (text.ptr[0] == '+') {
    index = 1;
  }
  if (index == text.len) return 0;
  uint64_t total = 0;
  for (; index < text.len; index++) {
    unsigned char byte = text.ptr[index];
    if (byte < '0' || byte > '9') return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > ((uint64_t)limit - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  int32_t value = negative ? (total == 2147483648ull ? INT32_MIN : -(int32_t)total) : (int32_t)total;
  return (1ull << 32) | (uint32_t)value;
}

uint32_t zero_fmt_u32(ZeroMutByteView buffer, uint32_t value) {
  if (!buffer.ptr) return 0;
  unsigned char tmp[10];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint32_t zero_fmt_bool(ZeroMutByteView buffer, uint32_t value) {
  static const unsigned char true_text[] = {'t', 'r', 'u', 'e'};
  static const unsigned char false_text[] = {'f', 'a', 'l', 's', 'e'};
  const unsigned char *text = value ? true_text : false_text;
  size_t len = value ? sizeof(true_text) : sizeof(false_text);
  if (!buffer.ptr || len > buffer.len) return 0;
  memcpy(buffer.ptr, text, len);
  return (uint32_t)len;
}

uint32_t zero_fmt_hex_lower_u32(ZeroMutByteView buffer, uint32_t value) {
  if (!buffer.ptr) return 0;
  static const unsigned char digits[] = "0123456789abcdef";
  unsigned char tmp[8];
  size_t len = 0;
  do {
    tmp[len++] = digits[value & 0xfu];
    value >>= 4;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint32_t zero_fmt_i32(ZeroMutByteView buffer, int32_t value) {
  if (!buffer.ptr) return 0;
  if (value >= 0) return zero_fmt_u32(buffer, (uint32_t)value);
  if (buffer.len == 0) return 0;
  buffer.ptr[0] = '-';
  uint32_t magnitude = value == INT32_MIN ? 2147483648u : (uint32_t)(-value);
  ZeroMutByteView rest = {buffer.ptr + 1, buffer.len - 1};
  uint32_t written = zero_fmt_u32(rest, magnitude);
  return written ? written + 1 : 0;
}

uint32_t zero_fmt_usize(ZeroMutByteView buffer, uint64_t value) {
  if (!buffer.ptr) return 0;
  unsigned char tmp[20];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint64_t zero_args_find(uint64_t argc, const char *const *argv, ZeroByteView name) {
  if (!argv || (!name.ptr && name.len > 0)) return 0;
  for (uint64_t i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) continue;
    size_t len = strlen(arg);
    if (len == name.len && memcmp(arg, name.ptr, len) == 0) {
      return (1ull << 32) | (i & 0xffffffffull);
    }
  }
  return 0;
}

uint32_t zero_ascii_op(uint32_t byte, uint32_t op) {
  byte &= 0xffu;
  uint32_t is_digit = byte >= '0' && byte <= '9';
  uint32_t is_lower = byte >= 'a' && byte <= 'z';
  uint32_t is_upper = byte >= 'A' && byte <= 'Z';
  uint32_t is_alpha = is_lower || is_upper;
  switch (op) {
    case ZERO_ASCII_OP_IS_DIGIT:
      return is_digit;
    case ZERO_ASCII_OP_IS_LOWER:
      return is_lower;
    case ZERO_ASCII_OP_IS_UPPER:
      return is_upper;
    case ZERO_ASCII_OP_IS_ALPHA:
      return is_alpha;
    case ZERO_ASCII_OP_IS_ALNUM:
      return is_alpha || is_digit;
    case ZERO_ASCII_OP_IS_WHITESPACE:
      return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
    case ZERO_ASCII_OP_IS_HEX_DIGIT:
      return is_digit || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
    case ZERO_ASCII_OP_TO_LOWER:
      return is_upper ? byte + ('a' - 'A') : byte;
    case ZERO_ASCII_OP_TO_UPPER:
      return is_lower ? byte - ('a' - 'A') : byte;
    case ZERO_ASCII_OP_DIGIT_VALUE:
      return is_digit ? 0x100u | (byte - '0') : 0;
    case ZERO_ASCII_OP_HEX_VALUE:
      if (is_digit) return 0x100u | (byte - '0');
      if (byte >= 'A' && byte <= 'F') return 0x100u | (byte - 'A' + 10u);
      if (byte >= 'a' && byte <= 'f') return 0x100u | (byte - 'a' + 10u);
      return 0;
    default:
      return 0;
  }
}

static int zero_str_view_valid(ZeroByteView view) {
  return view.ptr || view.len == 0;
}

static uint32_t zero_str_some_len(size_t len) {
  if (len > UINT32_MAX - 1u) return 0;
  return (uint32_t)(len + 1u);
}

static unsigned char zero_str_lower(unsigned char byte) {
  return byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte + ('a' - 'A')) : byte;
}

static unsigned char zero_str_upper(unsigned char byte) {
  return byte >= 'a' && byte <= 'z' ? (unsigned char)(byte - ('a' - 'A')) : byte;
}

static int zero_str_ascii_space(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

static size_t zero_text_utf8_sequence_len(unsigned char first) {
  if (first < 128u) return 1;
  if (first >= 194u && first <= 223u) return 2;
  if (first >= 224u && first <= 239u) return 3;
  if (first >= 240u && first <= 244u) return 4;
  return 0;
}

static int zero_text_is_cont(unsigned char byte) {
  return byte >= 128u && byte <= 191u;
}

static uint64_t zero_text_utf8_len_encoded(ZeroByteView text, int count_codepoints) {
  if (!zero_str_view_valid(text)) return 0;
  size_t index = 0;
  uint64_t count = 0;
  while (index < text.len) {
    unsigned char first = text.ptr[index];
    size_t width = zero_text_utf8_sequence_len(first);
    if (width == 0 || index + width > text.len) return 0;
    if (width == 2) {
      if (!zero_text_is_cont(text.ptr[index + 1])) return 0;
    } else if (width == 3) {
      unsigned char second = text.ptr[index + 1];
      if (!zero_text_is_cont(second) || !zero_text_is_cont(text.ptr[index + 2])) return 0;
      if (first == 224u && second < 160u) return 0;
      if (first == 237u && second > 159u) return 0;
    } else if (width == 4) {
      unsigned char second = text.ptr[index + 1];
      if (!zero_text_is_cont(second) || !zero_text_is_cont(text.ptr[index + 2]) || !zero_text_is_cont(text.ptr[index + 3])) return 0;
      if (first == 240u && second < 144u) return 0;
      if (first == 244u && second > 143u) return 0;
    }
    index += width;
    count++;
  }
  if (!count_codepoints) return 1;
  if (count == UINT64_MAX) return 0;
  return count + 1u;
}

uint64_t zero_text_op(ZeroByteView text, uint32_t op) {
  switch (op) {
    case ZERO_TEXT_OP_IS_ASCII:
      if (!zero_str_view_valid(text)) return 0;
      for (size_t i = 0; i < text.len; i++) {
        if (text.ptr[i] >= 128u) return 0;
      }
      return 1;
    case ZERO_TEXT_OP_UTF8_VALID:
      return zero_text_utf8_len_encoded(text, 0);
    case ZERO_TEXT_OP_UTF8_LEN:
      return zero_text_utf8_len_encoded(text, 1);
    default:
      return 0;
  }
}

static int zero_parse_ascii_digit(unsigned char byte) {
  return byte >= '0' && byte <= '9';
}

static int zero_parse_ascii_alpha(unsigned char byte) {
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static int zero_parse_identifier_start(unsigned char byte) {
  return zero_parse_ascii_alpha(byte) || byte == '_';
}

static int zero_parse_identifier_byte(unsigned char byte) {
  return zero_parse_identifier_start(byte) || zero_parse_ascii_digit(byte);
}

static uint64_t zero_parse_u32_max(ZeroByteView text, uint32_t max) {
  if (!zero_str_view_valid(text) || text.len == 0) return 0;
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (!zero_parse_ascii_digit(byte)) return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > ((uint64_t)max - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  return (1ull << 32) | total;
}

uint64_t zero_parse_op(ZeroByteView text, uint32_t arg, uint32_t op) {
  if (!zero_str_view_valid(text)) return 0;
  unsigned char first = text.len > 0 ? text.ptr[0] : 0;
  switch (op) {
    case ZERO_PARSE_OP_IS_ASCII_DIGIT:
      return text.len > 0 && zero_parse_ascii_digit(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_ASCII_ALPHA:
      return text.len > 0 && zero_parse_ascii_alpha(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_IDENTIFIER_START:
      return text.len > 0 && zero_parse_identifier_start(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_WHITESPACE:
      return text.len > 0 && zero_str_ascii_space(first) ? 1 : 0;
    case ZERO_PARSE_OP_SCAN_DIGITS: {
      size_t index = 0;
      while (index < text.len && zero_parse_ascii_digit(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_SCAN_IDENTIFIER: {
      if (text.len == 0 || !zero_parse_identifier_start(first)) return 0;
      size_t index = 1;
      while (index < text.len && zero_parse_identifier_byte(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_SCAN_UNTIL_BYTE: {
      unsigned char target = (unsigned char)(arg & 0xffu);
      for (size_t i = 0; i < text.len; i++) {
        if (text.ptr[i] == target) return i;
      }
      return text.len;
    }
    case ZERO_PARSE_OP_SCAN_WHITESPACE: {
      size_t index = 0;
      while (index < text.len && zero_str_ascii_space(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_PARSE_BOOL:
      if (text.len == 4 && memcmp(text.ptr, "true", 4) == 0) return (1ull << 32) | 1u;
      if (text.len == 5 && memcmp(text.ptr, "false", 5) == 0) return (1ull << 32);
      return 0;
    case ZERO_PARSE_OP_PARSE_U8:
      return zero_parse_u32_max(text, UINT8_MAX);
    case ZERO_PARSE_OP_PARSE_U16:
      return zero_parse_u32_max(text, UINT16_MAX);
    default:
      return 0;
  }
}

ZeroMaybeUsize zero_parse_usize(ZeroByteView text) {
  if (!zero_str_view_valid(text) || text.len == 0) return (ZeroMaybeUsize){0, 0};
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (!zero_parse_ascii_digit(byte)) return (ZeroMaybeUsize){0, 0};
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > (UINT64_MAX - digit) / 10u) return (ZeroMaybeUsize){0, 0};
    total = total * 10u + digit;
  }
  return (ZeroMaybeUsize){1, total};
}

static int64_t zero_time_floor_div(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  int64_t remainder = value % divisor;
  if (value < 0 && remainder != 0) return quotient - 1;
  return quotient;
}

int64_t zero_time_op(int64_t a, int64_t b, int64_t c, uint32_t op) {
  switch (op) {
    case ZERO_TIME_OP_AS_US_FLOOR:
      return zero_time_floor_div(a, 1000);
    case ZERO_TIME_OP_AS_MS_FLOOR:
      return zero_time_floor_div(a, 1000000);
    case ZERO_TIME_OP_AS_SECONDS_FLOOR:
      return zero_time_floor_div(a, 1000000000);
    case ZERO_TIME_OP_MIN:
      return a < b ? a : b;
    case ZERO_TIME_OP_MAX:
      return a < b ? b : a;
    case ZERO_TIME_OP_CLAMP: {
      int64_t lower = b < c ? b : c;
      int64_t upper = b < c ? c : b;
      if (a < lower) return lower;
      if (upper < a) return upper;
      return a;
    }
    default:
      return 0;
  }
}

static uint64_t zero_math_clamp_u64(uint64_t value, uint64_t low, uint64_t high) {
  uint64_t lower = low < high ? low : high;
  uint64_t upper = low < high ? high : low;
  if (value < lower) return lower;
  if (upper < value) return upper;
  return value;
}

static int64_t zero_math_clamp_i64(int64_t value, int64_t low, int64_t high) {
  int64_t lower = low < high ? low : high;
  int64_t upper = low < high ? high : low;
  if (value < lower) return lower;
  if (upper < value) return upper;
  return value;
}

static uint64_t zero_math_some_u32(uint32_t value) {
  return (1ull << 32) | (uint64_t)value;
}

static ZeroMaybeUsize zero_math_some_usize(uint64_t value) {
  return (ZeroMaybeUsize){1, value};
}

static uint64_t zero_math_checked_add_u32(uint32_t left, uint32_t right) {
  if (left > UINT32_MAX - right) return 0;
  return zero_math_some_u32(left + right);
}

static uint64_t zero_math_checked_sub_u32(uint32_t left, uint32_t right) {
  if (left < right) return 0;
  return zero_math_some_u32(left - right);
}

static uint64_t zero_math_checked_mul_u32(uint32_t left, uint32_t right) {
  if (left != 0 && right > UINT32_MAX / left) return 0;
  return zero_math_some_u32(left * right);
}

static uint64_t zero_math_checked_add_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left + (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint64_t zero_math_checked_sub_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left - (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint64_t zero_math_checked_mul_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left * (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint32_t zero_math_gcd_u32(uint32_t a, uint32_t b) {
  while (b != 0) {
    uint32_t next = a % b;
    a = b;
    b = next;
  }
  return a;
}

static uint64_t zero_math_checked_lcm_u32(uint32_t left, uint32_t right) {
  if (left == 0 || right == 0) return zero_math_some_u32(0);
  uint32_t gcd = zero_math_gcd_u32(left, right);
  return zero_math_checked_mul_u32(left / gcd, right);
}

static uint64_t zero_math_checked_pow_u32(uint32_t base, uint32_t exponent) {
  uint32_t result = 1;
  uint32_t current = base;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) {
      uint64_t next_result = zero_math_checked_mul_u32(result, current);
      if ((next_result >> 32) == 0) return 0;
      result = (uint32_t)next_result;
    }
    exp = exp / 2;
    if (exp > 0) {
      uint64_t next_current = zero_math_checked_mul_u32(current, current);
      if ((next_current >> 32) == 0) return 0;
      current = (uint32_t)next_current;
    }
  }
  return zero_math_some_u32(result);
}

static uint32_t zero_math_pow_u32(uint32_t base, uint32_t exponent) {
  uint32_t result = 1;
  uint32_t current = base;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) result = result * current;
    current = current * current;
    exp = exp / 2;
  }
  return result;
}

static uint32_t zero_math_mod_pow_u32(uint32_t base, uint32_t exponent, uint32_t modulus) {
  if (modulus == 0) return 0;
  uint64_t wide_modulus = modulus;
  uint64_t result = 1ull % wide_modulus;
  uint64_t current = ((uint64_t)base) % wide_modulus;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) result = result * current % wide_modulus;
    current = current * current % wide_modulus;
    exp = exp / 2;
  }
  return (uint32_t)result;
}

static uint32_t zero_math_is_prime_u32(uint32_t value) {
  if (value < 2) return 0;
  if (value == 2) return 1;
  if (value % 2 == 0) return 0;
  uint32_t divisor = 3;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) return 0;
    divisor += 2;
  }
  return 1;
}

static uint32_t zero_math_sqrt_floor_u32(uint32_t value) {
  uint32_t low = 0;
  uint32_t high = 65536;
  while (low + 1 < high) {
    uint32_t mid = low + (high - low) / 2;
    if (mid <= value / mid) low = mid;
    else high = mid;
  }
  return low;
}

static uint64_t zero_math_factorial_u32(uint32_t value) {
  uint32_t result = 1;
  uint32_t next = 2;
  while (next <= value) {
    uint64_t multiplied = zero_math_checked_mul_u32(result, next);
    if ((multiplied >> 32) == 0) return 0;
    result = (uint32_t)multiplied;
    next++;
  }
  return zero_math_some_u32(result);
}

static uint64_t zero_math_binomial_u32(uint32_t n, uint32_t k0) {
  if (k0 > n) return 0;
  uint32_t k = k0;
  if (k > n - k) k = n - k;
  uint32_t result = 1;
  uint32_t i = 1;
  while (i <= k) {
    uint32_t numerator = n - k + i;
    uint32_t denominator = i;
    uint32_t numerator_gcd = zero_math_gcd_u32(numerator, denominator);
    numerator = numerator / numerator_gcd;
    denominator = denominator / numerator_gcd;
    uint32_t result_gcd = zero_math_gcd_u32(result, denominator);
    result = result / result_gcd;
    denominator = denominator / result_gcd;
    if (denominator != 1) return 0;
    uint64_t multiplied = zero_math_checked_mul_u32(result, numerator);
    if ((multiplied >> 32) == 0) return 0;
    result = (uint32_t)multiplied;
    i++;
  }
  return zero_math_some_u32(result);
}

static uint32_t zero_math_divisor_count_u32(uint32_t value) {
  if (value == 0) return 0;
  uint32_t count = 0;
  uint32_t divisor = 1;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) {
      uint32_t paired = value / divisor;
      count += paired == divisor ? 1u : 2u;
    }
    divisor++;
  }
  return count;
}

static uint32_t zero_math_proper_divisor_sum_u32(uint32_t value) {
  if (value <= 1) return 0;
  uint32_t sum = 1;
  uint32_t divisor = 2;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) {
      uint32_t paired = value / divisor;
      sum += divisor;
      if (paired != divisor) sum += paired;
    }
    divisor++;
  }
  return sum;
}

static ZeroMaybeUsize zero_math_checked_add_usize(uint64_t left, uint64_t right) {
  if (left > UINT64_MAX - right) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left + right);
}

static ZeroMaybeUsize zero_math_checked_sub_usize(uint64_t left, uint64_t right) {
  if (left < right) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left - right);
}

static ZeroMaybeUsize zero_math_checked_mul_usize(uint64_t left, uint64_t right) {
  if (left != 0 && right > UINT64_MAX / left) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left * right);
}

ZeroMaybeUsize zero_math_usize_op(uint64_t a, uint64_t b, uint64_t c, uint32_t op) {
  (void)c;
  switch (op) {
    case ZERO_MATH_OP_CHECKED_ADD_USIZE:
      return zero_math_checked_add_usize(a, b);
    case ZERO_MATH_OP_CHECKED_SUB_USIZE:
      return zero_math_checked_sub_usize(a, b);
    case ZERO_MATH_OP_CHECKED_MUL_USIZE:
      return zero_math_checked_mul_usize(a, b);
    default:
      return (ZeroMaybeUsize){0, 0};
  }
}

uint64_t zero_math_op(int64_t a, int64_t b, int64_t c, uint32_t op) {
  switch (op) {
    case ZERO_MATH_OP_MIN_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      return (uint64_t)(int64_t)(left < right ? left : right);
    }
    case ZERO_MATH_OP_MAX_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      return (uint64_t)(int64_t)(left > right ? left : right);
    }
    case ZERO_MATH_OP_CLAMP_I32: {
      int32_t value = (int32_t)a;
      int32_t low = (int32_t)b;
      int32_t high = (int32_t)c;
      int32_t lower = low < high ? low : high;
      int32_t upper = low < high ? high : low;
      if (value < lower) return (uint64_t)(int64_t)lower;
      if (upper < value) return (uint64_t)(int64_t)upper;
      return (uint64_t)(int64_t)value;
    }
    case ZERO_MATH_OP_MIN_I64:
      return (uint64_t)(a < b ? a : b);
    case ZERO_MATH_OP_MAX_I64:
      return (uint64_t)(a > b ? a : b);
    case ZERO_MATH_OP_CLAMP_I64:
      return (uint64_t)zero_math_clamp_i64(a, b, c);
    case ZERO_MATH_OP_MIN_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      return left < right ? left : right;
    }
    case ZERO_MATH_OP_MAX_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      return left > right ? left : right;
    }
    case ZERO_MATH_OP_CLAMP_U32:
      return (uint32_t)zero_math_clamp_u64((uint32_t)a, (uint32_t)b, (uint32_t)c);
    case ZERO_MATH_OP_MIN_U64:
    case ZERO_MATH_OP_MIN_USIZE: {
      uint64_t left = (uint64_t)a;
      uint64_t right = (uint64_t)b;
      return left < right ? left : right;
    }
    case ZERO_MATH_OP_MAX_U64:
    case ZERO_MATH_OP_MAX_USIZE: {
      uint64_t left = (uint64_t)a;
      uint64_t right = (uint64_t)b;
      return left > right ? left : right;
    }
    case ZERO_MATH_OP_CLAMP_U64:
    case ZERO_MATH_OP_CLAMP_USIZE:
      return zero_math_clamp_u64((uint64_t)a, (uint64_t)b, (uint64_t)c);
    case ZERO_MATH_OP_ABS_I32: {
      int32_t value = (int32_t)a;
      if (value == INT32_MIN) return 2147483648u;
      return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    }
    case ZERO_MATH_OP_ABS_I64:
      if (a == INT64_MIN) return 9223372036854775808ull;
      return a < 0 ? (uint64_t)(-a) : (uint64_t)a;
    case ZERO_MATH_OP_CHECKED_ADD_U32:
      return zero_math_checked_add_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_SUB_U32:
      return zero_math_checked_sub_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_MUL_U32:
      return zero_math_checked_mul_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_SATURATING_ADD_U32: {
      uint64_t result = zero_math_checked_add_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : UINT32_MAX;
    }
    case ZERO_MATH_OP_SATURATING_SUB_U32: {
      uint64_t result = zero_math_checked_sub_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : 0;
    }
    case ZERO_MATH_OP_SATURATING_MUL_U32: {
      uint64_t result = zero_math_checked_mul_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : UINT32_MAX;
    }
    case ZERO_MATH_OP_CHECKED_ADD_I32:
      return zero_math_checked_add_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_CHECKED_SUB_I32:
      return zero_math_checked_sub_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_CHECKED_MUL_I32:
      return zero_math_checked_mul_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_SATURATING_ADD_I32: {
      int64_t wide = (int64_t)(int32_t)a + (int64_t)(int32_t)b;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_SATURATING_SUB_I32: {
      int64_t wide = (int64_t)(int32_t)a - (int64_t)(int32_t)b;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_SATURATING_MUL_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      int64_t wide = (int64_t)left * (int64_t)right;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_GCD_U32:
      return zero_math_gcd_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_LCM_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      if (left == 0 || right == 0) return 0;
      return (uint32_t)((left / zero_math_gcd_u32(left, right)) * right);
    }
    case ZERO_MATH_OP_CHECKED_LCM_U32:
      return zero_math_checked_lcm_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_POW_U32:
      return zero_math_pow_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_POW_U32:
      return zero_math_checked_pow_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_MOD_POW_U32:
      return zero_math_mod_pow_u32((uint32_t)a, (uint32_t)b, (uint32_t)c);
    case ZERO_MATH_OP_IS_PRIME_U32:
      return zero_math_is_prime_u32((uint32_t)a);
    case ZERO_MATH_OP_SQRT_FLOOR_U32:
      return zero_math_sqrt_floor_u32((uint32_t)a);
    case ZERO_MATH_OP_FACTORIAL_U32:
      return zero_math_factorial_u32((uint32_t)a);
    case ZERO_MATH_OP_BINOMIAL_U32:
      return zero_math_binomial_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_DIVISOR_COUNT_U32:
      return zero_math_divisor_count_u32((uint32_t)a);
    case ZERO_MATH_OP_PROPER_DIVISOR_SUM_U32:
      return zero_math_proper_divisor_sum_u32((uint32_t)a);
    case ZERO_MATH_OP_SATURATING_ADD_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_add_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : UINT64_MAX;
    }
    case ZERO_MATH_OP_SATURATING_SUB_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_sub_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : 0;
    }
    case ZERO_MATH_OP_SATURATING_MUL_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_mul_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : UINT64_MAX;
    }
    default:
      return 0;
  }
}

static size_t zero_search_lower_bound_i32(const int32_t *items, size_t len, int32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_lower_bound_u32(const uint32_t *items, size_t len, uint32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_lower_bound_usize(const uint64_t *items, size_t len, uint64_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

uint64_t zero_search_op(ZeroByteView items, int64_t needle, uint32_t op) {
  if (!zero_str_view_valid(items)) return 0;
  switch (op) {
    case ZERO_SEARCH_OP_LOWER_BOUND_I32:
      return zero_search_lower_bound_i32((const int32_t *)items.ptr, items.len, (int32_t)needle);
    case ZERO_SEARCH_OP_BINARY_I32: {
      size_t index = zero_search_lower_bound_i32((const int32_t *)items.ptr, items.len, (int32_t)needle);
      return index < items.len && ((const int32_t *)items.ptr)[index] == (int32_t)needle ? index : items.len;
    }
    case ZERO_SEARCH_OP_LOWER_BOUND_U32:
      return zero_search_lower_bound_u32((const uint32_t *)items.ptr, items.len, (uint32_t)needle);
    case ZERO_SEARCH_OP_BINARY_U32: {
      size_t index = zero_search_lower_bound_u32((const uint32_t *)items.ptr, items.len, (uint32_t)needle);
      return index < items.len && ((const uint32_t *)items.ptr)[index] == (uint32_t)needle ? index : items.len;
    }
    case ZERO_SEARCH_OP_LOWER_BOUND_USIZE:
      return zero_search_lower_bound_usize((const uint64_t *)items.ptr, items.len, (uint64_t)needle);
    case ZERO_SEARCH_OP_BINARY_USIZE: {
      size_t index = zero_search_lower_bound_usize((const uint64_t *)items.ptr, items.len, (uint64_t)needle);
      return index < items.len && ((const uint64_t *)items.ptr)[index] == (uint64_t)needle ? index : items.len;
    }
    default:
      return 0;
  }
}

static void zero_sort_insertion_i32(int32_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    int32_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

static void zero_sort_insertion_u32(uint32_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    uint32_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

static void zero_sort_insertion_usize(uint64_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    uint64_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

void zero_sort_op(ZeroMutByteView items, uint32_t op) {
  if ((!items.ptr && items.len > 0)) return;
  switch (op) {
    case ZERO_SORT_OP_INSERTION_I32:
      zero_sort_insertion_i32((int32_t *)items.ptr, items.len);
      break;
    case ZERO_SORT_OP_INSERTION_U32:
      zero_sort_insertion_u32((uint32_t *)items.ptr, items.len);
      break;
    case ZERO_SORT_OP_INSERTION_USIZE:
      zero_sort_insertion_usize((uint64_t *)items.ptr, items.len);
      break;
    default:
      break;
  }
}

uint32_t zero_sort_is_sorted_op(ZeroByteView items, uint32_t op) {
  if (!zero_str_view_valid(items)) return 0;
  switch (op) {
    case ZERO_SORT_OP_IS_SORTED_I32: {
      const int32_t *values = (const int32_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    case ZERO_SORT_OP_IS_SORTED_U32: {
      const uint32_t *values = (const uint32_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    case ZERO_SORT_OP_IS_SORTED_USIZE: {
      const uint64_t *values = (const uint64_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    default:
      return 0;
  }
}

static int zero_str_eq_at(ZeroByteView text, size_t start, ZeroByteView needle) {
  if (start > text.len || needle.len > text.len - start) return 0;
  for (size_t i = 0; i < needle.len; i++) {
    if (text.ptr[start + i] != needle.ptr[i]) return 0;
  }
  return 1;
}

uint32_t zero_str_buffer_op(ZeroMutByteView buffer, ZeroByteView text, uint32_t op) {
  if (!buffer.ptr || !zero_str_view_valid(text) || text.len > buffer.len) return 0;
  uint32_t some_len = zero_str_some_len(text.len);
  if (!some_len) return 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    switch (op) {
      case ZERO_STR_OP_REVERSE:
        byte = text.ptr[text.len - 1u - i];
        break;
      case ZERO_STR_OP_COPY:
        break;
      case ZERO_STR_OP_TO_LOWER_ASCII:
        byte = zero_str_lower(byte);
        break;
      case ZERO_STR_OP_TO_UPPER_ASCII:
        byte = zero_str_upper(byte);
        break;
      default:
        return 0;
    }
    buffer.ptr[i] = byte;
  }
  return some_len;
}

uint32_t zero_str_concat(ZeroMutByteView buffer, ZeroByteView left, ZeroByteView right) {
  if (!buffer.ptr || !zero_str_view_valid(left) || !zero_str_view_valid(right)) return 0;
  if (left.len > buffer.len || right.len > buffer.len - left.len) return 0;
  uint32_t some_len = zero_str_some_len(left.len + right.len);
  if (!some_len) return 0;
  if (left.len > 0) memcpy(buffer.ptr, left.ptr, left.len);
  if (right.len > 0) memcpy(buffer.ptr + left.len, right.ptr, right.len);
  return some_len;
}

uint32_t zero_str_repeat(ZeroMutByteView buffer, ZeroByteView text, uint64_t count) {
  if (!buffer.ptr || !zero_str_view_valid(text)) return 0;
  if (text.len > 0 && count > (uint64_t)(buffer.len / text.len)) return 0;
  uint64_t total64 = (uint64_t)text.len * count;
  if (total64 > UINT32_MAX - 1u) return 0;
  size_t total = (size_t)total64;
  uint32_t some_len = zero_str_some_len(total);
  if (!some_len) return 0;
  size_t out = 0;
  for (uint64_t r = 0; r < count; r++) {
    if (text.len > 0) memcpy(buffer.ptr + out, text.ptr, text.len);
    out += text.len;
  }
  return some_len;
}

uint64_t zero_str_trim_op(ZeroByteView text, uint32_t op) {
  if (!zero_str_view_valid(text) || text.len > UINT32_MAX) return 0;
  size_t start = 0;
  size_t end = text.len;
  if (op == ZERO_STR_OP_PATH_BASENAME ||
      op == ZERO_STR_OP_PATH_DIRNAME ||
      op == ZERO_STR_OP_PATH_EXTENSION) {
    while (end > 0 && (text.ptr[end - 1u] == '/' || text.ptr[end - 1u] == '\\')) end--;
    if (op == ZERO_STR_OP_PATH_BASENAME) {
      start = end;
      while (start > 0 && text.ptr[start - 1u] != '/' && text.ptr[start - 1u] != '\\') start--;
      return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(end - start);
    }
    if (op == ZERO_STR_OP_PATH_DIRNAME) {
      size_t root_len = text.len > 0 && (text.ptr[0] == '/' || text.ptr[0] == '\\') ? 1u : 0u;
      if (root_len > 0 && end == 0) return (uint32_t)root_len;
      size_t split = end;
      while (split > 0 && text.ptr[split - 1u] != '/' && text.ptr[split - 1u] != '\\') split--;
      while (split > root_len && (text.ptr[split - 1u] == '/' || text.ptr[split - 1u] == '\\')) split--;
      return (uint32_t)split;
    }
    start = end;
    while (start > 0 && text.ptr[start - 1u] != '/' && text.ptr[start - 1u] != '\\') start--;
    size_t dot = end;
    size_t scan = end;
    while (scan > start) {
      scan--;
      if (text.ptr[scan] == '.') {
        dot = scan;
        scan = start;
      }
    }
    if (dot == end || dot == start) return ((uint64_t)(uint32_t)end << 32);
    return ((uint64_t)(uint32_t)(dot + 1u) << 32) | (uint32_t)(end - dot - 1u);
  }
  if (op == ZERO_STR_OP_PARSE_TOKEN_ASCII) {
    while (start < end && zero_str_ascii_space(text.ptr[start])) start++;
    size_t token_end = start;
    while (token_end < end && !zero_str_ascii_space(text.ptr[token_end])) token_end++;
    return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(token_end - start);
  }
  if (op == ZERO_STR_OP_TRIM_ASCII || op == ZERO_STR_OP_TRIM_START_ASCII) {
    while (start < end && zero_str_ascii_space(text.ptr[start])) start++;
  }
  if (op == ZERO_STR_OP_TRIM_ASCII || op == ZERO_STR_OP_TRIM_END_ASCII) {
    while (end > start && zero_str_ascii_space(text.ptr[end - 1u])) end--;
  }
  return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(end - start);
}

uint64_t zero_str_pair_op(ZeroByteView text, ZeroByteView needle, uint32_t op) {
  if (!zero_str_view_valid(text) || !zero_str_view_valid(needle)) return 0;
  switch (op) {
    case ZERO_STR_OP_STARTS_WITH:
      return needle.len <= text.len && zero_str_eq_at(text, 0, needle) ? 1 : 0;
    case ZERO_STR_OP_ENDS_WITH:
      return needle.len <= text.len && zero_str_eq_at(text, text.len - needle.len, needle) ? 1 : 0;
    case ZERO_STR_OP_CONTAINS:
      return zero_str_pair_op(text, needle, ZERO_STR_OP_INDEX_OF) != text.len || needle.len == 0 ? 1 : 0;
    case ZERO_STR_OP_INDEX_OF:
      if (needle.len == 0) return 0;
      if (needle.len > text.len) return text.len;
      for (size_t start = 0; start <= text.len - needle.len; start++) {
        if (zero_str_eq_at(text, start, needle)) return start;
      }
      return text.len;
    case ZERO_STR_OP_LAST_INDEX_OF:
      if (needle.len == 0 || needle.len > text.len) return text.len;
      for (size_t start = text.len - needle.len + 1u; start > 0; start--) {
        size_t candidate = start - 1u;
        if (zero_str_eq_at(text, candidate, needle)) return candidate;
      }
      return text.len;
    case ZERO_STR_OP_COUNT: {
      if (needle.len == 0) return text.len + 1u;
      size_t index = 0;
      uint64_t count = 0;
      while (index <= text.len) {
        ZeroByteView rest = { text.ptr ? text.ptr + index : NULL, text.len - index };
        uint64_t found = zero_str_pair_op(rest, needle, ZERO_STR_OP_INDEX_OF);
        if (found == rest.len) return count;
        count++;
        index += (size_t)found + needle.len;
      }
      return count;
    }
    case ZERO_STR_OP_EQL_IGNORE_ASCII_CASE:
      if (text.len != needle.len) return 0;
      for (size_t i = 0; i < text.len; i++) {
        if (zero_str_lower(text.ptr[i]) != zero_str_lower(needle.ptr[i])) return 0;
      }
      return 1;
    default:
      return 0;
  }
}

uint64_t zero_str_count_byte(ZeroByteView text, uint32_t byte) {
  if (!zero_str_view_valid(text)) return 0;
  uint64_t count = 0;
  unsigned char target = (unsigned char)(byte & 0xffu);
  for (size_t i = 0; i < text.len; i++) {
    if (text.ptr[i] == target) count++;
  }
  return count;
}

uint64_t zero_str_word_count_ascii(ZeroByteView text) {
  if (!zero_str_view_valid(text)) return 0;
  uint64_t count = 0;
  int in_word = 0;
  for (size_t i = 0; i < text.len; i++) {
    if (zero_str_ascii_space(text.ptr[i])) {
      in_word = 0;
    } else if (!in_word) {
      count++;
      in_word = 1;
    }
  }
  return count;
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

uint32_t zero_str_contains(ZeroByteView text, ZeroByteView needle) {
  return (uint32_t)zero_str_pair_op(text, needle, ZERO_STR_OP_CONTAINS);
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

static uint64_t zero_http_span_pack(size_t offset, size_t len) {
  if (offset > 0xffffffffu || len > 0x7fffffffu) return 0;
  return (1ull << 63) | ((uint64_t)(uint32_t)len << 32) | (uint64_t)(uint32_t)offset;
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

static int zero_http_token_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
         ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
         ch == '`' || ch == '|' || ch == '~';
}

static int zero_http_method_valid(ZeroByteView method) {
  if (!method.ptr || method.len == 0) return 0;
  for (size_t i = 0; i < method.len; i++) {
    if (!zero_http_token_char(method.ptr[i])) return 0;
  }
  return 1;
}

static ZeroByteView zero_http_trim_trailing_cr(ZeroByteView view) {
  if (view.len > 0 && view.ptr[view.len - 1] == '\r') view.len--;
  return view;
}

static int zero_http_first_line(ZeroByteView request, ZeroByteView *line_out) {
  if (line_out) *line_out = (ZeroByteView){0};
  if (!request.ptr || request.len == 0) return 0;
  size_t line_end = 0;
  while (line_end < request.len && request.ptr[line_end] != '\n') line_end++;
  if (line_end >= request.len) return 0;
  ZeroByteView line = zero_http_trim_trailing_cr((ZeroByteView){request.ptr, line_end});
  if (line.len == 0) return 0;
  if (line_out) *line_out = line;
  return 1;
}

static int zero_http_request_target(ZeroByteView request, size_t *offset_out, size_t *len_out) {
  if (offset_out) *offset_out = 0;
  if (len_out) *len_out = 0;
  ZeroByteView line = {0};
  if (!zero_http_first_line(request, &line)) return 0;
  size_t space = 0;
  while (space < line.len && line.ptr[space] != ' ') space++;
  if (space == 0 || space + 1 >= line.len) return 0;
  ZeroByteView method = {line.ptr, space};
  if (!zero_http_method_valid(method)) return 0;
  size_t start = space + 1;
  size_t end = start;
  while (end < line.len && line.ptr[end] != ' ') {
    if (line.ptr[end] <= 32u) return 0;
    end++;
  }
  if (end == start) return 0;
  for (size_t i = end + 1; i < line.len; i++) {
    if (line.ptr[i] != ' ') return 0;
  }
  if (offset_out) *offset_out = (size_t)(line.ptr + start - request.ptr);
  if (len_out) *len_out = end - start;
  return 1;
}

uint64_t zero_http_request_method_name(ZeroByteView request) {
  ZeroByteView line = {0};
  if (!zero_http_first_line(request, &line)) return 0;
  size_t space = 0;
  while (space < line.len && line.ptr[space] != ' ') space++;
  if (space == 0 || space >= line.len) return 0;
  ZeroByteView method = {line.ptr, space};
  if (!zero_http_method_valid(method)) return 0;
  return zero_http_span_pack(0, space);
}

uint64_t zero_http_request_path(ZeroByteView request) {
  size_t target_offset = 0;
  size_t target_len = 0;
  if (!zero_http_request_target(request, &target_offset, &target_len)) return 0;
  const unsigned char *target = request.ptr + target_offset;

  size_t path_start = 0;
  if (target_len >= 7 && memcmp(target, "http://", 7) == 0) {
    path_start = 7;
    while (path_start < target_len && target[path_start] != '/') path_start++;
    if (path_start >= target_len) return zero_http_span_pack(target_offset + 5, 1);
  } else if (target_len >= 8 && memcmp(target, "https://", 8) == 0) {
    path_start = 8;
    while (path_start < target_len && target[path_start] != '/') path_start++;
    if (path_start >= target_len) return zero_http_span_pack(target_offset + 6, 1);
  }

  size_t path_end = path_start;
  while (path_end < target_len && target[path_end] != '?' && target[path_end] != '#') path_end++;
  if (path_end == path_start) return 0;
  return zero_http_span_pack(target_offset + path_start, path_end - path_start);
}

static const char *zero_http_status_reason(uint32_t status) {
  switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "Unknown";
  }
}

static int zero_http_write_bytes(ZeroMutByteView buffer, size_t *offset, const unsigned char *bytes, size_t len) {
  if (!offset || !buffer.ptr || (!bytes && len > 0)) return 0;
  if (*offset > buffer.len || len > buffer.len - *offset) return 0;
  if (len > 0) memcpy(buffer.ptr + *offset, bytes, len);
  *offset += len;
  return 1;
}

static int zero_http_write_literal(ZeroMutByteView buffer, size_t *offset, const char *text) {
  return zero_http_write_bytes(buffer, offset, (const unsigned char *)text, strlen(text));
}

static int zero_http_write_u32_decimal(ZeroMutByteView buffer, size_t *offset, uint32_t value) {
  unsigned char tmp[10];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (!offset || *offset > buffer.len || len > buffer.len - *offset) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[*offset + i] = tmp[len - 1 - i];
  *offset += len;
  return 1;
}

uint32_t zero_http_write_json_response(ZeroMutByteView buffer, uint32_t status, ZeroByteView body) {
  if (!buffer.ptr || (!body.ptr && body.len > 0) || body.len > 0xffffffffu) return 0;
  size_t offset = 0;
  const char *reason = zero_http_status_reason(status);
  if (!zero_http_write_literal(buffer, &offset, "HTTP/1.1 ") ||
      !zero_http_write_u32_decimal(buffer, &offset, status) ||
      !zero_http_write_literal(buffer, &offset, " ") ||
      !zero_http_write_literal(buffer, &offset, reason) ||
      !zero_http_write_literal(buffer, &offset, "\ncontent-type: application/json\ncontent-length: ") ||
      !zero_http_write_u32_decimal(buffer, &offset, (uint32_t)body.len) ||
      !zero_http_write_literal(buffer, &offset, "\n\n") ||
      !zero_http_write_bytes(buffer, &offset, body.ptr, body.len) ||
      offset > 0xffffffffu) {
    return 0;
  }
  return (uint32_t)offset;
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
