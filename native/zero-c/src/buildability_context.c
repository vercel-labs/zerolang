#include "buildability_internal.h"

#include <stdio.h>
#include <string.h>

const char *z_build_type_name(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_VOID: return "Void";
    case IR_TYPE_BOOL: return "Bool";
    case IR_TYPE_U8: return "u8";
    case IR_TYPE_U16: return "u16";
    case IR_TYPE_USIZE: return "usize";
    case IR_TYPE_I32: return "i32";
    case IR_TYPE_U32: return "u32";
    case IR_TYPE_I64: return "i64";
    case IR_TYPE_U64: return "u64";
    case IR_TYPE_BYTE_VIEW: return "Span<u8>";
    case IR_TYPE_ALLOC: return "FixedBufAlloc";
    case IR_TYPE_VEC: return "Vec";
    case IR_TYPE_MAYBE_BYTE_VIEW: return "Maybe<MutSpan<u8>>";
    case IR_TYPE_MAYBE_SCALAR: return "Maybe<usize>";
    case IR_TYPE_RECORD: return "record";
    default: return "unsupported";
  }
}

const char *z_build_value_kind_name(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_INT: return "IR_VALUE_INT";
    case IR_VALUE_BOOL: return "IR_VALUE_BOOL";
    case IR_VALUE_LOCAL: return "IR_VALUE_LOCAL";
    case IR_VALUE_CAST: return "IR_VALUE_CAST";
    case IR_VALUE_BINARY: return "IR_VALUE_BINARY";
    case IR_VALUE_COMPARE: return "IR_VALUE_COMPARE";
    case IR_VALUE_CALL: return "IR_VALUE_CALL";
    case IR_VALUE_INDEX_LOAD: return "IR_VALUE_INDEX_LOAD";
    case IR_VALUE_STRING_LITERAL: return "IR_VALUE_STRING_LITERAL";
    case IR_VALUE_ARRAY_BYTE_VIEW: return "IR_VALUE_ARRAY_BYTE_VIEW";
    case IR_VALUE_BYTE_SLICE: return "IR_VALUE_BYTE_SLICE";
    case IR_VALUE_BYTE_VIEW_LEN: return "IR_VALUE_BYTE_VIEW_LEN";
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: return "IR_VALUE_BYTE_VIEW_INDEX_LOAD";
    case IR_VALUE_BYTE_VIEW_EQ: return "IR_VALUE_BYTE_VIEW_EQ";
    case IR_VALUE_BYTE_COPY: return "IR_VALUE_BYTE_COPY";
    case IR_VALUE_BYTE_FILL: return "IR_VALUE_BYTE_FILL";
    case IR_VALUE_CRC32_BYTES: return "IR_VALUE_CRC32_BYTES";
    case IR_VALUE_FIXED_BUF_ALLOC: return "IR_VALUE_FIXED_BUF_ALLOC";
    case IR_VALUE_VEC_INIT: return "IR_VALUE_VEC_INIT";
    case IR_VALUE_VEC_PUSH: return "IR_VALUE_VEC_PUSH";
    case IR_VALUE_VEC_LEN: return "IR_VALUE_VEC_LEN";
    case IR_VALUE_VEC_CAPACITY: return "IR_VALUE_VEC_CAPACITY";
    case IR_VALUE_ALLOC_BYTES: return "IR_VALUE_ALLOC_BYTES";
    case IR_VALUE_MAYBE_HAS: return "IR_VALUE_MAYBE_HAS";
    case IR_VALUE_MAYBE_VALUE: return "IR_VALUE_MAYBE_VALUE";
    case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL: return "IR_VALUE_MAYBE_BYTE_VIEW_LITERAL";
    case IR_VALUE_MAYBE_SCALAR_LITERAL: return "IR_VALUE_MAYBE_SCALAR_LITERAL";
    case IR_VALUE_ARGS_LEN: return "IR_VALUE_ARGS_LEN";
    case IR_VALUE_ARGS_GET: return "IR_VALUE_ARGS_GET";
    case IR_VALUE_ENV_GET: return "IR_VALUE_ENV_GET";
    case IR_VALUE_TIME_WALL_SECONDS: return "IR_VALUE_TIME_WALL_SECONDS";
    case IR_VALUE_TIME_MONOTONIC: return "IR_VALUE_TIME_MONOTONIC";
    case IR_VALUE_TIME_AS_MS: return "IR_VALUE_TIME_AS_MS";
    case IR_VALUE_RAND_NEXT_U32: return "IR_VALUE_RAND_NEXT_U32";
    case IR_VALUE_RAND_ENTROPY_U32: return "IR_VALUE_RAND_ENTROPY_U32";
    case IR_VALUE_FS_HOST: return "IR_VALUE_FS_HOST";
    case IR_VALUE_FS_OPEN: return "IR_VALUE_FS_OPEN";
    case IR_VALUE_FS_CREATE: return "IR_VALUE_FS_CREATE";
    case IR_VALUE_FS_READ_PATH: return "IR_VALUE_FS_READ_PATH";
    case IR_VALUE_FS_WRITE_PATH: return "IR_VALUE_FS_WRITE_PATH";
    case IR_VALUE_FS_READ_BYTES_PATH: return "IR_VALUE_FS_READ_BYTES_PATH";
    case IR_VALUE_FS_WRITE_BYTES_PATH: return "IR_VALUE_FS_WRITE_BYTES_PATH";
    case IR_VALUE_FS_READ_ALL: return "IR_VALUE_FS_READ_ALL";
    case IR_VALUE_FS_READ_FILE: return "IR_VALUE_FS_READ_FILE";
    case IR_VALUE_FS_WRITE_ALL_FILE: return "IR_VALUE_FS_WRITE_ALL_FILE";
    case IR_VALUE_FS_CLOSE_FILE: return "IR_VALUE_FS_CLOSE_FILE";
    case IR_VALUE_FS_EXISTS: return "IR_VALUE_FS_EXISTS";
    case IR_VALUE_FS_REMOVE: return "IR_VALUE_FS_REMOVE";
    case IR_VALUE_FS_RENAME: return "IR_VALUE_FS_RENAME";
    case IR_VALUE_FS_FILE_LEN: return "IR_VALUE_FS_FILE_LEN";
    case IR_VALUE_FS_MAKE_DIR: return "IR_VALUE_FS_MAKE_DIR";
    case IR_VALUE_FS_REMOVE_DIR: return "IR_VALUE_FS_REMOVE_DIR";
    case IR_VALUE_FS_IS_DIR: return "IR_VALUE_FS_IS_DIR";
    case IR_VALUE_FS_DIR_ENTRY_COUNT: return "IR_VALUE_FS_DIR_ENTRY_COUNT";
    case IR_VALUE_FS_TEMP_NAME: return "IR_VALUE_FS_TEMP_NAME";
    case IR_VALUE_FS_ATOMIC_WRITE: return "IR_VALUE_FS_ATOMIC_WRITE";
    case IR_VALUE_JSON_PARSE_BYTES: return "IR_VALUE_JSON_PARSE_BYTES";
    case IR_VALUE_JSON_VALIDATE_BYTES: return "IR_VALUE_JSON_VALIDATE_BYTES";
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES: return "IR_VALUE_JSON_STREAM_TOKENS_BYTES";
    case IR_VALUE_HTTP_FETCH: return "IR_VALUE_HTTP_FETCH";
    case IR_VALUE_HTTP_RESULT_OK: return "IR_VALUE_HTTP_RESULT_OK";
    case IR_VALUE_HTTP_RESULT_STATUS: return "IR_VALUE_HTTP_RESULT_STATUS";
    case IR_VALUE_HTTP_RESULT_BODY_LEN: return "IR_VALUE_HTTP_RESULT_BODY_LEN";
    case IR_VALUE_HTTP_RESULT_ERROR: return "IR_VALUE_HTTP_RESULT_ERROR";
    case IR_VALUE_HTTP_RESPONSE_LEN: return "IR_VALUE_HTTP_RESPONSE_LEN";
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN: return "IR_VALUE_HTTP_RESPONSE_HEADERS_LEN";
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: return "IR_VALUE_HTTP_RESPONSE_BODY_OFFSET";
    case IR_VALUE_HTTP_HEADER_VALUE: return "IR_VALUE_HTTP_HEADER_VALUE";
    case IR_VALUE_HTTP_HEADER_FOUND: return "IR_VALUE_HTTP_HEADER_FOUND";
    case IR_VALUE_HTTP_HEADER_OFFSET: return "IR_VALUE_HTTP_HEADER_OFFSET";
    case IR_VALUE_HTTP_HEADER_LEN: return "IR_VALUE_HTTP_HEADER_LEN";
    case IR_VALUE_FIELD_LOAD: return "IR_VALUE_FIELD_LOAD";
    case IR_VALUE_CHECK: return "IR_VALUE_CHECK";
    case IR_VALUE_RESCUE: return "IR_VALUE_RESCUE";
  }
  return "IR_VALUE_UNKNOWN";
}

bool z_build_is_elf_scalar(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE ||
         type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

bool z_build_is_scalar32(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE ||
         type == IR_TYPE_I32 || type == IR_TYPE_U32;
}

bool z_build_diag(const ZBuildability *ctx, ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (!diag) return false;
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "direct backend buildability check failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", ctx && ctx->expected ? ctx->expected : "direct backend buildability subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual && actual[0] ? actual : "unsupported construct");
  snprintf(diag->help, sizeof(diag->help), "%s", ctx && ctx->help ? ctx->help : "choose a supported direct target or simplify the program for this backend");
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        ctx && ctx->target && ctx->target->name ? ctx->target->name : "unknown",
                        ctx && ctx->target && ctx->target->object_format ? ctx->target->object_format : "unknown",
                        ctx && ctx->backend_name ? ctx->backend_name : "unknown",
                        "buildability",
                        diag->actual);
  z_diag_set_backend_blocker(diag, &blocker);
  return false;
}

static const char *build_expected_for_backend(ZDirectBackend backend, bool executable) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF64: return executable ? "direct ELF64 executable buildability subset" : "direct ELF64 object buildability subset";
    case Z_DIRECT_BACKEND_ELF_AARCH64: return executable ? "direct AArch64 ELF executable subset" : "direct AArch64 ELF object subset";
    case Z_DIRECT_BACKEND_MACHO64: return executable ? "direct AArch64 Mach-O executable buildability subset" : "direct AArch64 Mach-O object buildability subset";
    case Z_DIRECT_BACKEND_MACHO_X64: return executable ? "direct x86_64 Mach-O executable buildability subset" : "direct x86_64 Mach-O object buildability subset";
    case Z_DIRECT_BACKEND_COFF_X64: return executable ? "direct COFF x64 executable buildability subset" : "direct COFF x64 object buildability subset";
    case Z_DIRECT_BACKEND_COFF_AARCH64: return executable ? "direct COFF AArch64 executable subset" : "direct COFF AArch64 object subset";
    default: return "direct backend buildability subset";
  }
}

static const char *build_help_for_backend(ZDirectBackend backend) {
  switch (backend) {
    case Z_DIRECT_BACKEND_ELF_AARCH64:
      return "choose a supported direct target or reduce the program to AArch64 ELF supported direct-backend constructs";
    case Z_DIRECT_BACKEND_COFF_X64:
      return "reduce the program to primitive direct-backend constructs or choose a supported direct target";
    case Z_DIRECT_BACKEND_COFF_AARCH64:
      return "choose a supported direct target or reduce the program to AArch64 COFF supported direct-backend constructs";
    case Z_DIRECT_BACKEND_MACHO_X64:
      return "choose a supported direct target or reduce the program to x86_64 Mach-O supported direct-backend constructs";
    case Z_DIRECT_BACKEND_MACHO64:
      return "choose a supported direct target or reduce the program to Mach-O supported direct-backend constructs";
    default:
      return "choose a supported direct target or restrict this program to the ELF64 direct-backend subset";
  }
}

bool z_build_select(const IrProgram *ir, const ZTargetInfo *target, const char *emit_kind, ZBuildability *ctx, ZDiag *diag) {
  memset(ctx, 0, sizeof(*ctx));
  bool executable = emit_kind && strcmp(emit_kind, "exe") == 0;
  ctx->target = target;
  ctx->emit_kind = executable ? "exe" : "obj";
  ctx->executable = executable;
  ctx->backend = executable ? z_direct_exe_backend(target) : z_direct_object_backend(target);
  ctx->backend_name = executable ? z_direct_backend_exe_emitter(ctx->backend) : z_direct_backend_object_emitter(ctx->backend);
  ctx->expected = build_expected_for_backend(ctx->backend, executable);
  ctx->help = build_help_for_backend(ctx->backend);
  if (!ir) return z_build_diag(ctx, diag, "direct backend buildability requires MIR", 1, 1, "missing MIR");
  if (ctx->backend == Z_DIRECT_BACKEND_NONE) {
    return z_build_diag(ctx, diag, "direct backend does not support this target and artifact kind", 1, 1, target && target->name ? target->name : "unknown target");
  }
  return true;
}
