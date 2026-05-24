#include "buildability_internal.h"

#include <stdio.h>
#include <string.h>

static bool build_value_supported(const ZBuildability *ctx, const IrValue *value, bool local_set_value) {
  if (!ctx || !value) return false;
  if (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64) return true;
  switch (value->kind) {
    case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
    case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD:
      return true;
    case IR_VALUE_FIXED_BUF_ALLOC: case IR_VALUE_VEC_INIT: case IR_VALUE_ALLOC_BYTES: case IR_VALUE_MAYBE_SCALAR_LITERAL:
      return local_set_value;
    case IR_VALUE_VEC_PUSH: case IR_VALUE_VEC_LEN: case IR_VALUE_VEC_CAPACITY: case IR_VALUE_MAYBE_HAS:
      return true;
    case IR_VALUE_ARGS_GET:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 ? local_set_value : false;
    case IR_VALUE_ARGS_LEN:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_ENV_GET:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 && local_set_value;
    case IR_VALUE_TIME_WALL_SECONDS: case IR_VALUE_TIME_MONOTONIC: case IR_VALUE_TIME_AS_MS:
    case IR_VALUE_RAND_NEXT_U32: case IR_VALUE_RAND_ENTROPY_U32:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64;
    case IR_VALUE_FS_HOST: case IR_VALUE_FS_OPEN: case IR_VALUE_FS_CREATE: case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_WRITE_PATH: case IR_VALUE_FS_READ_BYTES_PATH: case IR_VALUE_FS_WRITE_BYTES_PATH:
    case IR_VALUE_FS_READ_ALL: case IR_VALUE_FS_READ_FILE: case IR_VALUE_FS_WRITE_ALL_FILE:
    case IR_VALUE_FS_CLOSE_FILE: case IR_VALUE_FS_EXISTS: case IR_VALUE_FS_REMOVE: case IR_VALUE_FS_RENAME:
    case IR_VALUE_FS_FILE_LEN: case IR_VALUE_FS_MAKE_DIR: case IR_VALUE_FS_REMOVE_DIR: case IR_VALUE_FS_IS_DIR:
    case IR_VALUE_FS_DIR_ENTRY_COUNT: case IR_VALUE_FS_TEMP_NAME: case IR_VALUE_FS_ATOMIC_WRITE:
    case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_VIEW_EQ: case IR_VALUE_CRC32_BYTES:
    case IR_VALUE_CHECK: case IR_VALUE_RESCUE:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64;
    case IR_VALUE_MAYBE_VALUE:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && value->type == IR_TYPE_BYTE_VIEW);
    case IR_VALUE_JSON_PARSE_BYTES: case IR_VALUE_JSON_VALIDATE_BYTES: case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_CODEC_HEX_ENCODE: case IR_VALUE_CODEC_UTF8_VALID:
    case IR_VALUE_HTTP_FETCH: case IR_VALUE_HTTP_RESULT_OK: case IR_VALUE_HTTP_RESULT_STATUS: case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR: case IR_VALUE_HTTP_RESPONSE_LEN: case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: case IR_VALUE_HTTP_HEADER_VALUE: case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET: case IR_VALUE_HTTP_HEADER_LEN:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_BYTE_FILL:
      return false;
  }
  return false;
}

static bool build_check_value(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, bool local_set_value, unsigned macho_scratch_slot, ZDiag *diag) {
  if (!value) return z_build_diag(ctx, diag, "direct backend buildability found a missing expression", 1, 1, "missing expression");
  if (!build_value_supported(ctx, value, local_set_value)) {
    return z_build_diag(ctx, diag, "direct backend buildability does not support this MIR value", value->line, value->column, z_build_value_kind_name(value->kind));
  }
  bool skip_left = false;
  if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64) {
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_coff_byte_view_len(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT) && !z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
    skip_left = value->kind == IR_VALUE_BYTE_VIEW_LEN || value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD || value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT;
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64) {
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_macho_byte_view_len(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT) && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_JSON_PARSE_BYTES || value->kind == IR_VALUE_JSON_VALIDATE_BYTES || value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES || value->kind == IR_VALUE_CODEC_UTF8_VALID) &&
        !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_CODEC_HEX_ENCODE) {
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if (value->kind == IR_VALUE_HTTP_FETCH) {
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if ((value->kind == IR_VALUE_HTTP_RESPONSE_LEN || value->kind == IR_VALUE_HTTP_RESPONSE_HEADERS_LEN || value->kind == IR_VALUE_HTTP_RESPONSE_BODY_OFFSET) &&
        !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_HTTP_HEADER_VALUE) {
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
    }
  }
  if (value->kind == IR_VALUE_BINARY) {
    bool supported = true;
    if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64) supported = value->binary_op == IR_BIN_ADD || value->binary_op == IR_BIN_SUB || value->binary_op == IR_BIN_MUL;
    if (ctx->backend == Z_DIRECT_BACKEND_MACHO64) supported = value->binary_op == IR_BIN_ADD || value->binary_op == IR_BIN_SUB || value->binary_op == IR_BIN_MUL || value->binary_op == IR_BIN_DIV || value->binary_op == IR_BIN_MOD || value->binary_op == IR_BIN_AND || value->binary_op == IR_BIN_OR;
    if (!supported) return z_build_diag(ctx, diag, "direct backend buildability does not support this binary operator", value->line, value->column, "unsupported operator");
    if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && value->binary_op != IR_BIN_AND && value->binary_op != IR_BIN_OR &&
        macho_scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) {
      return z_build_diag(ctx, diag, "direct AArch64 Mach-O expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    }
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && value->kind == IR_VALUE_COMPARE && macho_scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) {
    return z_build_diag(ctx, diag, "direct AArch64 Mach-O expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  }
  if (value->kind == IR_VALUE_CALL) {
    size_t max_args = ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? 4 : (ctx->backend == Z_DIRECT_BACKEND_ELF64 ? 6 : 8);
    if (ctx->backend != Z_DIRECT_BACKEND_ELF_AARCH64 && value->arg_len > max_args) {
      char actual[80];
      snprintf(actual, sizeof(actual), "%zu argument(s)", value->arg_len);
      return z_build_diag(ctx, diag, "direct backend buildability found a call with too many arguments", value->line, value->column, actual);
    }
    if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && macho_scratch_slot + value->arg_len >= BUILD_MACHO_SCRATCH_SLOT_COUNT) {
      return z_build_diag(ctx, diag, "direct AArch64 Mach-O call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
    }
  }
  if (value->kind == IR_VALUE_LOCAL && fun && value->local_index < fun->local_len && fun->locals[value->local_index].is_array) {
    return z_build_diag(ctx, diag, "direct backend buildability cannot use fixed array locals as scalar values", value->line, value->column, "array local");
  }
  unsigned right_slot = macho_scratch_slot;
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 &&
      ((value->kind == IR_VALUE_BINARY && value->binary_op != IR_BIN_AND && value->binary_op != IR_BIN_OR) ||
       value->kind == IR_VALUE_COMPARE)) {
    right_slot = macho_scratch_slot + 1;
  }
  if (value->index && !build_check_value(ctx, fun, value->index, false, macho_scratch_slot, diag)) return false;
  if (value->left && !skip_left && !build_check_value(ctx, fun, value->left, false, macho_scratch_slot, diag)) return false;
  if (value->right && !build_check_value(ctx, fun, value->right, false, right_slot, diag)) return false;
  for (size_t i = 0; i < value->arg_len; i++) {
    unsigned arg_slot = ctx->backend == Z_DIRECT_BACKEND_MACHO64 && value->kind == IR_VALUE_CALL
                      ? macho_scratch_slot + (unsigned)value->arg_len
                      : macho_scratch_slot;
    if (!build_check_value(ctx, fun, value->args[i], false, arg_slot, diag)) return false;
  }
  return true;
}

static bool build_check_instrs(const ZBuildability *ctx, const IrFunction *fun, const IrInstr *instrs, size_t len, ZDiag *diag);

static bool build_check_instr(const ZBuildability *ctx, const IrFunction *fun, const IrInstr *instr, ZDiag *diag) {
  if (!ctx || !instr) return z_build_diag(ctx, diag, "direct backend buildability found a missing instruction", 1, 1, "missing instruction");
  if (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64) return true;
  switch (instr->kind) {
    case IR_INSTR_LOCAL_SET:
      if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        return !instr->value || z_build_check_coff_byte_view(ctx, fun, instr->value, diag);
      }
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        if (instr->value && !z_build_check_macho_byte_view(ctx, fun, instr->value, diag)) return false;
      }
      if (instr->value && !build_check_value(ctx, fun, instr->value, true, 0, diag)) return false;
      return true;
    case IR_INSTR_INDEX_STORE:
    case IR_INSTR_FIELD_STORE:
      if (instr->value && !build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (instr->index && !build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
      return true;
    case IR_INSTR_WORLD_WRITE:
      if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && instr->value && !z_build_check_coff_byte_view(ctx, fun, instr->value, diag)) return false;
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && instr->value && !z_build_check_macho_byte_view(ctx, fun, instr->value, diag)) return false;
      if (ctx->backend != Z_DIRECT_BACKEND_COFF_X64 && instr->value && !build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (instr->index && !build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
      return true;
    case IR_INSTR_EXPR:
    case IR_INSTR_RETURN:
      if (instr->value && !build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (instr->index && !build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
      return true;
    case IR_INSTR_IF:
    case IR_INSTR_WHILE:
      if (instr->value && !build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (!build_check_instrs(ctx, fun, instr->then_instrs, instr->then_len, diag)) return false;
      return build_check_instrs(ctx, fun, instr->else_instrs, instr->else_len, diag);
    case IR_INSTR_RAISE:
      if (ctx->backend == Z_DIRECT_BACKEND_ELF64) return true;
      return z_build_diag(ctx, diag, "direct backend buildability does not support raise instructions for this emitter", instr->line, instr->column, "IR_INSTR_RAISE");
  }
  return z_build_diag(ctx, diag, "direct backend buildability does not support this instruction", instr->line, instr->column, "unsupported instruction");
}

static bool build_check_instrs(const ZBuildability *ctx, const IrFunction *fun, const IrInstr *instrs, size_t len, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!build_check_instr(ctx, fun, &instrs[i], diag)) return false;
  }
  return true;
}

static bool build_check_function_shape(const ZBuildability *ctx, const IrFunction *fun, ZDiag *diag) {
  if (!fun) return z_build_diag(ctx, diag, "direct backend buildability found a missing function", 1, 1, "missing function");
  if (ctx->backend != Z_DIRECT_BACKEND_ELF_AARCH64 && fun->param_count > 8) {
    return z_build_diag(ctx, diag, "direct backend object buildability supports at most eight parameters", fun->line, fun->column, fun->name);
  }
  if (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64) {
    return z_build_check_aarch64_literal_shape(ctx, fun, diag);
  }
  bool wide_scalars = ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
  bool return_ok = wide_scalars ? (fun->return_type == IR_TYPE_VOID || z_build_is_elf_scalar(fun->return_type))
                                : (fun->return_type == IR_TYPE_VOID || z_build_is_scalar32(fun->return_type));
  if (!return_ok) return z_build_diag(ctx, diag, "direct backend object buildability does not support this return type", fun->line, fun->column, z_build_type_name(fun->return_type));
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->type == IR_TYPE_BYTE_VIEW && local->is_param) return z_build_diag(ctx, diag, "direct backend object buildability does not support byte-view parameters", local->line, local->column, local->name);
    if (local->is_record || local->type == IR_TYPE_BYTE_VIEW || local->type == IR_TYPE_ALLOC || local->type == IR_TYPE_VEC || local->type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (ctx->backend != Z_DIRECT_BACKEND_COFF_X64 && local->type == IR_TYPE_MAYBE_SCALAR) continue;
    if (local->is_array) {
      bool array_ok = local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_U32 ||
                      local->element_type == IR_TYPE_USIZE || (ctx->backend == Z_DIRECT_BACKEND_ELF64 && (local->element_type == IR_TYPE_I64 || local->element_type == IR_TYPE_U64));
      if (!array_ok) return z_build_diag(ctx, diag, "direct backend object buildability does not support this fixed-array local", local->line, local->column, z_build_type_name(local->element_type));
      continue;
    }
    bool local_ok = wide_scalars ? z_build_is_elf_scalar(local->type) : z_build_is_scalar32(local->type);
    if (!local_ok) return z_build_diag(ctx, diag, "direct backend object buildability does not support this local type", local->line, local->column, z_build_type_name(local->type));
  }
  return true;
}

static const IrFunction *build_find_main(const ZBuildability *ctx, const IrProgram *ir, ZDiag *diag) {
  const IrFunction *main_fun = NULL;
  for (size_t i = 0; ir && i < ir->function_len; i++) {
    if (!ir->functions[i].is_exported || !ir->functions[i].name || strcmp(ir->functions[i].name, "main") != 0) continue;
    if (main_fun) {
      z_build_diag(ctx, diag, "direct executable buildability requires exactly one exported main function", ir->functions[i].line, ir->functions[i].column, ir->functions[i].name);
      return NULL;
    }
    main_fun = &ir->functions[i];
  }
  if (!main_fun) z_build_diag(ctx, diag, "direct executable buildability requires an exported main function", 1, 1, "missing main");
  return main_fun;
}

static bool build_check_executable_shape(const ZBuildability *ctx, const IrProgram *ir, ZDiag *diag) {
  const IrFunction *main_fun = build_find_main(ctx, ir, diag);
  if (!main_fun) return false;
  if (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64) return true;
  if (main_fun->param_count != 0) {
    const char *message = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? "direct ELF64 executable main must not take parameters" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? "direct COFF x64 executable main must not take parameters" :
                           "direct AArch64 Mach-O executable main must not take parameters");
    return z_build_diag(ctx, diag, message, main_fun->line, main_fun->column, main_fun->name);
  }
  bool return_ok = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? z_build_is_scalar32(main_fun->return_type)
                                                         : (main_fun->return_type == IR_TYPE_VOID || z_build_is_scalar32(main_fun->return_type));
  if (!return_ok) {
    const char *message = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? "direct ELF64 executable main must return a 32-bit-or-smaller scalar" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? "direct COFF x64 executable main must return Void or a 32-bit-or-smaller scalar" :
                           "direct AArch64 Mach-O executable main must return Void or a 32-bit-or-smaller scalar");
    return z_build_diag(ctx, diag, message, main_fun->line, main_fun->column, z_build_type_name(main_fun->return_type));
  }
  return true;
}

bool z_direct_buildability_check(const IrProgram *ir, const ZTargetInfo *target, const char *emit_kind, ZDiag *diag) {
  ZBuildability ctx;
  if (!z_build_select(ir, target, emit_kind, &ctx, diag)) return false;
  if (!ir->mir_valid) {
    return z_build_diag(&ctx, diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
  }
  if (ir->function_len == 0) return z_build_diag(&ctx, diag, "direct backend buildability requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported) has_export = true;
    if (ctx.backend == Z_DIRECT_BACKEND_ELF_AARCH64 && !ir->functions[i].is_exported) continue;
    if (!build_check_function_shape(&ctx, &ir->functions[i], diag)) return false;
    if (!build_check_instrs(&ctx, &ir->functions[i], ir->functions[i].instrs, ir->functions[i].instr_len, diag)) return false;
  }
  if (!has_export) return z_build_diag(&ctx, diag, "direct backend buildability requires at least one exported function", 1, 1, "no exported function");
  if (strcmp(ctx.emit_kind, "exe") == 0 && !build_check_executable_shape(&ctx, ir, diag)) return false;
  return true;
}
