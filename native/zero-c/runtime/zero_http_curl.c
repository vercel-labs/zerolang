#include "zero_runtime.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__has_include)
#define __has_include(x) 0
#endif

#if !defined(ZERO_RUNTIME_NO_CURL) && __has_include(<curl/curl.h>)
#define ZERO_RUNTIME_HAS_CURL 1
#include <curl/curl.h>
#endif

static uint64_t zero_http_result_pack(uint32_t error, uint16_t status, uint32_t body_len) {
  return ((uint64_t)(error & 0xffffu) << 48) |
         ((uint64_t)status << 32) |
         (uint64_t)body_len;
}

static void zero_http_write_u16le(unsigned char *ptr, uint16_t value) {
  ptr[0] = (unsigned char)(value & 0xffu);
  ptr[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void zero_http_write_u32le(unsigned char *ptr, uint32_t value) {
  ptr[0] = (unsigned char)(value & 0xffu);
  ptr[1] = (unsigned char)((value >> 8) & 0xffu);
  ptr[2] = (unsigned char)((value >> 16) & 0xffu);
  ptr[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void zero_http_write_response_meta(
  ZeroMutByteView response,
  uint16_t status,
  uint32_t error,
  uint32_t headers_len,
  uint32_t body_len
) {
  if (!response.ptr || response.len < ZERO_HTTP_RESPONSE_META_BYTES) return;
  response.ptr[0] = 'Z';
  response.ptr[1] = 'H';
  response.ptr[2] = 'R';
  response.ptr[3] = '1';
  zero_http_write_u16le(response.ptr + 4, status);
  zero_http_write_u16le(response.ptr + 6, (uint16_t)(error & 0xffffu));
  zero_http_write_u32le(response.ptr + 8, headers_len);
  zero_http_write_u32le(response.ptr + 12, body_len);
  zero_http_write_u32le(response.ptr + 16, headers_len > 0xffffffffu - body_len ? 0xffffffffu : headers_len + body_len);
  zero_http_write_u32le(response.ptr + 20, 0);
}

#if ZERO_RUNTIME_HAS_CURL
typedef struct {
  unsigned char *ptr;
  size_t cap;
  size_t headers_len;
  size_t body_len;
  int too_large;
} ZeroCurlResponseBuf;

typedef struct {
  ZeroByteView method;
  ZeroByteView url;
  ZeroByteView headers;
  ZeroByteView body;
} ZeroParsedHttpRequest;

static CURLcode zero_curl_global_status = CURLE_FAILED_INIT;
static int zero_curl_global_initialized = 0;

static void zero_runtime_curl_global_cleanup(void) {
  curl_global_cleanup();
}

static void zero_runtime_curl_global_init(void) {
  if (zero_curl_global_initialized) return;
  zero_curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
  zero_curl_global_initialized = 1;
  if (zero_curl_global_status == CURLE_OK) {
    (void)atexit(zero_runtime_curl_global_cleanup);
  }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void zero_runtime_curl_global_constructor(void) {
  zero_runtime_curl_global_init();
}
#endif

static int zero_runtime_curl_global_ok(void) {
  zero_runtime_curl_global_init();
  return zero_curl_global_status == CURLE_OK;
}

static int zero_curl_response_header_starts_block(const char *ptr, size_t n) {
  return n >= 5 && ptr[0] == 'H' && ptr[1] == 'T' && ptr[2] == 'T' && ptr[3] == 'P' && ptr[4] == '/';
}

static size_t zero_curl_response_append(ZeroCurlResponseBuf *out, const char *ptr, size_t n, int is_header) {
  if (!out || !ptr) return 0;
  if (is_header) {
    if (out->body_len > 0) return n;
    /* libcurl emits separate blocks for interim and final responses; keep the final block. */
    if (zero_curl_response_header_starts_block(ptr, n)) out->headers_len = 0;
  }
  if (out->too_large) return 0;
  size_t used = out->headers_len + out->body_len;
  if (used < out->headers_len || used > out->cap || n > out->cap - used) {
    out->too_large = 1;
    return 0;
  }
  if (n > 0) memcpy(out->ptr + used, ptr, n);
  if (is_header) out->headers_len += n;
  else out->body_len += n;
  return n;
}

static size_t zero_curl_response_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t n = size * nmemb;
  if (size != 0 && n / size != nmemb) return 0;
  return zero_curl_response_append((ZeroCurlResponseBuf *)userdata, ptr, n, 1);
}

static size_t zero_curl_response_body_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t n = size * nmemb;
  if (size != 0 && n / size != nmemb) return 0;
  return zero_curl_response_append((ZeroCurlResponseBuf *)userdata, ptr, n, 0);
}

static int zero_runtime_view_has_nul(ZeroByteView view) {
  for (size_t i = 0; i < view.len; i++) {
    if (view.ptr[i] == 0) return 1;
  }
  return 0;
}

static ZeroByteView zero_runtime_trim_trailing_cr(ZeroByteView view) {
  if (view.len > 0 && view.ptr[view.len - 1] == '\r') view.len--;
  return view;
}

static int zero_runtime_url_supported(ZeroByteView url) {
  static const unsigned char http[] = "http://";
  static const unsigned char https[] = "https://";
  return (url.len >= sizeof(http) - 1 && memcmp(url.ptr, http, sizeof(http) - 1) == 0) ||
         (url.len >= sizeof(https) - 1 && memcmp(url.ptr, https, sizeof(https) - 1) == 0);
}

static int zero_runtime_http_token_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
         ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
         ch == '`' || ch == '|' || ch == '~';
}

static int zero_runtime_method_valid(ZeroByteView method) {
  if (!method.ptr || method.len == 0) return 0;
  for (size_t i = 0; i < method.len; i++) {
    if (!zero_runtime_http_token_char(method.ptr[i])) return 0;
  }
  return 1;
}

static int zero_runtime_method_is(ZeroByteView method, const char *name) {
  size_t len = strlen(name);
  return method.len == len && memcmp(method.ptr, name, len) == 0;
}

static char *zero_runtime_dup_view(ZeroByteView view) {
  if (view.len == (size_t)-1) return NULL;
  char *out = (char *)malloc(view.len + 1);
  if (!out) return NULL;
  if (view.len > 0) memcpy(out, view.ptr, view.len);
  out[view.len] = 0;
  return out;
}

static int zero_http_parse_request(ZeroByteView request, ZeroParsedHttpRequest *out) {
  if (!out) return 0;
  *out = (ZeroParsedHttpRequest){0};
  if (!request.ptr || request.len == 0) return 0;

  size_t first_line_end = 0;
  while (first_line_end < request.len && request.ptr[first_line_end] != '\n') first_line_end++;
  if (first_line_end >= request.len) return 0;
  ZeroByteView line = zero_runtime_trim_trailing_cr((ZeroByteView){request.ptr, first_line_end});
  if (line.len == 0 || zero_runtime_view_has_nul(line)) return 0;

  size_t space = 0;
  while (space < line.len && line.ptr[space] != ' ') space++;
  if (space == 0 || space >= line.len - 1) return 0;
  out->method = (ZeroByteView){line.ptr, space};
  out->url = (ZeroByteView){line.ptr + space + 1, line.len - space - 1};
  if (!zero_runtime_method_valid(out->method) || zero_runtime_view_has_nul(out->url)) return 0;
  for (size_t i = 0; i < out->url.len; i++) {
    if (out->url.ptr[i] <= ' ') return 0;
  }

  size_t pos = first_line_end < request.len ? first_line_end + 1 : first_line_end;
  size_t header_start = pos;
  while (pos < request.len) {
    size_t line_start = pos;
    while (pos < request.len && request.ptr[pos] != '\n') pos++;
    size_t line_len = pos - line_start;
    if (line_len > 0 && request.ptr[line_start + line_len - 1] == '\r') line_len--;
    size_t next = pos < request.len ? pos + 1 : pos;
    if (line_len == 0) {
      out->headers = (ZeroByteView){request.ptr + header_start, line_start - header_start};
      out->body = (ZeroByteView){request.ptr + next, request.len - next};
      return 1;
    }
    pos = next;
  }

  (void)header_start;
  return 0;
}

static int zero_runtime_header_line_valid(ZeroByteView line) {
  if (!line.ptr || line.len == 0 || zero_runtime_view_has_nul(line)) return 0;
  size_t colon = 0;
  while (colon < line.len && line.ptr[colon] != ':') colon++;
  if (colon == 0 || colon >= line.len) return 0;
  for (size_t i = 0; i < colon; i++) {
    if (!zero_runtime_http_token_char(line.ptr[i])) return 0;
  }
  for (size_t i = colon + 1; i < line.len; i++) {
    unsigned char ch = line.ptr[i];
    if (ch == '\r' || ch == '\n') return 0;
    if (ch < 0x20u && ch != '\t') return 0;
  }
  return 1;
}

static uint32_t zero_runtime_curl_headers(ZeroByteView headers, struct curl_slist **out) {
  if (!out) return ZERO_HTTP_IO;
  *out = NULL;
  size_t pos = 0;
  while (pos < headers.len) {
    size_t line_start = pos;
    while (pos < headers.len && headers.ptr[pos] != '\n') pos++;
    size_t line_len = pos - line_start;
    if (line_len > 0 && headers.ptr[line_start + line_len - 1] == '\r') line_len--;
    if (pos < headers.len) pos++;
    ZeroByteView line = {headers.ptr + line_start, line_len};
    if (line.len == 0) continue;
    if (!zero_runtime_header_line_valid(line)) {
      curl_slist_free_all(*out);
      *out = NULL;
      return ZERO_HTTP_INVALID_REQUEST;
    }
    char *header = zero_runtime_dup_view(line);
    if (!header) {
      curl_slist_free_all(*out);
      *out = NULL;
      return ZERO_HTTP_IO;
    }
    struct curl_slist *next = curl_slist_append(*out, header);
    free(header);
    if (!next) {
      curl_slist_free_all(*out);
      *out = NULL;
      return ZERO_HTTP_IO;
    }
    *out = next;
  }
  return ZERO_HTTP_OK;
}

static long zero_runtime_timeout_ms(int64_t timeout_ns) {
  if (timeout_ns <= 0) return 0;
  uint64_t ns = (uint64_t)timeout_ns;
  uint64_t ms = ns / 1000000ull;
  if (ms == 0) ms = 1;
  if (ms > (uint64_t)LONG_MAX) return LONG_MAX;
  return (long)ms;
}

static const char *zero_runtime_test_ca_bundle(void) {
  const char *path = getenv("ZERO_HTTP_TEST_CA_BUNDLE");
  return path && path[0] ? path : NULL;
}

static uint32_t zero_runtime_curl_error(CURLcode code) {
  switch (code) {
    case CURLE_OK:
      return ZERO_HTTP_OK;
    case CURLE_URL_MALFORMAT:
      return ZERO_HTTP_INVALID_URL;
    case CURLE_UNSUPPORTED_PROTOCOL:
      return ZERO_HTTP_UNSUPPORTED_PROTOCOL;
    case CURLE_COULDNT_RESOLVE_HOST:
      return ZERO_HTTP_DNS;
    case CURLE_COULDNT_CONNECT:
      return ZERO_HTTP_CONNECT;
    case CURLE_OPERATION_TIMEDOUT:
      return ZERO_HTTP_TIMEOUT;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
      return ZERO_HTTP_TLS;
    case CURLE_WRITE_ERROR:
      return ZERO_HTTP_TOO_LARGE;
    default:
      return ZERO_HTTP_IO;
  }
}

static int zero_http_perform_fetch(
  ZeroByteView method,
  ZeroByteView url,
  ZeroByteView request_headers,
  ZeroByteView request_body,
  ZeroMutByteView response_out,
  int64_t timeout_ns,
  uint16_t *status_out,
  uint32_t *headers_len_out,
  uint32_t *body_len_out,
  uint32_t *error_out
) {
  if (status_out) *status_out = 0;
  if (headers_len_out) *headers_len_out = 0;
  if (body_len_out) *body_len_out = 0;
  if (error_out) *error_out = ZERO_HTTP_OK;
  zero_http_write_response_meta(response_out, 0, ZERO_HTTP_OK, 0, 0);
  if (!response_out.ptr || response_out.len < ZERO_HTTP_RESPONSE_META_BYTES) {
    if (error_out) *error_out = ZERO_HTTP_TOO_LARGE;
    return -1;
  }
  if (!zero_runtime_method_valid(method)) {
    if (error_out) *error_out = ZERO_HTTP_INVALID_REQUEST;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_INVALID_REQUEST, 0, 0);
    return -1;
  }
  if (!url.ptr || url.len == 0 || zero_runtime_view_has_nul(url)) {
    if (error_out) *error_out = ZERO_HTTP_INVALID_URL;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_INVALID_URL, 0, 0);
    return -1;
  }
  if (!zero_runtime_url_supported(url)) {
    if (error_out) *error_out = ZERO_HTTP_UNSUPPORTED_PROTOCOL;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_UNSUPPORTED_PROTOCOL, 0, 0);
    return -1;
  }
  if (!request_body.ptr && request_body.len > 0) {
    if (error_out) *error_out = ZERO_HTTP_IO;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_IO, 0, 0);
    return -1;
  }

  struct curl_slist *header_list = NULL;
  uint32_t header_error = zero_runtime_curl_headers(request_headers, &header_list);
  if (header_error != ZERO_HTTP_OK) {
    if (error_out) *error_out = header_error;
    zero_http_write_response_meta(response_out, 0, header_error, 0, 0);
    return -1;
  }

  char *method_c = zero_runtime_dup_view(method);
  char *url_c = zero_runtime_dup_view(url);
  if (!method_c || !url_c) {
    curl_slist_free_all(header_list);
    free(method_c);
    free(url_c);
    if (error_out) *error_out = ZERO_HTTP_IO;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_IO, 0, 0);
    return -1;
  }

  if (!zero_runtime_curl_global_ok()) {
    curl_slist_free_all(header_list);
    free(method_c);
    free(url_c);
    if (error_out) *error_out = ZERO_HTTP_PROVIDER_UNAVAILABLE;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_PROVIDER_UNAVAILABLE, 0, 0);
    return -1;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    curl_slist_free_all(header_list);
    free(method_c);
    free(url_c);
    if (error_out) *error_out = ZERO_HTTP_PROVIDER_UNAVAILABLE;
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_PROVIDER_UNAVAILABLE, 0, 0);
    return -1;
  }

  ZeroCurlResponseBuf response = {
    .ptr = response_out.ptr + ZERO_HTTP_RESPONSE_META_BYTES,
    .cap = response_out.len - ZERO_HTTP_RESPONSE_META_BYTES,
    .headers_len = 0,
    .body_len = 0,
    .too_large = 0
  };

  curl_easy_setopt(curl, CURLOPT_URL, url_c);
  if (zero_runtime_method_is(method, "GET") && request_body.len == 0) {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else if (zero_runtime_method_is(method, "HEAD") && request_body.len == 0) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_c);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  } else {
    static const char empty_body[] = "";
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_c);
    if (request_body.len > 0 ||
        zero_runtime_method_is(method, "POST") ||
        zero_runtime_method_is(method, "PUT") ||
        zero_runtime_method_is(method, "PATCH")) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request_body.len);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.len > 0 ? (const char *)request_body.ptr : empty_body);
    }
  }
  if (header_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
#if defined(LIBCURL_VERSION_NUM) && LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  const char *test_ca_bundle = zero_runtime_test_ca_bundle();
  if (test_ca_bundle) curl_easy_setopt(curl, CURLOPT_CAINFO, test_ca_bundle);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, zero_runtime_timeout_ms(timeout_ns));
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, zero_curl_response_header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zero_curl_response_body_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  curl_slist_free_all(header_list);
  free(method_c);
  free(url_c);

  uint16_t status16 = status > 0 && status <= 65535 ? (uint16_t)status : 0;
  if (status_out) *status_out = status16;
  uint32_t mapped = zero_runtime_curl_error(result);
  if (response.too_large) mapped = ZERO_HTTP_TOO_LARGE;
  if (response.headers_len > 0xffffffffu || response.body_len > 0xffffffffu ||
      response.headers_len > 0xffffffffu - response.body_len) {
    mapped = ZERO_HTTP_TOO_LARGE;
  }
  uint32_t headers_len = mapped == ZERO_HTTP_OK ? (uint32_t)response.headers_len : 0;
  uint32_t body_len = mapped == ZERO_HTTP_OK ? (uint32_t)response.body_len : 0;
  if (headers_len_out) *headers_len_out = headers_len;
  if (body_len_out) *body_len_out = body_len;
  if (error_out) *error_out = mapped;
  zero_http_write_response_meta(response_out, status16, mapped, headers_len, body_len);
  if (mapped != ZERO_HTTP_OK) return -1;
  return 0;
}

uint64_t zero_http_fetch_result(
  ZeroByteView request,
  ZeroMutByteView response_out,
  int64_t timeout_ns
) {
  uint16_t status = 0;
  uint32_t headers_len = 0;
  uint32_t body_len = 0;
  uint32_t error = ZERO_HTTP_OK;
  ZeroParsedHttpRequest parsed;
  if (!zero_http_parse_request(request, &parsed)) {
    zero_http_write_response_meta(response_out, 0, ZERO_HTTP_INVALID_REQUEST, 0, 0);
    return zero_http_result_pack(ZERO_HTTP_INVALID_REQUEST, 0, 0);
  }
  int ok = zero_http_perform_fetch(
    parsed.method,
    parsed.url,
    parsed.headers,
    parsed.body,
    response_out,
    timeout_ns,
    &status,
    &headers_len,
    &body_len,
    &error
  );
  (void)headers_len;
  if (ok < 0) body_len = 0;
  return zero_http_result_pack(error, status, body_len);
}

#else
uint64_t zero_http_fetch_result(
  ZeroByteView request,
  ZeroMutByteView response_out,
  int64_t timeout_ns
) {
  (void)request;
  (void)timeout_ns;
  zero_http_write_response_meta(response_out, 0, ZERO_HTTP_PROVIDER_UNAVAILABLE, 0, 0);
  return zero_http_result_pack(ZERO_HTTP_PROVIDER_UNAVAILABLE, 0, 0);
}

#endif
