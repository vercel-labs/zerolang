#include "buildability_internal.h"

static bool build_backend_is_native_graph_runtime(ZDirectBackend backend) {
  return backend == Z_DIRECT_BACKEND_ELF64 || backend == Z_DIRECT_BACKEND_MACHO64;
}

static bool build_backend_has_byte_runtime(ZDirectBackend backend) {
  return build_backend_is_native_graph_runtime(backend) ||
         backend == Z_DIRECT_BACKEND_MACHO_X64 ||
         backend == Z_DIRECT_BACKEND_COFF_X64;
}

static bool build_backend_supports_hosted_runtime(const ZBuildability *ctx) {
  return ctx && (build_backend_is_native_graph_runtime(ctx->backend) ||
                 (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && !ctx->executable));
}

static bool build_backend_supports_json_parse(const ZBuildability *ctx) {
  if (!ctx) return false;
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64) return !ctx->executable;
  return build_backend_is_native_graph_runtime(ctx->backend);
}

static bool build_backend_supports_json_validate(const ZBuildability *ctx) {
  if (!ctx) return false;
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 || ctx->backend == Z_DIRECT_BACKEND_COFF_X64) return !ctx->executable;
  return build_backend_is_native_graph_runtime(ctx->backend);
}

static bool build_backend_supports_aarch64_hosted_runtime(ZDirectBackend backend) {
  return backend == Z_DIRECT_BACKEND_ELF_AARCH64 || backend == Z_DIRECT_BACKEND_COFF_AARCH64;
}

static bool build_value_supported_aarch64(ZDirectBackend backend, IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
    case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN: case IR_VALUE_BYTE_VIEW_REMAINING:
    case IR_VALUE_JSON_ERROR_LABEL:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL:
    case IR_VALUE_ITEM_COPY: case IR_VALUE_ITEM_FILL: case IR_VALUE_ITEM_CONTAINS:
    case IR_VALUE_BYTE_VIEW_EQ:
    case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD: case IR_VALUE_RECORD_ADDR:
    case IR_VALUE_MAYBE_HAS: case IR_VALUE_MAYBE_VALUE: case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
    case IR_VALUE_MAYBE_SCALAR_LITERAL: case IR_VALUE_RAND_NEXT_U32: case IR_VALUE_RAND_NEXT_BELOW: case IR_VALUE_RAND_RANGE_U32: case IR_VALUE_CRC32_BYTES:
      return true;
    case IR_VALUE_ASCII_RUNTIME:
    case IR_VALUE_TEXT_RUNTIME:
    case IR_VALUE_PARSE_RUNTIME:
    case IR_VALUE_STR_RUNTIME:
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32:
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE:
    case IR_VALUE_TIME_RUNTIME:
    case IR_VALUE_TERM_RUNTIME:
    case IR_VALUE_MATH_RUNTIME:
    case IR_VALUE_SEARCH_RUNTIME:
    case IR_VALUE_SORT_RUNTIME:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
    case IR_VALUE_JSON_FIELD:
    case IR_VALUE_JSON_LOOKUP_SCALAR:
    case IR_VALUE_JSON_STRING_DECODE:
    case IR_VALUE_JSON_STRING_FIELD:
    case IR_VALUE_JSON_WRITE_STRING:
    case IR_VALUE_JSON_WRITE_RUNTIME:
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME:
    case IR_VALUE_HTTP_REQUEST_PATH:
    case IR_VALUE_PROC_SPAWN_INHERIT:
    case IR_VALUE_PROC_CAPTURE:
    case IR_VALUE_PROC_CAPTURE_FILES:
    case IR_VALUE_PROC_CHILD_SPAWN:
    case IR_VALUE_PROC_CHILD_OP:
    case IR_VALUE_PROC_CHILD_IO:
    case IR_VALUE_PROC_PTY_RESIZE:
      return build_backend_supports_aarch64_hosted_runtime(backend);
    default:
      return false;
  }
}

static bool build_value_supported_macho_x64(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
    case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN: case IR_VALUE_BYTE_VIEW_REMAINING:
    case IR_VALUE_JSON_ERROR_LABEL:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL:
    case IR_VALUE_ITEM_COPY: case IR_VALUE_ITEM_FILL: case IR_VALUE_ITEM_CONTAINS:
    case IR_VALUE_BYTE_VIEW_EQ:
    case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD: case IR_VALUE_RECORD_ADDR: case IR_VALUE_CHECK:
    case IR_VALUE_MAYBE_HAS: case IR_VALUE_MAYBE_VALUE: case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL: case IR_VALUE_MAYBE_SCALAR_LITERAL:
    case IR_VALUE_RAND_NEXT_U32: case IR_VALUE_RAND_NEXT_BELOW: case IR_VALUE_RAND_RANGE_U32: case IR_VALUE_CRC32_BYTES:
    case IR_VALUE_ASCII_RUNTIME: case IR_VALUE_TEXT_RUNTIME: case IR_VALUE_PARSE_RUNTIME:
    case IR_VALUE_PARSE_I32: case IR_VALUE_PARSE_U32:
    case IR_VALUE_FMT_BOOL: case IR_VALUE_FMT_HEX_U32: case IR_VALUE_FMT_I32: case IR_VALUE_FMT_U32: case IR_VALUE_FMT_USIZE:
    case IR_VALUE_STR_RUNTIME: case IR_VALUE_TIME_RUNTIME: case IR_VALUE_TERM_RUNTIME: case IR_VALUE_MATH_RUNTIME:
    case IR_VALUE_SEARCH_RUNTIME: case IR_VALUE_SORT_RUNTIME:
    case IR_VALUE_JSON_PARSE_BYTES: case IR_VALUE_JSON_VALIDATE_BYTES: case IR_VALUE_JSON_STREAM_TOKENS_BYTES: case IR_VALUE_JSON_DIAGNOSTIC_BYTES: case IR_VALUE_JSON_FIELD: case IR_VALUE_JSON_LOOKUP_SCALAR: case IR_VALUE_JSON_STRING_DECODE: case IR_VALUE_JSON_STRING_FIELD: case IR_VALUE_JSON_WRITE_STRING: case IR_VALUE_JSON_WRITE_RUNTIME:
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH:
    case IR_VALUE_PROC_SPAWN_INHERIT:
    case IR_VALUE_PROC_CAPTURE:
    case IR_VALUE_PROC_CAPTURE_FILES:
    case IR_VALUE_PROC_CHILD_SPAWN:
    case IR_VALUE_PROC_CHILD_OP:
    case IR_VALUE_PROC_CHILD_IO:
    case IR_VALUE_PROC_PTY_RESIZE:
      return true;
    default:
      return false;
  }
}

static bool build_value_supported_generic(const ZBuildability *ctx, const IrValue *value, bool local_set_value) {
  switch (value->kind) {
    case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
    case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN: case IR_VALUE_BYTE_VIEW_REMAINING:
    case IR_VALUE_JSON_ERROR_LABEL:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD: case IR_VALUE_RECORD_ADDR:
      return true;
    case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
      return true;
    case IR_VALUE_MAYBE_SCALAR_LITERAL:
      return true;
    case IR_VALUE_FIXED_BUF_ALLOC: case IR_VALUE_VEC_INIT: case IR_VALUE_ALLOC_BYTES:
      return local_set_value;
    case IR_VALUE_VEC_PUSH: case IR_VALUE_VEC_LEN: case IR_VALUE_VEC_CAPACITY:
    case IR_VALUE_VEC_BYTES: case IR_VALUE_VEC_GET: case IR_VALUE_VEC_SET: case IR_VALUE_VEC_CLEAR: case IR_VALUE_VEC_POP: case IR_VALUE_VEC_TRUNCATE: case IR_VALUE_VEC_REMOVE_SWAP:
    case IR_VALUE_VEC_INDEX: case IR_VALUE_VEC_CONTAINS: case IR_VALUE_VEC_INSERT_UNIQUE: case IR_VALUE_VEC_REMOVE_VALUE: case IR_VALUE_MAYBE_HAS:
      return true;
    case IR_VALUE_ARGS_GET:
      return build_backend_is_native_graph_runtime(ctx->backend) ? local_set_value : false;
    case IR_VALUE_ARGS_EQ:
      return build_backend_is_native_graph_runtime(ctx->backend);
    case IR_VALUE_ARGS_GET_OR:
      return build_backend_is_native_graph_runtime(ctx->backend);
    case IR_VALUE_ARGS_VALUE_AFTER:
      return build_backend_is_native_graph_runtime(ctx->backend) ? local_set_value : false;
    case IR_VALUE_ARGS_VALUE_AFTER_OR:
      return build_backend_is_native_graph_runtime(ctx->backend);
    case IR_VALUE_ARGS_LEN:
      return build_backend_is_native_graph_runtime(ctx->backend);
    case IR_VALUE_ENV_GET:
      return build_backend_is_native_graph_runtime(ctx->backend) && local_set_value;
    case IR_VALUE_TIME_WALL_SECONDS: case IR_VALUE_TIME_MONOTONIC: case IR_VALUE_TIME_AS_MS:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64;
    case IR_VALUE_RAND_ENTROPY_U32:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_RAND_NEXT_U32:
      return build_backend_has_byte_runtime(ctx->backend);
    case IR_VALUE_RAND_NEXT_BELOW:
    case IR_VALUE_RAND_RANGE_U32:
      return build_backend_has_byte_runtime(ctx->backend);
    case IR_VALUE_FS_HOST:
      return build_backend_is_native_graph_runtime(ctx->backend);
    case IR_VALUE_FS_READ_BYTES_PATH: case IR_VALUE_FS_READ_BYTES_AT_PATH: case IR_VALUE_FS_WRITE_BYTES_PATH: case IR_VALUE_FS_APPEND_BYTES_PATH:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_FS_EXISTS: case IR_VALUE_FS_REMOVE: case IR_VALUE_FS_RENAME: case IR_VALUE_FS_ATOMIC_WRITE:
    case IR_VALUE_FS_MAKE_DIR: case IR_VALUE_FS_REMOVE_DIR: case IR_VALUE_FS_IS_DIR:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_FS_OPEN: case IR_VALUE_FS_CREATE: case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_READ_ALL: case IR_VALUE_FS_READ_FILE: case IR_VALUE_FS_WRITE_ALL_FILE:
    case IR_VALUE_FS_CLOSE_FILE:
    case IR_VALUE_FS_FILE_LEN:
    case IR_VALUE_FS_TEMP_NAME:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64;
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_FS_DIR_ENTRY_NAME:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_CRC32_BYTES:
      return build_backend_has_byte_runtime(ctx->backend) || z_build_backend_is_aarch64_direct(ctx->backend);
    case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL:
    case IR_VALUE_ITEM_COPY: case IR_VALUE_ITEM_FILL: case IR_VALUE_ITEM_CONTAINS:
      return build_backend_has_byte_runtime(ctx->backend);
    case IR_VALUE_BYTE_VIEW_EQ:
      return build_backend_has_byte_runtime(ctx->backend);
    case IR_VALUE_STR_RUNTIME:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_ASCII_RUNTIME:
    case IR_VALUE_TEXT_RUNTIME:
    case IR_VALUE_PARSE_RUNTIME:
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32:
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE:
    case IR_VALUE_PROC_SPAWN_INHERIT:
    case IR_VALUE_PROC_CAPTURE:
    case IR_VALUE_PROC_CAPTURE_FILES:
    case IR_VALUE_PROC_CHILD_SPAWN:
    case IR_VALUE_PROC_CHILD_OP:
    case IR_VALUE_PROC_CHILD_IO:
    case IR_VALUE_PROC_PTY_RESIZE:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_TIME_RUNTIME:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_TERM_RUNTIME:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_MATH_RUNTIME:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_STR_CONTAINS:
    case IR_VALUE_SEARCH_RUNTIME: case IR_VALUE_SORT_RUNTIME:
    case IR_VALUE_ARGS_PARSE_U32: case IR_VALUE_ARGS_FIND: case IR_VALUE_ARGS_CONTAINS:
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
      return build_backend_supports_hosted_runtime(ctx);
    case IR_VALUE_CHECK: return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 || ctx->backend == Z_DIRECT_BACKEND_MACHO_X64;
    case IR_VALUE_RESCUE: return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_MAYBE_VALUE:
      return build_backend_has_byte_runtime(ctx->backend);
    case IR_VALUE_JSON_PARSE_BYTES:
      return build_backend_supports_json_parse(ctx);
    case IR_VALUE_JSON_VALIDATE_BYTES: case IR_VALUE_JSON_STREAM_TOKENS_BYTES: case IR_VALUE_JSON_DIAGNOSTIC_BYTES: case IR_VALUE_JSON_FIELD: case IR_VALUE_JSON_LOOKUP_SCALAR: case IR_VALUE_JSON_STRING_DECODE: case IR_VALUE_JSON_STRING_FIELD: case IR_VALUE_JSON_WRITE_STRING: case IR_VALUE_JSON_WRITE_RUNTIME:
      return build_backend_supports_json_validate(ctx);
    case IR_VALUE_HTTP_FETCH: case IR_VALUE_HTTP_RESULT_OK: case IR_VALUE_HTTP_RESULT_STATUS: case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR: case IR_VALUE_HTTP_RESPONSE_LEN: case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: case IR_VALUE_HTTP_HEADER_VALUE: case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET: case IR_VALUE_HTTP_HEADER_LEN: case IR_VALUE_HTTP_WRITE_JSON_RESPONSE:
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH: case IR_VALUE_HTTP_REQUEST_MATCHES:
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN:
    case IR_VALUE_HTTP_STATUS_CLASS:
      return build_backend_is_native_graph_runtime(ctx->backend) ||
             ((value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME || value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
               value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN) &&
              ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && !ctx->executable);
  }
  return false;
}

bool z_build_value_supported(const ZBuildability *ctx, const IrValue *value, bool local_set_value) {
  if (!ctx || !value) return false;
  if (z_build_backend_is_aarch64_direct(ctx->backend)) return build_value_supported_aarch64(ctx->backend, value->kind);
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64) return build_value_supported_macho_x64(value->kind);
  return build_value_supported_generic(ctx, value, local_set_value);
}
