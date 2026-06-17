#ifndef ZERO_C_RUNTIME_H
#define ZERO_C_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const unsigned char *ptr;
  size_t len;
} ZeroByteView;

typedef struct {
  unsigned char *ptr;
  size_t len;
} ZeroMutByteView;

typedef struct {
  uint64_t has;
  uint64_t value;
} ZeroMaybeUsize;

typedef enum {
  ZERO_HTTP_OK = 0,
  ZERO_HTTP_INVALID_URL = 1,
  ZERO_HTTP_UNSUPPORTED_PROTOCOL = 2,
  ZERO_HTTP_DNS = 3,
  ZERO_HTTP_CONNECT = 4,
  ZERO_HTTP_TLS = 5,
  ZERO_HTTP_TIMEOUT = 6,
  ZERO_HTTP_TOO_LARGE = 7,
  ZERO_HTTP_PROVIDER_UNAVAILABLE = 8,
  ZERO_HTTP_IO = 9,
  ZERO_HTTP_INVALID_REQUEST = 10
} ZeroHttpError;

typedef enum {
  ZERO_STR_OP_REVERSE = 0,
  ZERO_STR_OP_COPY = 1,
  ZERO_STR_OP_CONCAT = 2,
  ZERO_STR_OP_REPEAT = 3,
  ZERO_STR_OP_TO_LOWER_ASCII = 4,
  ZERO_STR_OP_TO_UPPER_ASCII = 5,
  ZERO_STR_OP_TRIM_ASCII = 6,
  ZERO_STR_OP_TRIM_START_ASCII = 7,
  ZERO_STR_OP_TRIM_END_ASCII = 8,
  ZERO_STR_OP_COUNT_BYTE = 9,
  ZERO_STR_OP_STARTS_WITH = 10,
  ZERO_STR_OP_ENDS_WITH = 11,
  ZERO_STR_OP_CONTAINS = 12,
  ZERO_STR_OP_COUNT = 13,
  ZERO_STR_OP_INDEX_OF = 14,
  ZERO_STR_OP_LAST_INDEX_OF = 15,
  ZERO_STR_OP_EQL_IGNORE_ASCII_CASE = 16,
  ZERO_STR_OP_WORD_COUNT_ASCII = 17,
  ZERO_STR_OP_PATH_BASENAME = 18,
  ZERO_STR_OP_PATH_DIRNAME = 19,
  ZERO_STR_OP_PATH_EXTENSION = 20,
  ZERO_STR_OP_PARSE_TOKEN_ASCII = 21
} ZeroStrOp;

typedef enum {
  ZERO_CRYPTO_DIGEST_SHA256 = 0,
  ZERO_CRYPTO_DIGEST_SHA256_HEX = 1
} ZeroCryptoDigestOp;

typedef enum {
  ZERO_ASCII_OP_IS_DIGIT = 0,
  ZERO_ASCII_OP_IS_LOWER = 1,
  ZERO_ASCII_OP_IS_UPPER = 2,
  ZERO_ASCII_OP_IS_ALPHA = 3,
  ZERO_ASCII_OP_IS_ALNUM = 4,
  ZERO_ASCII_OP_IS_WHITESPACE = 5,
  ZERO_ASCII_OP_IS_HEX_DIGIT = 6,
  ZERO_ASCII_OP_TO_LOWER = 7,
  ZERO_ASCII_OP_TO_UPPER = 8,
  ZERO_ASCII_OP_DIGIT_VALUE = 9,
  ZERO_ASCII_OP_HEX_VALUE = 10
} ZeroAsciiOp;

typedef enum {
  ZERO_TEXT_OP_IS_ASCII = 0,
  ZERO_TEXT_OP_UTF8_VALID = 1,
  ZERO_TEXT_OP_UTF8_LEN = 2
} ZeroTextOp;

typedef enum {
  ZERO_PARSE_OP_IS_ASCII_DIGIT = 0,
  ZERO_PARSE_OP_IS_ASCII_ALPHA = 1,
  ZERO_PARSE_OP_IS_IDENTIFIER_START = 2,
  ZERO_PARSE_OP_IS_WHITESPACE = 3,
  ZERO_PARSE_OP_SCAN_DIGITS = 4,
  ZERO_PARSE_OP_SCAN_IDENTIFIER = 5,
  ZERO_PARSE_OP_SCAN_UNTIL_BYTE = 6,
  ZERO_PARSE_OP_SCAN_WHITESPACE = 7,
  ZERO_PARSE_OP_PARSE_BOOL = 8,
  ZERO_PARSE_OP_PARSE_U8 = 9,
  ZERO_PARSE_OP_PARSE_U16 = 10,
  ZERO_PARSE_OP_PARSE_USIZE = 11,
  ZERO_PARSE_OP_TERM_KEY_CODE = 12,
  ZERO_PARSE_OP_TERM_KEY_BYTE_LEN = 13
} ZeroParseOp;

typedef enum {
  ZERO_TIME_OP_AS_US_FLOOR = 0,
  ZERO_TIME_OP_AS_MS_FLOOR = 1,
  ZERO_TIME_OP_AS_SECONDS_FLOOR = 2,
  ZERO_TIME_OP_MIN = 3,
  ZERO_TIME_OP_MAX = 4,
  ZERO_TIME_OP_CLAMP = 5,
  ZERO_TIME_OP_SLEEP = 6,
  ZERO_TIME_OP_WALL_SECONDS = 7,
  ZERO_TIME_OP_MONOTONIC = 8
} ZeroTimeOp;

typedef enum {
  ZERO_MATH_OP_MIN_I32 = 0,
  ZERO_MATH_OP_MAX_I32 = 1,
  ZERO_MATH_OP_CLAMP_I32 = 2,
  ZERO_MATH_OP_MIN_I64 = 3,
  ZERO_MATH_OP_MAX_I64 = 4,
  ZERO_MATH_OP_CLAMP_I64 = 5,
  ZERO_MATH_OP_MIN_U32 = 6,
  ZERO_MATH_OP_MAX_U32 = 7,
  ZERO_MATH_OP_CLAMP_U32 = 8,
  ZERO_MATH_OP_MIN_U64 = 9,
  ZERO_MATH_OP_MAX_U64 = 10,
  ZERO_MATH_OP_CLAMP_U64 = 11,
  ZERO_MATH_OP_MIN_USIZE = 12,
  ZERO_MATH_OP_MAX_USIZE = 13,
  ZERO_MATH_OP_CLAMP_USIZE = 14,
  ZERO_MATH_OP_ABS_I32 = 15,
  ZERO_MATH_OP_ABS_I64 = 16,
  ZERO_MATH_OP_CHECKED_ADD_U32 = 17,
  ZERO_MATH_OP_CHECKED_SUB_U32 = 18,
  ZERO_MATH_OP_CHECKED_MUL_U32 = 19,
  ZERO_MATH_OP_SATURATING_ADD_U32 = 20,
  ZERO_MATH_OP_SATURATING_SUB_U32 = 21,
  ZERO_MATH_OP_SATURATING_MUL_U32 = 22,
  ZERO_MATH_OP_CHECKED_ADD_I32 = 23,
  ZERO_MATH_OP_CHECKED_SUB_I32 = 24,
  ZERO_MATH_OP_CHECKED_MUL_I32 = 25,
  ZERO_MATH_OP_SATURATING_ADD_I32 = 26,
  ZERO_MATH_OP_SATURATING_SUB_I32 = 27,
  ZERO_MATH_OP_SATURATING_MUL_I32 = 28,
  ZERO_MATH_OP_GCD_U32 = 29,
  ZERO_MATH_OP_LCM_U32 = 30,
  ZERO_MATH_OP_CHECKED_LCM_U32 = 31,
  ZERO_MATH_OP_POW_U32 = 32,
  ZERO_MATH_OP_CHECKED_POW_U32 = 33,
  ZERO_MATH_OP_MOD_POW_U32 = 34,
  ZERO_MATH_OP_IS_PRIME_U32 = 35,
  ZERO_MATH_OP_SQRT_FLOOR_U32 = 36,
  ZERO_MATH_OP_FACTORIAL_U32 = 37,
  ZERO_MATH_OP_BINOMIAL_U32 = 38,
  ZERO_MATH_OP_DIVISOR_COUNT_U32 = 39,
  ZERO_MATH_OP_PROPER_DIVISOR_SUM_U32 = 40,
  ZERO_MATH_OP_CHECKED_ADD_USIZE = 41,
  ZERO_MATH_OP_CHECKED_SUB_USIZE = 42,
  ZERO_MATH_OP_CHECKED_MUL_USIZE = 43,
  ZERO_MATH_OP_SATURATING_ADD_USIZE = 44,
  ZERO_MATH_OP_SATURATING_SUB_USIZE = 45,
  ZERO_MATH_OP_SATURATING_MUL_USIZE = 46
} ZeroMathOp;

typedef enum {
  ZERO_SEARCH_OP_LOWER_BOUND_I32 = 0,
  ZERO_SEARCH_OP_BINARY_I32 = 1,
  ZERO_SEARCH_OP_LOWER_BOUND_U32 = 2,
  ZERO_SEARCH_OP_BINARY_U32 = 3,
  ZERO_SEARCH_OP_LOWER_BOUND_USIZE = 4,
  ZERO_SEARCH_OP_BINARY_USIZE = 5,
  ZERO_SEARCH_OP_UPPER_BOUND_I32 = 6,
  ZERO_SEARCH_OP_UPPER_BOUND_U32 = 7,
  ZERO_SEARCH_OP_UPPER_BOUND_USIZE = 8
} ZeroSearchOp;

typedef enum {
  ZERO_SORT_OP_INSERTION_I32 = 0,
  ZERO_SORT_OP_IS_SORTED_I32 = 1,
  ZERO_SORT_OP_INSERTION_U32 = 2,
  ZERO_SORT_OP_IS_SORTED_U32 = 3,
  ZERO_SORT_OP_INSERTION_USIZE = 4,
  ZERO_SORT_OP_IS_SORTED_USIZE = 5
} ZeroSortOp;

typedef enum {
  ZERO_TERM_OP_STDIN_IS_TTY = 0,
  ZERO_TERM_OP_STDOUT_IS_TTY = 1,
  ZERO_TERM_OP_WIDTH_OR = 2,
  ZERO_TERM_OP_HEIGHT_OR = 3,
  ZERO_TERM_OP_ENTER_RAW_MODE = 4,
  ZERO_TERM_OP_LEAVE_RAW_MODE = 5,
  ZERO_TERM_OP_READ_INPUT = 6
} ZeroTermOp;

typedef enum {
  ZERO_PROC_CHILD_OP_RUNNING = 0,
  ZERO_PROC_CHILD_OP_WAIT = 1,
  ZERO_PROC_CHILD_OP_KILL = 2,
  ZERO_PROC_CHILD_OP_CLOSE = 3,
  ZERO_PROC_CHILD_OP_VALID = 4
} ZeroProcChildOp;

typedef enum {
  ZERO_PROC_CHILD_IO_READ_STDOUT = 0,
  ZERO_PROC_CHILD_IO_READ_STDERR = 1,
  ZERO_PROC_CHILD_IO_WRITE_STDIN = 2
} ZeroProcChildIoOp;

typedef enum {
  ZERO_FS_PATH_EXISTS = 0,
  ZERO_FS_PATH_IS_DIR = 1,
  ZERO_FS_PATH_MAKE_DIR = 2,
  ZERO_FS_PATH_REMOVE_DIR = 3,
  ZERO_FS_PATH_REMOVE = 4
} ZeroFsPathOp;

#define ZERO_HTTP_RESPONSE_META_BYTES 24u

int zero_world_write(int fd, const char *buf, unsigned len);

ZeroMaybeUsize zero_fs_read_bytes(ZeroByteView path, ZeroMutByteView buffer);
ZeroMaybeUsize zero_fs_read_bytes_at(ZeroByteView path, uint64_t offset, ZeroMutByteView buffer);
ZeroMaybeUsize zero_fs_write_bytes(ZeroByteView path, ZeroByteView bytes);
ZeroMaybeUsize zero_fs_append_bytes(ZeroByteView path, ZeroByteView bytes);
uint32_t zero_fs_path_op(ZeroByteView path, uint32_t op);
uint32_t zero_fs_rename(ZeroByteView from_path, ZeroByteView to_path);
int32_t zero_proc_spawn_inherit(ZeroByteView command);
int32_t zero_proc_spawn_inherit_args(ZeroByteView program, ZeroByteView args, ZeroByteView cwd, ZeroByteView env);
ZeroMaybeUsize zero_proc_capture(ZeroByteView command, ZeroMutByteView buffer);
ZeroMaybeUsize zero_proc_capture_args(ZeroByteView program, ZeroByteView args, ZeroMutByteView buffer);
int32_t zero_proc_capture_files(ZeroByteView command, ZeroByteView stdout_path, ZeroByteView stderr_path);
int32_t zero_proc_capture_files_args(ZeroByteView program, ZeroByteView args, ZeroByteView stdout_path, ZeroByteView stderr_path);
int32_t zero_proc_spawn_child(ZeroByteView command);
int32_t zero_proc_spawn_child_in(ZeroByteView command, ZeroByteView cwd);
int32_t zero_proc_spawn_child_in_env(ZeroByteView command, ZeroByteView cwd, ZeroByteView env);
int32_t zero_proc_spawn_child_args(ZeroByteView program, ZeroByteView args, ZeroByteView cwd, ZeroByteView env);
int32_t zero_proc_child_op(int32_t child, uint32_t op);
ZeroMaybeUsize zero_proc_child_io(int32_t child, ZeroMutByteView buffer, uint32_t op);

int64_t zero_json_parse_bytes(ZeroByteView input);
uint64_t zero_json_diagnostic(ZeroByteView input, uint32_t op);
uint64_t zero_json_field(ZeroByteView input, ZeroByteView key);
uint64_t zero_json_lookup_scalar(ZeroByteView input, ZeroByteView key, uint32_t op);
uint64_t zero_json_string_decode(ZeroMutByteView buffer, ZeroByteView raw);
uint64_t zero_json_string_field(ZeroMutByteView buffer, ZeroByteView input, ZeroByteView key);
uint64_t zero_json_write_string(ZeroMutByteView buffer, ZeroByteView text);
uint64_t zero_json_write_field_raw(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value);
uint64_t zero_json_write_field_string(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value);
uint64_t zero_json_write_field_u32(ZeroMutByteView buffer, ZeroByteView key, uint32_t value);
uint64_t zero_json_write_field_bool(ZeroMutByteView buffer, ZeroByteView key, uint32_t value);
uint64_t zero_json_write_object1_string(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value);
uint64_t zero_json_write_object1_u32(ZeroMutByteView buffer, ZeroByteView key, uint32_t value);
uint64_t zero_json_write_object1_bool(ZeroMutByteView buffer, ZeroByteView key, uint32_t value);
uint64_t zero_json_write_object2_fields(ZeroMutByteView buffer, ZeroByteView field0, ZeroByteView field1);
uint64_t zero_json_write_object2_string_field(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value, ZeroByteView field1);
uint64_t zero_json_write_object2_u32_field(ZeroMutByteView buffer, ZeroByteView key, uint32_t value, ZeroByteView field1);
uint64_t zero_json_write_object2_bool_field(ZeroMutByteView buffer, ZeroByteView key, uint32_t value, ZeroByteView field1);
uint64_t zero_json_write_array2_strings(ZeroMutByteView buffer, ZeroByteView value0, ZeroByteView value1);
uint64_t zero_json_write_array2_u32(ZeroMutByteView buffer, uint32_t value0, uint32_t value1);
uint64_t zero_json_write_array2_bools(ZeroMutByteView buffer, uint32_t value0, uint32_t value1);
uint32_t zero_ascii_op(uint32_t byte, uint32_t op);
uint64_t zero_text_op(ZeroByteView text, uint32_t op);
uint64_t zero_parse_op(ZeroByteView text, uint32_t arg, uint32_t op);
ZeroMaybeUsize zero_parse_usize(ZeroByteView text);
int64_t zero_time_op(int64_t a, int64_t b, int64_t c, uint32_t op);
uint32_t zero_rand_entropy_u32(void);
uint64_t zero_math_op(int64_t a, int64_t b, int64_t c, uint32_t op);
ZeroMaybeUsize zero_math_usize_op(uint64_t a, uint64_t b, uint64_t c, uint32_t op);
uint64_t zero_search_op(ZeroByteView items, int64_t needle, uint32_t op);
void zero_sort_op(ZeroMutByteView items, uint32_t op);
uint32_t zero_sort_is_sorted_op(ZeroByteView items, uint32_t op);
uint64_t zero_term_op(uint64_t fallback, uint32_t op);
ZeroMaybeUsize zero_term_read_input(ZeroMutByteView buffer);
uint32_t zero_str_contains(ZeroByteView text, ZeroByteView needle);
uint32_t zero_str_buffer_op(ZeroMutByteView buffer, ZeroByteView text, uint32_t op);
uint32_t zero_str_concat(ZeroMutByteView buffer, ZeroByteView left, ZeroByteView right);
uint32_t zero_str_repeat(ZeroMutByteView buffer, ZeroByteView text, uint64_t count);
uint64_t zero_str_trim_op(ZeroByteView text, uint32_t op);
uint64_t zero_str_pair_op(ZeroByteView text, ZeroByteView needle, uint32_t op);
uint64_t zero_str_count_byte(ZeroByteView text, uint32_t byte);
uint64_t zero_str_word_count_ascii(ZeroByteView text);
uint32_t zero_crypto_digest(ZeroMutByteView buffer, ZeroByteView bytes, uint32_t op);
uint32_t zero_crypto_hmac_sha256(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView bytes);
uint32_t zero_crypto_hmac_sha256_hex(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView bytes);
uint64_t zero_args_find(uint64_t argc, const char *const *argv, ZeroByteView name);
uint64_t zero_parse_i32(ZeroByteView text);
uint64_t zero_parse_u32(ZeroByteView text);
uint32_t zero_fmt_bool(ZeroMutByteView buffer, uint32_t value);
uint32_t zero_fmt_hex_lower_u32(ZeroMutByteView buffer, uint32_t value);
uint32_t zero_fmt_i32(ZeroMutByteView buffer, int32_t value);
uint32_t zero_fmt_u32(ZeroMutByteView buffer, uint32_t value);
uint32_t zero_fmt_usize(ZeroMutByteView buffer, uint64_t value);

uint64_t zero_http_fetch_result(
  ZeroByteView request,
  ZeroMutByteView response_out,
  int64_t timeout_ns
);

uint32_t zero_http_result_ok(uint64_t result);
uint32_t zero_http_result_status(uint64_t result);
uint32_t zero_http_result_body_len(uint64_t result);
uint32_t zero_http_result_error(uint64_t result);

uint32_t zero_http_response_len(ZeroByteView response);
uint32_t zero_http_response_headers_len(ZeroByteView response);
uint32_t zero_http_response_body_offset(ZeroByteView response);
uint64_t zero_http_header_value(ZeroByteView headers, ZeroByteView name);
uint32_t zero_http_header_found(uint64_t value);
uint32_t zero_http_header_offset(uint64_t value);
uint32_t zero_http_header_len(uint64_t value);
uint32_t zero_http_write_json_response(ZeroMutByteView buffer, uint32_t status, ZeroByteView body);
uint64_t zero_http_request_method_name(ZeroByteView request);
uint64_t zero_http_request_path(ZeroByteView request);
uint32_t zero_http_request_matches(ZeroByteView request, ZeroByteView method, ZeroByteView path);
uint64_t zero_http_request_body_within(ZeroByteView request, uint64_t max, uint32_t require_json);

#endif
