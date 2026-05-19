#include "zero_runtime.h"

#include <errno.h>
#include <stdlib.h>
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

/* ---- std.proc.run ----------------------------------------------------- */

#define ZERO_PROC_LEN_MAX 0xffffffu /* 24-bit capture cap per stream */

static uint64_t zero_proc_pack(uint32_t error, int32_t exit_code,
                               uint32_t timed_out, uint32_t out_len,
                               uint32_t err_len) {
  uint64_t e = (uint64_t)(error & 0x7fu);
  uint64_t code = (uint64_t)((uint32_t)exit_code & 0xffu);
  uint64_t t = timed_out ? 1ull : 0ull;
  if (out_len > ZERO_PROC_LEN_MAX) out_len = ZERO_PROC_LEN_MAX;
  if (err_len > ZERO_PROC_LEN_MAX) err_len = ZERO_PROC_LEN_MAX;
  return (t << 63) | (e << 56) | (code << 48) |
         ((uint64_t)out_len << 24) | (uint64_t)err_len;
}

int32_t zero_proc_run_exit_code(uint64_t result) {
  return (int32_t)((result >> 48) & 0xffu);
}

uint32_t zero_proc_run_out_len(uint64_t result) {
  return (uint32_t)((result >> 24) & ZERO_PROC_LEN_MAX);
}

uint32_t zero_proc_run_err_len(uint64_t result) {
  return (uint32_t)(result & ZERO_PROC_LEN_MAX);
}

uint32_t zero_proc_run_timed_out(uint64_t result) {
  return (uint32_t)((result >> 63) & 1u);
}

uint32_t zero_proc_run_error(uint64_t result) {
  return (uint32_t)((result >> 56) & 0x7fu);
}

static uint32_t zero_proc_read_u32le(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * Decodes the flat argv blob into a NULL-terminated argv array of NUL-
 * terminated C strings. Returns the allocated array (caller frees argv and
 * argv[0]'s backing store) or NULL on malformed input / allocation failure.
 * The whole string table is allocated as one block pointed to by *storage.
 */
static char **zero_proc_decode_argv(ZeroByteView argv, char **storage) {
  *storage = NULL;
  if (!argv.ptr || argv.len < 4) return NULL;
  uint32_t count = zero_proc_read_u32le(argv.ptr);
  if (count == 0 || count > 4096) return NULL;
  size_t pos = 4;
  size_t total = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (pos + 4 > argv.len) return NULL;
    uint32_t alen = zero_proc_read_u32le(argv.ptr + pos);
    pos += 4;
    if (alen > argv.len || pos + alen > argv.len) return NULL;
    total += (size_t)alen + 1;
    pos += alen;
  }
  char **vec = (char **)malloc(sizeof(char *) * ((size_t)count + 1));
  if (!vec) return NULL;
  char *buf = (char *)malloc(total ? total : 1);
  if (!buf) {
    free(vec);
    return NULL;
  }
  pos = 4;
  size_t off = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t alen = zero_proc_read_u32le(argv.ptr + pos);
    pos += 4;
    vec[i] = buf + off;
    if (alen) memcpy(buf + off, argv.ptr + pos, alen);
    buf[off + alen] = '\0';
    off += (size_t)alen + 1;
    pos += alen;
  }
  vec[count] = NULL;
  *storage = buf;
  return vec;
}

#if defined(_WIN32)

uint64_t zero_proc_run_result(
  ZeroByteView argv,
  ZeroByteView stdin_bytes,
  ZeroMutByteView out,
  ZeroMutByteView err,
  int64_t timeout_ns
) {
  (void)argv;
  (void)stdin_bytes;
  (void)out;
  (void)err;
  (void)timeout_ns;
  return zero_proc_pack(ZERO_PROC_UNAVAILABLE, -1, 0, 0, 0);
}

#else

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

static void zero_proc_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int64_t zero_proc_now_ns(void) {
  struct timespec ts;
#if defined(CLOCK_MONOTONIC)
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
  }
#endif
  return 0;
}

uint64_t zero_proc_run_result(
  ZeroByteView argv,
  ZeroByteView stdin_bytes,
  ZeroMutByteView out,
  ZeroMutByteView err,
  int64_t timeout_ns
) {
  char *storage = NULL;
  char **vec = zero_proc_decode_argv(argv, &storage);
  if (!vec) return zero_proc_pack(ZERO_PROC_INVALID_ARGS, -1, 0, 0, 0);

  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    for (int i = 0; i < 2; i++) {
      if (in_pipe[i] >= 0) close(in_pipe[i]);
      if (out_pipe[i] >= 0) close(out_pipe[i]);
      if (err_pipe[i] >= 0) close(err_pipe[i]);
    }
    free(vec);
    free(storage);
    return zero_proc_pack(ZERO_PROC_SPAWN_FAILED, -1, 0, 0, 0);
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(in_pipe[0]); close(in_pipe[1]);
    close(out_pipe[0]); close(out_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);
    free(vec);
    free(storage);
    return zero_proc_pack(ZERO_PROC_SPAWN_FAILED, -1, 0, 0, 0);
  }

  if (pid == 0) {
    /* child */
    dup2(in_pipe[0], 0);
    dup2(out_pipe[1], 1);
    dup2(err_pipe[1], 2);
    close(in_pipe[0]); close(in_pipe[1]);
    close(out_pipe[0]); close(out_pipe[1]);
    close(err_pipe[0]); close(err_pipe[1]);
    execvp(vec[0], vec);
    _exit(127);
  }

  /* parent */
  close(in_pipe[0]);
  close(out_pipe[1]);
  close(err_pipe[1]);
  zero_proc_set_nonblocking(in_pipe[1]);
  zero_proc_set_nonblocking(out_pipe[0]);
  zero_proc_set_nonblocking(err_pipe[0]);

  const unsigned char *in_cursor = stdin_bytes.ptr;
  size_t in_remaining = stdin_bytes.ptr ? stdin_bytes.len : 0;
  if (in_remaining == 0) {
    close(in_pipe[1]);
    in_pipe[1] = -1;
  }

  size_t out_len = 0;
  size_t err_len = 0;
  int out_open = 1;
  int err_open = 1;
  uint32_t error = ZERO_PROC_OK;
  uint32_t timed_out = 0;
  int64_t deadline = (timeout_ns > 0) ? zero_proc_now_ns() + timeout_ns : 0;

  while (out_open || err_open || in_pipe[1] >= 0) {
    struct pollfd fds[3];
    int nfds = 0;
    int idx_out = -1;
    int idx_err = -1;
    int idx_in = -1;
    if (out_open) {
      fds[nfds].fd = out_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      idx_out = nfds++;
    }
    if (err_open) {
      fds[nfds].fd = err_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      idx_err = nfds++;
    }
    if (in_pipe[1] >= 0) {
      fds[nfds].fd = in_pipe[1];
      fds[nfds].events = POLLOUT;
      fds[nfds].revents = 0;
      idx_in = nfds++;
    }

    int wait_ms = -1;
    if (deadline) {
      int64_t now = zero_proc_now_ns();
      int64_t left = deadline - now;
      if (left <= 0) {
        timed_out = 1;
        error = ZERO_PROC_TIMEOUT;
        kill(pid, SIGKILL);
        break;
      }
      int64_t left_ms = left / 1000000ll;
      wait_ms = (left_ms > 1000) ? 1000 : (int)(left_ms + 1);
    }

    int ready = poll(fds, (nfds_t)nfds, wait_ms);
    if (ready < 0) {
      if (errno == EINTR) continue;
      error = ZERO_PROC_IO;
      break;
    }
    if (ready == 0) continue; /* re-check deadline */

    if (idx_in >= 0 && (fds[idx_in].revents & (POLLOUT | POLLERR | POLLHUP))) {
      ssize_t w = write(in_pipe[1], in_cursor, in_remaining);
      if (w > 0) {
        in_cursor += w;
        in_remaining -= (size_t)w;
        if (in_remaining == 0) {
          close(in_pipe[1]);
          in_pipe[1] = -1;
        }
      } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        close(in_pipe[1]);
        in_pipe[1] = -1;
      }
    }

    if (idx_out >= 0 && (fds[idx_out].revents & (POLLIN | POLLHUP | POLLERR))) {
      unsigned char tmp[4096];
      ssize_t r = read(out_pipe[0], tmp, sizeof(tmp));
      if (r > 0) {
        if (out.ptr && out_len < out.len) {
          size_t room = out.len - out_len;
          size_t take = ((size_t)r < room) ? (size_t)r : room;
          memcpy(out.ptr + out_len, tmp, take);
        }
        out_len += (size_t)r;
      } else if (r == 0) {
        out_open = 0;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        out_open = 0;
      }
    }

    if (idx_err >= 0 && (fds[idx_err].revents & (POLLIN | POLLHUP | POLLERR))) {
      unsigned char tmp[4096];
      ssize_t r = read(err_pipe[0], tmp, sizeof(tmp));
      if (r > 0) {
        if (err.ptr && err_len < err.len) {
          size_t room = err.len - err_len;
          size_t take = ((size_t)r < room) ? (size_t)r : room;
          memcpy(err.ptr + err_len, tmp, take);
        }
        err_len += (size_t)r;
      } else if (r == 0) {
        err_open = 0;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        err_open = 0;
      }
    }
  }

  if (in_pipe[1] >= 0) close(in_pipe[1]);
  close(out_pipe[0]);
  close(err_pipe[0]);

  int status = 0;
  for (;;) {
    pid_t w = waitpid(pid, &status, 0);
    if (w == pid) break;
    if (w < 0 && errno != EINTR) {
      status = 0;
      break;
    }
  }

  int32_t exit_code;
  if (timed_out) {
    exit_code = -1;
  } else if (WIFEXITED(status)) {
    exit_code = (int32_t)WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exit_code = (int32_t)(128 + WTERMSIG(status));
  } else {
    exit_code = -1;
  }

  free(vec);
  free(storage);
  return zero_proc_pack(error, exit_code, timed_out,
                        (uint32_t)(out_len > ZERO_PROC_LEN_MAX ? ZERO_PROC_LEN_MAX : out_len),
                        (uint32_t)(err_len > ZERO_PROC_LEN_MAX ? ZERO_PROC_LEN_MAX : err_len));
}

#endif
