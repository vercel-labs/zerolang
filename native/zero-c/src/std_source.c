#include "std_source.h"

#include "embedded_stdlib_graph.inc"
#include "program_graph_format.h"
#include "program_graph_store_binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *public_name;
  const char *target_name;
  const char *module;
} ZStdSourceCall;

static const ZStdSourceModule std_source_modules[] = {
  {"std.args", "std/args.0", zero_embedded_stdlib_graph_std_args_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_args_graph_bytes)},
  {"std.ascii", "std/ascii.0", zero_embedded_stdlib_graph_std_ascii_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_ascii_graph_bytes)},
  {"std.cli", "std/cli.0", zero_embedded_stdlib_graph_std_cli_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_cli_graph_bytes)},
  {"std.codec", "std/codec.0", zero_embedded_stdlib_graph_std_codec_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_codec_graph_bytes)},
  {"std.collections", "std/collections.0", zero_embedded_stdlib_graph_std_collections_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_collections_graph_bytes)},
  {"std.env", "std/env.0", zero_embedded_stdlib_graph_std_env_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_env_graph_bytes)},
  {"std.fmt", "std/fmt.0", zero_embedded_stdlib_graph_std_fmt_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_fmt_graph_bytes)},
  {"std.fs", "std/fs.0", zero_embedded_stdlib_graph_std_fs_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_fs_graph_bytes)},
  {"std.http", "std/http.0", zero_embedded_stdlib_graph_std_http_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_http_graph_bytes)},
  {"std.io", "std/io.0", zero_embedded_stdlib_graph_std_io_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_io_graph_bytes)},
  {"std.json", "std/json.0", zero_embedded_stdlib_graph_std_json_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_json_graph_bytes)},
  {"std.log", "std/log.0", zero_embedded_stdlib_graph_std_log_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_log_graph_bytes)},
  {"std.math", "std/math.0", zero_embedded_stdlib_graph_std_math_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_math_graph_bytes)},
  {"std.mem", "std/mem.0", zero_embedded_stdlib_graph_std_mem_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_mem_graph_bytes)},
  {"std.net", "std/net.0", zero_embedded_stdlib_graph_std_net_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_net_graph_bytes)},
  {"std.parse", "std/parse.0", zero_embedded_stdlib_graph_std_parse_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_parse_graph_bytes)},
  {"std.path", "std/path.0", zero_embedded_stdlib_graph_std_path_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_path_graph_bytes)},
  {"std.proc", "std/proc.0", zero_embedded_stdlib_graph_std_proc_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_proc_graph_bytes)},
  {"std.search", "std/search.0", zero_embedded_stdlib_graph_std_search_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_search_graph_bytes)},
  {"std.sort", "std/sort.0", zero_embedded_stdlib_graph_std_sort_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_sort_graph_bytes)},
  {"std.str", "std/str.0", zero_embedded_stdlib_graph_std_str_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_str_graph_bytes)},
  {"std.testing", "std/testing.0", zero_embedded_stdlib_graph_std_testing_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_testing_graph_bytes)},
  {"std.text", "std/text.0", zero_embedded_stdlib_graph_std_text_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_text_graph_bytes)},
  {"std.time", "std/time.0", zero_embedded_stdlib_graph_std_time_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_time_graph_bytes)},
  {"std.toml", "std/toml.0", zero_embedded_stdlib_graph_std_toml_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_toml_graph_bytes)},
  {"std.url", "std/url.0", zero_embedded_stdlib_graph_std_url_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_url_graph_bytes)},
};

static const ZStdSourceCall std_source_calls[] = {
  {"std.codec.base64EncodedLen", "__zero_std_codec_base64_encoded_len", "std.codec"},
  {"std.codec.base64Encode", "__zero_std_codec_base64_encode", "std.codec"},
  {"std.codec.base64Decode", "__zero_std_codec_base64_decode", "std.codec"},
  {"std.codec.base64DecodedLen", "__zero_std_codec_base64_decoded_len", "std.codec"},
  {"std.codec.encodedVarintLen", "__zero_std_codec_encoded_varint_len", "std.codec"},
  {"std.codec.hexEncode", "__zero_std_codec_hex_encode", "std.codec"},
  {"std.codec.hexDecode", "__zero_std_codec_hex_decode", "std.codec"},
  {"std.codec.hexDecodedLen", "__zero_std_codec_hex_decoded_len", "std.codec"},
  {"std.codec.readU16Be", "__zero_std_codec_read_u16_be", "std.codec"},
  {"std.codec.readU16Le", "__zero_std_codec_read_u16_le", "std.codec"},
  {"std.codec.readU32Be", "__zero_std_codec_read_u32_be", "std.codec"},
  {"std.codec.readU32Le", "__zero_std_codec_read_u32_le", "std.codec"},
  {"std.codec.varintDecode", "__zero_std_codec_varint_decode", "std.codec"},
  {"std.codec.varintEncode", "__zero_std_codec_varint_encode", "std.codec"},
  {"std.codec.writeU16Be", "__zero_std_codec_write_u16_be", "std.codec"},
  {"std.codec.writeU16Le", "__zero_std_codec_write_u16_le", "std.codec"},
  {"std.codec.writeU32Be", "__zero_std_codec_write_u32_be", "std.codec"},
  {"std.codec.writeU32Le", "__zero_std_codec_write_u32_le", "std.codec"},
  {"std.codec.urlEncode", "__zero_std_codec_url_encode", "std.codec"},
  {"std.collections.append", "__zero_std_collections_append", "std.collections"},
  {"std.collections.contains", "__zero_std_collections_contains", "std.collections"},
  {"std.collections.count", "__zero_std_collections_count", "std.collections"},
  {"std.collections.moveToFront", "__zero_std_collections_move_to_front", "std.collections"},
  {"std.collections.push", "__zero_std_collections_push", "std.collections"},
  {"std.collections.removeSwap", "__zero_std_collections_remove_swap", "std.collections"},
  {"std.collections.view", "__zero_std_collections_view", "std.collections"},
  {"std.env.getOr", "__zero_std_env_get_or", "std.env"},
  {"std.env.has", "__zero_std_env_has", "std.env"},
  {"std.env.parseBool", "__zero_std_env_parse_bool", "std.env"},
  {"std.env.parseU32", "__zero_std_env_parse_u32", "std.env"},
  {"std.fs.copyFile", "__zero_std_fs_copy_file", "std.fs"},
  {"std.fs.readFile", "__zero_std_fs_read_file", "std.fs"},
  {"std.fs.writeFile", "__zero_std_fs_write_file", "std.fs"},
  {"std.http.headerBytes", "__zero_std_http_header_bytes", "std.http"},
  {"std.http.pathSegment", "__zero_std_http_path_segment", "std.http"},
  {"std.http.pathSegmentCount", "__zero_std_http_path_segment_count", "std.http"},
  {"std.http.requestBody", "__zero_std_http_request_body", "std.http"},
  {"std.http.requestBodyWithin", "__zero_std_http_request_body_within", "std.http"},
  {"std.http.requestBearerToken", "__zero_std_http_request_bearer_token", "std.http"},
  {"std.http.requestCookie", "__zero_std_http_request_cookie", "std.http"},
  {"std.http.requestHeader", "__zero_std_http_request_header", "std.http"},
  {"std.http.requestHasJsonContentType", "__zero_std_http_request_has_json_content_type", "std.http"},
  {"std.http.requestJsonBodyWithin", "__zero_std_http_request_json_body_within", "std.http"},
  {"std.http.requestMatches", "__zero_std_http_request_matches", "std.http"},
  {"std.http.requestMethodIs", "__zero_std_http_request_method_is", "std.http"},
  {"std.http.requestQuery", "__zero_std_http_request_query", "std.http"},
  {"std.http.requestQueryValue", "__zero_std_http_request_query_value", "std.http"},
  {"std.http.requestIsDelete", "__zero_std_http_request_is_delete", "std.http"},
  {"std.http.requestIsGet", "__zero_std_http_request_is_get", "std.http"},
  {"std.http.requestIsHead", "__zero_std_http_request_is_head", "std.http"},
  {"std.http.requestIsOptions", "__zero_std_http_request_is_options", "std.http"},
  {"std.http.requestIsPatch", "__zero_std_http_request_is_patch", "std.http"},
  {"std.http.requestIsPost", "__zero_std_http_request_is_post", "std.http"},
  {"std.http.requestIsPut", "__zero_std_http_request_is_put", "std.http"},
  {"std.http.requestPathSegment", "__zero_std_http_request_path_segment", "std.http"},
  {"std.http.requestPathSegmentCount", "__zero_std_http_request_path_segment_count", "std.http"},
  {"std.http.requestPathStartsWith", "__zero_std_http_request_path_starts_with", "std.http"},
  {"std.http.requestPathTailAfter", "__zero_std_http_request_path_tail_after", "std.http"},
  {"std.http.requestTarget", "__zero_std_http_request_target", "std.http"},
  {"std.http.responseBody", "__zero_std_http_response_body", "std.http"},
  {"std.http.responseBodyBytes", "__zero_std_http_response_body_bytes", "std.http"},
  {"std.http.statusReason", "__zero_std_http_status_reason", "std.http"},
  {"std.http.writeCorsJsonResponse", "__zero_std_http_write_cors_json_response", "std.http"},
  {"std.http.writeCorsPreflight", "__zero_std_http_write_cors_preflight", "std.http"},
  {"std.http.writeJsonBadRequest", "__zero_std_http_write_json_bad_request", "std.http"},
  {"std.http.writeJsonConflict", "__zero_std_http_write_json_conflict", "std.http"},
  {"std.http.writeJsonCreated", "__zero_std_http_write_json_created", "std.http"},
  {"std.http.writeJsonForbidden", "__zero_std_http_write_json_forbidden", "std.http"},
  {"std.http.writeJsonInternalServerError", "__zero_std_http_write_json_internal_server_error", "std.http"},
  {"std.http.writeJsonMethodNotAllowed", "__zero_std_http_write_json_method_not_allowed", "std.http"},
  {"std.http.writeJsonNotFound", "__zero_std_http_write_json_not_found", "std.http"},
  {"std.http.writeJsonOk", "__zero_std_http_write_json_ok", "std.http"},
  {"std.http.writeJsonRequest", "__zero_std_http_write_json_request", "std.http"},
  {"std.http.writeJsonTooManyRequests", "__zero_std_http_write_json_too_many_requests", "std.http"},
  {"std.http.writeJsonUnauthorized", "__zero_std_http_write_json_unauthorized", "std.http"},
  {"std.http.writeJsonUnprocessable", "__zero_std_http_write_json_unprocessable", "std.http"},
  {"std.http.writeNoContent", "__zero_std_http_write_no_content", "std.http"},
  {"std.http.writeRequest", "__zero_std_http_write_request", "std.http"},
  {"std.http.writeResponse", "__zero_std_http_write_response", "std.http"},
  {"std.io.countLines", "__zero_std_io_count_lines", "std.io"},
  {"std.io.nextLine", "__zero_std_io_next_line", "std.io"},
  {"std.io.nextLineStart", "__zero_std_io_next_line_start", "std.io"},
  {"std.io.writeByte", "__zero_std_io_write_byte", "std.io"},
  {"std.io.writeSpan", "__zero_std_io_write_span", "std.io"},
  {"std.io.written", "__zero_std_io_written", "std.io"},
  {"std.json.bool", "__zero_std_json_bool", "std.json"},
  {"std.json.field", "__zero_std_json_field", "std.json"},
  {"std.json.string", "__zero_std_json_string", "std.json"},
  {"std.json.stringDecode", "__zero_std_json_string_decode", "std.json"},
  {"std.json.streamTokens", "__zero_std_json_stream_tokens", "std.json"},
  {"std.json.streamTokensBytes", "__zero_std_json_stream_tokens_bytes", "std.json"},
  {"std.json.u32", "__zero_std_json_u32", "std.json"},
  {"std.json.validate", "__zero_std_json_validate", "std.json"},
  {"std.json.validateBytes", "__zero_std_json_validate_bytes", "std.json"},
  {"std.json.validateError", "__zero_std_json_validate_error", "std.json"},
  {"std.json.writeObject1Bool", "__zero_std_json_write_object1_bool", "std.json"},
  {"std.json.writeObject1String", "__zero_std_json_write_object1_string", "std.json"},
  {"std.json.writeObject1U32", "__zero_std_json_write_object1_u32", "std.json"},
  {"std.json.writeString", "__zero_std_json_write_string", "std.json"},
  {"std.json.writeStringBytes", "__zero_std_json_write_string_bytes", "std.json"},
  {"std.log.keyValue", "__zero_std_log_key_value", "std.log"},
  {"std.log.message", "__zero_std_log_message", "std.log"},
  {"std.mem.contains", "__zero_std_mem_contains", "std.mem"},
  {"std.mem.copyItems", "__zero_std_mem_copy_items", "std.mem"},
  {"std.mem.dropPrefix", "__zero_std_mem_drop_prefix", "std.mem"},
  {"std.mem.fillItems", "__zero_std_mem_fill_items", "std.mem"},
  {"std.mem.prefix", "__zero_std_mem_prefix", "std.mem"},
  {"std.net.localhost", "__zero_std_net_localhost", "std.net"},
  {"std.net.loopback", "__zero_std_net_loopback", "std.net"},
  {"std.path.join", "__zero_std_path_join", "std.path"},
  {"std.path.normalize", "__zero_std_path_normalize", "std.path"},
  {"std.path.relative", "__zero_std_path_relative", "std.path"},
  {"std.search.indexOf", "__zero_std_search_index_of", "std.search"},
  {"std.search.lastIndexOf", "__zero_std_search_last_index_of", "std.search"},
  {"std.toml.bool", "__zero_std_toml_bool", "std.toml"},
  {"std.toml.field", "__zero_std_toml_field", "std.toml"},
  {"std.toml.string", "__zero_std_toml_string", "std.toml"},
  {"std.toml.stringDecode", "__zero_std_toml_string_decode", "std.toml"},
  {"std.toml.u32", "__zero_std_toml_u32", "std.toml"},
  {"std.toml.validate", "__zero_std_toml_validate", "std.toml"},
  {"std.toml.validateBytes", "__zero_std_toml_validate_bytes", "std.toml"},
  {"std.url.appendQuery", "__zero_std_url_append_query", "std.url"},
  {"std.url.authority", "__zero_std_url_authority", "std.url"},
  {"std.url.host", "__zero_std_url_host", "std.url"},
  {"std.url.path", "__zero_std_url_path", "std.url"},
  {"std.url.percentDecode", "__zero_std_url_percent_decode", "std.url"},
  {"std.url.percentEncode", "__zero_std_url_percent_encode", "std.url"},
  {"std.url.query", "__zero_std_url_query", "std.url"},
  {"std.url.queryEscape", "__zero_std_url_query_escape", "std.url"},
  {"std.url.queryUnescape", "__zero_std_url_query_unescape", "std.url"},
  {"std.url.queryValue", "__zero_std_url_query_value", "std.url"},
  {"std.url.scheme", "__zero_std_url_scheme", "std.url"},
  {"std.url.writeQueryParam", "__zero_std_url_write_query_param", "std.url"},
};

static bool std_source_text_eq(const char *left, const char *right) {
  left = left ? left : "";
  right = right ? right : "";
  size_t left_len = strlen(left);
  return left_len == strlen(right) && memcmp(left, right, left_len) == 0;
}

static uint64_t std_source_hash_text(uint64_t hash, const char *text) {
  const unsigned char *bytes = (const unsigned char *)(text ? text : "");
  while (*bytes) {
    hash ^= (uint64_t)*bytes++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t std_source_hash_bytes(uint64_t hash, const unsigned char *bytes, size_t len) {
  hash ^= (uint64_t)len;
  hash *= 1099511628211ull;
  for (size_t i = 0; bytes && i < len; i++) {
    hash ^= (uint64_t)bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

uint64_t z_std_source_graph_fingerprint(void) {
  static uint64_t cached = 0;
  if (cached != 0) return cached;
  uint64_t hash = 1469598103934665603ull;
  hash = std_source_hash_text(hash, "zero-embedded-stdlib-graph-v1");
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    hash = std_source_hash_text(hash, module ? module->module : "");
    hash = std_source_hash_text(hash, module ? module->path : "");
    hash = std_source_hash_bytes(hash, module ? module->graph_bytes : NULL, module ? module->graph_len : 0);
  }
  cached = hash ? hash : 1;
  return cached;
}

size_t z_std_source_module_count(void) {
  return sizeof(std_source_modules) / sizeof(std_source_modules[0]);
}

const ZStdSourceModule *z_std_source_module_at(size_t index) {
  return index < z_std_source_module_count() ? &std_source_modules[index] : NULL;
}

const ZStdSourceModule *z_std_source_module_for_name(const char *module) {
  for (size_t i = 0; i < sizeof(std_source_modules) / sizeof(std_source_modules[0]); i++) {
    if (std_source_text_eq(std_source_modules[i].module, module)) return &std_source_modules[i];
  }
  return NULL;
}

static const char *std_source_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static bool std_source_stem_eq(const char *left, const char *right) {
  const char *left_base = std_source_basename(left);
  const char *right_base = std_source_basename(right);
  const char *left_dot = strrchr(left_base, '.');
  const char *right_dot = strrchr(right_base, '.');
  size_t left_len = left_dot ? (size_t)(left_dot - left_base) : strlen(left_base);
  size_t right_len = right_dot ? (size_t)(right_dot - right_base) : strlen(right_base);
  return left_len == right_len && strncmp(left_base, right_base, left_len) == 0;
}

const ZStdSourceModule *z_std_source_module_for_path(const char *path) {
  for (size_t i = 0; path && i < sizeof(std_source_modules) / sizeof(std_source_modules[0]); i++) {
    const ZStdSourceModule *module = &std_source_modules[i];
    if (std_source_text_eq(module->path, path) ||
        std_source_text_eq(std_source_basename(module->path), std_source_basename(path)) ||
        std_source_stem_eq(module->path, path)) {
      return module;
    }
  }
  return NULL;
}

bool z_std_source_path_is_module_artifact(const char *path) {
  return path && path[0] &&
         (strncmp(path, "std/", strlen("std/")) == 0 || strstr(path, "/std/") != NULL) &&
         z_std_source_module_for_path(path) != NULL;
}

static const ZStdSourceCall *std_source_call_for_name(const char *qualified_name) {
  for (size_t i = 0; i < sizeof(std_source_calls) / sizeof(std_source_calls[0]); i++) {
    if (std_source_text_eq(std_source_calls[i].public_name, qualified_name)) return &std_source_calls[i];
  }
  return NULL;
}

const ZStdSourceModule *z_std_source_module_for_public_call(const char *qualified_name) {
  const ZStdSourceCall *call = std_source_call_for_name(qualified_name);
  return call ? z_std_source_module_for_name(call->module) : NULL;
}

const char *z_std_source_target_for_public_call(const char *qualified_name) {
  const ZStdSourceCall *call = std_source_call_for_name(qualified_name);
  return call ? call->target_name : NULL;
}

bool z_std_source_module_load_graph(const ZStdSourceModule *module, ZProgramGraph *out, ZDiag *diag) {
  if (!module || !module->graph_bytes || module->graph_len == 0) {
    if (diag) {
      *diag = (ZDiag){0};
      diag->code = 1001;
      diag->path = module ? module->path : "std";
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "embedded stdlib graph is missing");
      snprintf(diag->expected, sizeof(diag->expected), "binary graph bytes");
      snprintf(diag->actual, sizeof(diag->actual), "%s", module && module->module ? module->module : "unknown std module");
    }
    return false;
  }
  if (!z_program_graph_store_bytes_are_binary(module->graph_bytes, module->graph_len)) {
    char *text = z_checked_malloc(module->graph_len + 1);
    memcpy(text, module->graph_bytes, module->graph_len);
    text[module->graph_len] = '\0';
    bool ok = z_program_graph_parse_dump(text, out, diag);
    free(text);
    if (ok) {
      ZProgramGraphValidation validation = {0};
      ok = z_program_graph_validate(out, &validation);
      if (!ok && diag) {
        *diag = (ZDiag){0};
        diag->code = 1001;
        diag->path = module->path;
        diag->line = 1;
        diag->column = 1;
        diag->length = 1;
        snprintf(diag->message, sizeof(diag->message), "embedded stdlib graph failed validation: %s",
                 validation.message[0] ? validation.message : "invalid graph shape");
        snprintf(diag->expected, sizeof(diag->expected), "shape-valid program graph");
        snprintf(diag->actual, sizeof(diag->actual), "%s", validation.code[0] ? validation.code : "invalid graph");
      }
    }
    if (!ok) {
      if (diag && !diag->path) diag->path = module->path;
      if (out) z_program_graph_free(out);
    }
    return ok;
  }
  ZProgramGraphStore store;
  z_program_graph_store_init(&store);
  bool ok = z_program_graph_store_parse_binary(module->path, module->graph_bytes, module->graph_len, &store, diag);
  if (ok && out) {
    *out = store.graph;
    store.graph = (ZProgramGraph){0};
  }
  z_program_graph_store_free(&store);
  if (!ok && diag && !diag->path) diag->path = module->path;
  return ok;
}
