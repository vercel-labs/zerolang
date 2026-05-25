#include "buildability_internal.h"

#include <string.h>

static bool build_value_supported(const ZBuildability *ctx, const IrValue *value, bool local_set_value) {
  if (!ctx || !value) return false;
  if (z_build_backend_is_aarch64_direct(ctx->backend)) {
    switch (value->kind) {
      case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
      case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN:
      case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL: case IR_VALUE_BYTE_VIEW_EQ:
      case IR_VALUE_INDEX_LOAD: case IR_VALUE_MAYBE_HAS: case IR_VALUE_MAYBE_VALUE: case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
        (void)local_set_value;
        return true;
      default:
        (void)local_set_value;
        return false;
    }
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64) {
    switch (value->kind) {
      case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
      case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN:
      case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL: case IR_VALUE_BYTE_VIEW_EQ:
      case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD: case IR_VALUE_CHECK:
      case IR_VALUE_MAYBE_HAS: case IR_VALUE_MAYBE_VALUE: case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
        return true;
      default:
        (void)local_set_value;
        return false;
    }
  }
  switch (value->kind) {
    case IR_VALUE_INT: case IR_VALUE_BOOL: case IR_VALUE_LOCAL: case IR_VALUE_CAST: case IR_VALUE_BINARY: case IR_VALUE_COMPARE: case IR_VALUE_CALL:
    case IR_VALUE_STRING_LITERAL: case IR_VALUE_ARRAY_BYTE_VIEW: case IR_VALUE_BYTE_SLICE: case IR_VALUE_BYTE_VIEW_LEN:
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: case IR_VALUE_INDEX_LOAD: case IR_VALUE_FIELD_LOAD:
      return true;
    case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
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
    case IR_VALUE_CRC32_BYTES:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64;
    case IR_VALUE_BYTE_COPY: case IR_VALUE_BYTE_FILL:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 ||
             ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 || ctx->backend == Z_DIRECT_BACKEND_COFF_X64;
    case IR_VALUE_BYTE_VIEW_EQ:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 ||
             ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 || ctx->backend == Z_DIRECT_BACKEND_COFF_X64;
    case IR_VALUE_CHECK: return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 || ctx->backend == Z_DIRECT_BACKEND_MACHO_X64;
    case IR_VALUE_RESCUE: return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
    case IR_VALUE_MAYBE_VALUE:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 ||
             ((ctx->backend == Z_DIRECT_BACKEND_MACHO64 || ctx->backend == Z_DIRECT_BACKEND_COFF_X64) && value->type == IR_TYPE_BYTE_VIEW);
    case IR_VALUE_JSON_PARSE_BYTES: case IR_VALUE_JSON_VALIDATE_BYTES: case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_HTTP_FETCH: case IR_VALUE_HTTP_RESULT_OK: case IR_VALUE_HTTP_RESULT_STATUS: case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR: case IR_VALUE_HTTP_RESPONSE_LEN: case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: case IR_VALUE_HTTP_HEADER_VALUE: case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET: case IR_VALUE_HTTP_HEADER_LEN:
      return ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64;
  }
  return false;
}

bool z_build_check_value(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, bool local_set_value, unsigned scratch_slot, ZDiag *diag) {
  if (!value) return z_build_diag(ctx, diag, "direct backend buildability found a missing expression", 1, 1, "missing expression");
  if (!build_value_supported(ctx, value, local_set_value)) {
    return z_build_diag(ctx, diag, "direct backend buildability does not support this MIR value", value->line, value->column, z_build_value_kind_name(value->kind));
  }
  bool skip_left = false;
  unsigned right_slot = scratch_slot;
  if (!z_build_check_target_value(ctx, fun, value, scratch_slot, &skip_left, &right_slot, diag)) return false;
  if (value->kind == IR_VALUE_LOCAL && fun && value->local_index < fun->local_len && fun->locals[value->local_index].is_array) {
    return z_build_diag(ctx, diag, "direct backend buildability cannot use fixed array locals as scalar values", value->line, value->column, "array local");
  }
  if (value->index && !z_build_check_value(ctx, fun, value->index, false, scratch_slot, diag)) return false;
  if (value->left && !skip_left && !z_build_check_value(ctx, fun, value->left, false, scratch_slot, diag)) return false;
  if (value->right && !z_build_check_value(ctx, fun, value->right, false, right_slot, diag)) return false;
  unsigned arg_slot = z_build_target_call_arg_slot(ctx, value, scratch_slot);
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!z_build_check_value(ctx, fun, value->args[i], false, arg_slot, diag)) return false;
  }
  return true;
}

static bool build_check_instrs(const ZBuildability *ctx, const IrFunction *fun, const IrInstr *instrs, size_t len, ZDiag *diag);

static bool build_check_instr(const ZBuildability *ctx, const IrFunction *fun, const IrInstr *instr, ZDiag *diag) {
  if (!ctx || !instr) return z_build_diag(ctx, diag, "direct backend buildability found a missing instruction", 1, 1, "missing instruction");
  if (z_build_backend_is_aarch64_direct(ctx->backend) && instr->kind == IR_INSTR_WORLD_WRITE) {
    if (!ctx->executable) {
      return z_build_diag(ctx, diag, "direct AArch64 object buildability does not support World write instructions", instr->line, instr->column, "IR_INSTR_WORLD_WRITE");
    }
    if (instr->value && !z_build_check_aarch64_world_write_byte_view(ctx, fun, instr->value, diag)) return false;
    if (instr->index && !z_build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
    return true;
  }
  if (z_build_backend_is_aarch64_direct(ctx->backend) &&
      (instr->kind == IR_INSTR_FIELD_STORE || instr->kind == IR_INSTR_RAISE)) {
    return z_build_diag(ctx, diag, "direct AArch64 buildability does not support this instruction yet", instr->line, instr->column, "unsupported instruction");
  }
  switch (instr->kind) {
    case IR_INSTR_LOCAL_SET:
      if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        return !instr->value || z_build_check_coff_byte_view(ctx, fun, instr->value, diag);
      }
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        return !instr->value || z_build_check_macho_x64_byte_view(ctx, fun, instr->value, diag);
      }
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        if (instr->value && !z_build_check_macho_byte_view(ctx, fun, instr->value, diag)) return false;
      }
      if (z_build_backend_is_aarch64_direct(ctx->backend) && fun && instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
        if (instr->value && !z_build_check_aarch64_byte_view(ctx, fun, instr->value, diag)) return false;
      }
      if (instr->value && !z_build_check_value(ctx, fun, instr->value, true, 0, diag)) return false;
      return true;
    case IR_INSTR_INDEX_STORE:
    case IR_INSTR_FIELD_STORE: {
      if (instr->value && !z_build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      unsigned index_scratch_slot = instr->kind == IR_INSTR_INDEX_STORE && z_build_backend_is_aarch64_direct(ctx->backend) ? 1 : 0;
      if (instr->index && !z_build_check_value(ctx, fun, instr->index, false, index_scratch_slot, diag)) return false;
      return true;
    }
    case IR_INSTR_WORLD_WRITE:
      if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 && instr->value && !z_build_check_coff_byte_view(ctx, fun, instr->value, diag)) return false;
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 && instr->value && !z_build_check_macho_x64_byte_view(ctx, fun, instr->value, diag)) return false;
      if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && instr->value && !z_build_check_macho_byte_view(ctx, fun, instr->value, diag)) return false;
      if (ctx->backend != Z_DIRECT_BACKEND_COFF_X64 && instr->value && !z_build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (instr->index && !z_build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
      return true;
    case IR_INSTR_EXPR:
    case IR_INSTR_RETURN:
      if (instr->value && !z_build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (instr->index && !z_build_check_value(ctx, fun, instr->index, false, 0, diag)) return false;
      return true;
    case IR_INSTR_IF:
    case IR_INSTR_WHILE:
      if (instr->value && !z_build_check_value(ctx, fun, instr->value, false, 0, diag)) return false;
      if (!build_check_instrs(ctx, fun, instr->then_instrs, instr->then_len, diag)) return false;
      return build_check_instrs(ctx, fun, instr->else_instrs, instr->else_len, diag);
    case IR_INSTR_RAISE:
      if (ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 || ctx->backend == Z_DIRECT_BACKEND_MACHO_X64) return true;
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
  size_t abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) abi_slots += fun->locals[i].type == IR_TYPE_BYTE_VIEW ? 2 : 1;
  size_t max_slots = ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? 8 : (ctx->backend == Z_DIRECT_BACKEND_MACHO64 ? 8 : 6);
  if (!z_build_backend_is_aarch64_direct(ctx->backend) && abi_slots > max_slots) {
    return z_build_diag(ctx, diag, "direct backend object buildability has too many ABI argument slots", fun->line, fun->column, fun->name);
  }
  if (z_build_backend_is_aarch64_direct(ctx->backend)) return z_build_check_aarch64_function_shape(ctx, fun, diag);
  bool wide_scalars = ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 || ctx->backend == Z_DIRECT_BACKEND_MACHO_X64;
  bool return_ok = wide_scalars ? (fun->return_type == IR_TYPE_VOID || z_build_is_elf_scalar(fun->return_type))
                                : (fun->return_type == IR_TYPE_VOID || z_build_is_scalar32(fun->return_type));
  if (fun->return_type == IR_TYPE_BYTE_VIEW || fun->return_type == IR_TYPE_MAYBE_BYTE_VIEW) return_ok = true;
  if (!return_ok) return z_build_diag(ctx, diag, "direct backend object buildability does not support this return type", fun->line, fun->column, z_build_type_name(fun->return_type));
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->type == IR_TYPE_BYTE_VIEW && local->is_param && ctx->backend != Z_DIRECT_BACKEND_ELF64 && ctx->backend != Z_DIRECT_BACKEND_MACHO64 && ctx->backend != Z_DIRECT_BACKEND_MACHO_X64 && ctx->backend != Z_DIRECT_BACKEND_COFF_X64) return z_build_diag(ctx, diag, "direct backend object buildability does not support byte-view parameters", local->line, local->column, local->name);
    if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 && (local->type == IR_TYPE_ALLOC || local->type == IR_TYPE_VEC || local->type == IR_TYPE_MAYBE_SCALAR)) {
      return z_build_diag(ctx, diag, "direct x86_64 Mach-O object buildability does not support this local type", local->line, local->column, z_build_type_name(local->type));
    }
    if (local->is_record || local->type == IR_TYPE_BYTE_VIEW || local->type == IR_TYPE_ALLOC || local->type == IR_TYPE_VEC || local->type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (ctx->backend != Z_DIRECT_BACKEND_COFF_X64 && local->type == IR_TYPE_MAYBE_SCALAR) continue;
    if (local->is_array) {
      bool array_ok = local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_U32 ||
                      local->element_type == IR_TYPE_USIZE || ((ctx->backend == Z_DIRECT_BACKEND_ELF64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64) && local->element_type == IR_TYPE_BOOL) ||
                      (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 && local->element_type == IR_TYPE_BOOL) ||
                      (ctx->backend == Z_DIRECT_BACKEND_ELF64 && (local->element_type == IR_TYPE_I64 || local->element_type == IR_TYPE_U64));
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
  if (main_fun->param_count != 0) {
    const char *message = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? "direct ELF64 executable main must not take parameters" :
                          (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64 ? "direct AArch64 ELF executable main must not take parameters" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_AARCH64 ? "direct COFF AArch64 executable main must not take parameters" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? "direct COFF x64 executable main must not take parameters" :
                           (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 ? "direct x86_64 Mach-O executable main must not take parameters" :
                            "direct AArch64 Mach-O executable main must not take parameters"))));
    return z_build_diag(ctx, diag, message, main_fun->line, main_fun->column, main_fun->name);
  }
  bool return_ok = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? z_build_is_scalar32(main_fun->return_type)
                                                         : (main_fun->return_type == IR_TYPE_VOID || z_build_is_scalar32(main_fun->return_type));
  if (!return_ok) {
    const char *message = ctx->backend == Z_DIRECT_BACKEND_ELF64 ? "direct ELF64 executable main must return a 32-bit-or-smaller scalar" :
                          (ctx->backend == Z_DIRECT_BACKEND_ELF_AARCH64 ? "direct AArch64 ELF executable main must return Void or a 32-bit-or-smaller scalar" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_AARCH64 ? "direct COFF AArch64 executable main must return Void or a 32-bit-or-smaller scalar" :
                          (ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? "direct COFF x64 executable main must return Void or a 32-bit-or-smaller scalar" :
                           (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 ? "direct x86_64 Mach-O executable main must return Void or a 32-bit-or-smaller scalar" :
                            "direct AArch64 Mach-O executable main must return Void or a 32-bit-or-smaller scalar"))));
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
    if (!build_check_function_shape(&ctx, &ir->functions[i], diag)) return false;
    if (!build_check_instrs(&ctx, &ir->functions[i], ir->functions[i].instrs, ir->functions[i].instr_len, diag)) return false;
  }
  if (!has_export) return z_build_diag(&ctx, diag, "direct backend buildability requires at least one exported function", 1, 1, "no exported function");
  if (ctx.executable && !build_check_executable_shape(&ctx, ir, diag)) return false;
  return true;
}
