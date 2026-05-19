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

#define ZERO_HTTP_RESPONSE_META_BYTES 24u

typedef enum {
  ZERO_PROC_OK = 0,
  ZERO_PROC_INVALID_ARGS = 1,
  ZERO_PROC_SPAWN_FAILED = 2,
  ZERO_PROC_IO = 3,
  ZERO_PROC_TIMEOUT = 4,
  ZERO_PROC_UNAVAILABLE = 5
} ZeroProcError;

int zero_world_write(int fd, const char *buf, unsigned len);

int64_t zero_json_parse_bytes(ZeroByteView input);

uint64_t zero_http_fetch_result(
  ZeroByteView request,
  ZeroMutByteView response_out,
  int64_t timeout_ns
);

uint32_t zero_http_result_ok(uint64_t result);
uint32_t zero_http_result_status(uint64_t result);
uint32_t zero_http_result_body_len(uint64_t result);
uint32_t zero_http_result_error(uint64_t result);

/*
 * Runs argv[0] with arguments argv[1..] (no shell interpretation). The argv
 * vector is encoded as a flat blob: a little-endian u32 argument count followed
 * by, for each argument, a little-endian u32 length and that many raw bytes.
 * stdin bytes are written to the child; captured stdout/stderr are drained into
 * out/err (truncated to buffer capacity). The child is sent SIGKILL when
 * timeout_ns elapses (<= 0 means no timeout). Returns a packed result handle.
 */
uint64_t zero_proc_run_result(
  ZeroByteView argv,
  ZeroByteView stdin_bytes,
  ZeroMutByteView out,
  ZeroMutByteView err,
  int64_t timeout_ns
);

int32_t zero_proc_run_exit_code(uint64_t result);
uint32_t zero_proc_run_out_len(uint64_t result);
uint32_t zero_proc_run_err_len(uint64_t result);
uint32_t zero_proc_run_timed_out(uint64_t result);
uint32_t zero_proc_run_error(uint64_t result);

uint32_t zero_http_response_len(ZeroByteView response);
uint32_t zero_http_response_headers_len(ZeroByteView response);
uint32_t zero_http_response_body_offset(ZeroByteView response);
uint64_t zero_http_header_value(ZeroByteView headers, ZeroByteView name);
uint32_t zero_http_header_found(uint64_t value);
uint32_t zero_http_header_offset(uint64_t value);
uint32_t zero_http_header_len(uint64_t value);

#endif
