#include "buildability_internal.h"
#include <stdint.h>
#include <stdio.h>
enum { BUILD_AARCH64_IMM12_MAX = 4095u };

bool z_build_backend_is_aarch64_direct(ZDirectBackend backend) { return backend == Z_DIRECT_BACKEND_ELF_AARCH64 || backend == Z_DIRECT_BACKEND_COFF_AARCH64; }

static bool build_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static bool build_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind != IR_VALUE_BYTE_SLICE) return false;
  unsigned base_len = 0, start = 0, end = 0;
  if (!build_byte_view_const_len(view->left, &base_len)) return false;
  end = base_len;
  if ((view->index && !build_const_u32_value(view->index, &start)) || (view->right && !build_const_u32_value(view->right, &end))) return false;
  if (start > end || end > base_len) return false;
  if (out) *out = end - start;
  return true;
}

static IrTypeKind build_view_element_type(const IrValue *view) {
  return view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
}

static unsigned build_type_index_shift(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 0;
  if (type == IR_TYPE_U16) return 1;
  if (type == IR_TYPE_I64 || type == IR_TYPE_U64) return 3;
  return 2;
}

static bool build_scaled_index_exceeds_imm12(unsigned start, IrTypeKind element_type) {
  return start > ((unsigned)BUILD_AARCH64_IMM12_MAX >> build_type_index_shift(element_type));
}

static bool build_aarch64_index_load_uses_scratch(const IrFunction *fun, const IrValue *value) {
  if (!fun || value->kind != IR_VALUE_INDEX_LOAD || value->array_index >= fun->local_len) return true;
  const IrLocal *local = &fun->locals[value->array_index];
  unsigned const_index = 0;
  return !(local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE) &&
           build_const_u32_value(value->index, &const_index) && const_index < local->array_len);
}

static bool build_check_aarch64_byte_view_ptr_spill(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, unsigned scratch_slot, unsigned slot_count, const char *message, ZDiag *diag);

static bool build_check_aarch64_byte_view_len_spill(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, unsigned scratch_slot, unsigned slot_count, const char *message, ZDiag *diag) {
  if (!view || view->kind != IR_VALUE_BYTE_SLICE) return true;
  unsigned len = 0;
  if (build_byte_view_const_len(view, &len) && len <= 65535u) return true;
  if (!view->index && !view->right) {
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, view->left, scratch_slot, slot_count, message, diag)) return false;
    return build_check_aarch64_byte_view_len_spill(ctx, fun, view->left, scratch_slot, slot_count, message, diag);
  }
  unsigned required_slot = view->right ? 3u : 2u;
  if (scratch_slot + required_slot >= slot_count) return z_build_diag(ctx, diag, message, view->line, view->column, "expression too deep");
  if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, view->left, scratch_slot, slot_count, message, diag)) return false;
  if (!build_check_aarch64_byte_view_len_spill(ctx, fun, view->left, scratch_slot, slot_count, message, diag)) return false;
  return (!view->index || z_build_check_value(ctx, fun, view->index, false, scratch_slot + 2, diag)) &&
         (!view->right || z_build_check_value(ctx, fun, view->right, false, scratch_slot + 3, diag));
}

static bool build_check_aarch64_byte_view_ptr_spill(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, unsigned scratch_slot, unsigned slot_count, const char *message, ZDiag *diag) {
  if (!view || view->kind != IR_VALUE_BYTE_SLICE) return true;
  if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, view->left, scratch_slot, slot_count, message, diag)) return false;
  unsigned start = 0;
  if (view->index && !build_const_u32_value(view->index, &start)) {
    if (scratch_slot >= slot_count) return z_build_diag(ctx, diag, message, view->line, view->column, "expression too deep");
    if (!z_build_check_value(ctx, fun, view->index, false, scratch_slot + 1, diag)) return false;
  }
  return true;
}

static size_t build_value_abi_slots(const IrValue *value) {
  size_t slots = 0;
  for (size_t i = 0; value && i < value->arg_len; i++) {
    slots += value->args[i] && value->args[i]->type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
  }
  return slots;
}

static bool build_array_byte_view_has_storage(const IrFunction *fun, const IrValue *view) {
  if (!view || view->kind != IR_VALUE_ARRAY_BYTE_VIEW || !fun || view->array_index >= fun->local_len) return false;
  const IrLocal *local = &fun->locals[view->array_index];
  return (local->is_array && view->field_offset == 0) || local->is_record;
}

static bool build_aarch64_byte_view_ptr(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!view) return z_build_diag(ctx, diag, "direct AArch64 byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && fun && view->array_index < fun->local_len) {
    if (!build_array_byte_view_has_storage(fun, view)) return z_build_diag(ctx, diag, "direct AArch64 byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!build_aarch64_byte_view_ptr(ctx, fun, view->left, diag)) return false;
    if (build_const_u32_value(view->index, &start) && build_scaled_index_exceeds_imm12(start, build_view_element_type(view))) {
      return z_build_diag(ctx, diag, "direct AArch64 byte slice constant start is too large", view->line, view->column, "unsupported byte slice");
    }
    return true;
  }
  return z_build_diag(ctx, diag, "direct AArch64 value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

bool z_build_check_aarch64_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  unsigned len = 0;
  if (build_byte_view_const_len(view, &len)) return len <= 65535u || z_build_diag(ctx, diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
  if (!view) return z_build_diag(ctx, diag, "direct AArch64 byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > 65535u) return z_build_diag(ctx, diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    return z_build_check_aarch64_byte_view_len(ctx, fun, view->left, diag);
  }
  return z_build_diag(ctx, diag, "direct AArch64 byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

bool z_build_check_aarch64_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) { return build_aarch64_byte_view_ptr(ctx, fun, view, diag) && z_build_check_aarch64_byte_view_len(ctx, fun, view, diag); }

bool z_build_check_aarch64_world_write_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!z_build_check_aarch64_byte_view(ctx, fun, view, diag)) return false;
  if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, view, 0, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 World write exceeds scratch register spill capacity", diag)) return false;
  return build_check_aarch64_byte_view_len_spill(ctx, fun, view, 0, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 World write exceeds scratch register spill capacity", diag);
}

static bool build_aarch64_byte_operation(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, unsigned scratch_slot, bool *skip_left, ZDiag *diag) {
  if (value->kind == IR_VALUE_BYTE_COPY) {
    if (scratch_slot + 3 >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) {
      return z_build_diag(ctx, diag, "direct AArch64 byte copy exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    }
    if (!z_build_check_aarch64_byte_view(ctx, fun, value->left, diag)) return false;
    if (!z_build_check_aarch64_byte_view(ctx, fun, value->right, diag)) return false;
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->left, scratch_slot, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte copy exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte copy exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte copy exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 3, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte copy exceeds scratch register spill capacity", diag)) return false;
  }
  if (value->kind == IR_VALUE_BYTE_FILL) {
    if (scratch_slot + 2 >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) {
      return z_build_diag(ctx, diag, "direct AArch64 byte fill exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    }
    if (!z_build_check_aarch64_byte_view(ctx, fun, value->right, diag)) return false;
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 1, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte fill exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte fill exceeds scratch register spill capacity", diag)) return false;
  }
  if (value->kind == IR_VALUE_BYTE_VIEW_EQ) {
    if (scratch_slot + 3 >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) {
      return z_build_diag(ctx, diag, "direct AArch64 byte-view equality exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    }
    if (!z_build_check_aarch64_byte_view(ctx, fun, value->left, diag)) return false;
    if (!z_build_check_aarch64_byte_view(ctx, fun, value->right, diag)) return false;
    if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view equality exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 1, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view equality exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view equality exceeds scratch register spill capacity", diag)) return false;
    if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view equality exceeds scratch register spill capacity", diag)) return false;
  }
  if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view length exceeds scratch register spill capacity", diag)) return false;
  if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_aarch64_byte_view_len(ctx, fun, value->left, diag)) return false;
  if (value->kind == IR_VALUE_INDEX_LOAD && build_aarch64_index_load_uses_scratch(fun, value) && scratch_slot >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 indexed load exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && scratch_slot >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 byte-view indexed load exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_AARCH64_SCRATCH_SLOT_COUNT, "direct AArch64 byte-view indexed load exceeds scratch register spill capacity", diag)) return false;
  if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_aarch64_byte_view(ctx, fun, value->left, diag)) return false;
  if (value->kind == IR_VALUE_BYTE_VIEW_LEN || value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD) *skip_left = true;
  return true;
}

static bool build_check_binary_operator(const ZBuildability *ctx, const IrValue *value, unsigned scratch_slot, unsigned *right_slot, ZDiag *diag) {
  if (value->kind != IR_VALUE_BINARY) return true;
  bool supported = true;
  if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64) {
    supported = value->binary_op == IR_BIN_ADD || value->binary_op == IR_BIN_SUB || value->binary_op == IR_BIN_MUL ||
                value->binary_op == IR_BIN_DIV || value->binary_op == IR_BIN_MOD ||
                value->binary_op == IR_BIN_AND || value->binary_op == IR_BIN_OR;
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64 || ctx->backend == Z_DIRECT_BACKEND_MACHO64 || z_build_backend_is_aarch64_direct(ctx->backend)) {
    supported = value->binary_op == IR_BIN_ADD || value->binary_op == IR_BIN_SUB || value->binary_op == IR_BIN_MUL ||
                value->binary_op == IR_BIN_DIV || value->binary_op == IR_BIN_MOD ||
                value->binary_op == IR_BIN_AND || value->binary_op == IR_BIN_OR;
  }
  if (!supported) return z_build_diag(ctx, diag, "direct backend buildability does not support this binary operator", value->line, value->column, "unsupported operator");
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && value->binary_op != IR_BIN_AND && value->binary_op != IR_BIN_OR && scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) {
    return z_build_diag(ctx, diag, "direct AArch64 Mach-O expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  }
  if (z_build_backend_is_aarch64_direct(ctx->backend) && value->binary_op != IR_BIN_AND && value->binary_op != IR_BIN_OR && scratch_slot >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) {
    return z_build_diag(ctx, diag, "direct AArch64 expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  }
  if ((ctx->backend == Z_DIRECT_BACKEND_MACHO64 || z_build_backend_is_aarch64_direct(ctx->backend)) &&
      value->binary_op != IR_BIN_AND && value->binary_op != IR_BIN_OR) {
    *right_slot = scratch_slot + 1;
  }
  return true;
}

static bool build_check_compare(const ZBuildability *ctx, const IrValue *value, unsigned scratch_slot, unsigned *right_slot, ZDiag *diag) {
  if (value->kind != IR_VALUE_COMPARE) return true;
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) {
    return z_build_diag(ctx, diag, "direct AArch64 Mach-O expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  }
  if (z_build_backend_is_aarch64_direct(ctx->backend) && scratch_slot >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) {
    return z_build_diag(ctx, diag, "direct AArch64 expression nesting exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 || z_build_backend_is_aarch64_direct(ctx->backend)) *right_slot = scratch_slot + 1;
  return true;
}

static bool build_check_call_shape(const ZBuildability *ctx, const IrValue *value, unsigned scratch_slot, ZDiag *diag) {
  if (value->kind != IR_VALUE_CALL) return true;
  size_t max_args = ctx->backend == Z_DIRECT_BACKEND_COFF_X64 ? 8 : (ctx->backend == Z_DIRECT_BACKEND_MACHO64 ? 8 : 6);
  size_t abi_slots = build_value_abi_slots(value);
  if (!z_build_backend_is_aarch64_direct(ctx->backend) && abi_slots > max_args) {
    char actual[80];
    snprintf(actual, sizeof(actual), "%zu ABI argument slot(s)", abi_slots);
    return z_build_diag(ctx, diag, "direct backend buildability found a call with too many arguments", value->line, value->column, actual);
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64 && scratch_slot + abi_slots >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
  if (z_build_backend_is_aarch64_direct(ctx->backend) && scratch_slot + abi_slots >= BUILD_AARCH64_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
  return true;
}

bool z_build_check_target_value(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, unsigned scratch_slot, bool *skip_left, unsigned *right_slot, ZDiag *diag) {
  if (skip_left) *skip_left = false;
  if (right_slot) *right_slot = scratch_slot;
  if (!ctx || !value) return true;
  if (ctx->backend == Z_DIRECT_BACKEND_COFF_X64) {
    if (value->kind == IR_VALUE_BYTE_COPY) {
      if (!z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_coff_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_FILL && !z_build_check_coff_byte_view(ctx, fun, value->right, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_EQ) {
      if (!z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_coff_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_coff_byte_view_len(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT) && !z_build_check_coff_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN || value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD ||
        value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT) *skip_left = true;
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO_X64) {
    if (value->kind == IR_VALUE_BYTE_COPY) {
      if (!z_build_check_macho_x64_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_x64_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_FILL && !z_build_check_macho_x64_byte_view(ctx, fun, value->right, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_EQ) {
      if (!z_build_check_macho_x64_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_x64_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_macho_x64_byte_view_len(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_macho_x64_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN || value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD) *skip_left = true;
  }
  if (ctx->backend == Z_DIRECT_BACKEND_MACHO64) {
    if (value->kind == IR_VALUE_BYTE_COPY) {
      if (scratch_slot + 3 >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte copy exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
      if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->left, scratch_slot, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte copy exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte copy exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte copy exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 3, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte copy exceeds scratch register spill capacity", diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_FILL) {
      if (scratch_slot + 2 >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte fill exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
      if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 1, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte fill exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte fill exceeds scratch register spill capacity", diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_VIEW_EQ) {
      if (scratch_slot + 3 >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte-view equality exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
      if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view equality exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_len_spill(ctx, fun, value->right, scratch_slot + 1, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view equality exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view equality exceeds scratch register spill capacity", diag)) return false;
      if (!build_check_aarch64_byte_view_ptr_spill(ctx, fun, value->right, scratch_slot + 2, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view equality exceeds scratch register spill capacity", diag)) return false;
    }
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view length exceeds scratch register spill capacity", diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_LEN && !z_build_check_macho_byte_view_len(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_INDEX_LOAD && build_aarch64_index_load_uses_scratch(fun, value) && scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O indexed load exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && scratch_slot >= BUILD_MACHO_SCRATCH_SLOT_COUNT) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte-view indexed load exceeds scratch register spill capacity", value->line, value->column, "expression too deep");
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !build_check_aarch64_byte_view_len_spill(ctx, fun, value->left, scratch_slot + 1, BUILD_MACHO_SCRATCH_SLOT_COUNT, "direct AArch64 Mach-O byte-view indexed load exceeds scratch register spill capacity", diag)) return false;
    if (value->kind == IR_VALUE_BYTE_VIEW_INDEX_LOAD && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_FIXED_BUF_ALLOC || value->kind == IR_VALUE_VEC_INIT) && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if ((value->kind == IR_VALUE_JSON_PARSE_BYTES || value->kind == IR_VALUE_JSON_VALIDATE_BYTES || value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES) && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_HTTP_FETCH) {
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
    }
    if ((value->kind == IR_VALUE_HTTP_RESPONSE_LEN || value->kind == IR_VALUE_HTTP_RESPONSE_HEADERS_LEN || value->kind == IR_VALUE_HTTP_RESPONSE_BODY_OFFSET) && !z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
    if (value->kind == IR_VALUE_HTTP_HEADER_VALUE) {
      if (!z_build_check_macho_byte_view(ctx, fun, value->left, diag)) return false;
      if (!z_build_check_macho_byte_view(ctx, fun, value->right, diag)) return false;
    }
  }
  if (z_build_backend_is_aarch64_direct(ctx->backend) && !build_aarch64_byte_operation(ctx, fun, value, scratch_slot, skip_left, diag)) return false;
  return build_check_binary_operator(ctx, value, scratch_slot, right_slot, diag) &&
         build_check_compare(ctx, value, scratch_slot, right_slot, diag) &&
         build_check_call_shape(ctx, value, scratch_slot, diag);
}

unsigned z_build_target_call_arg_slot(const ZBuildability *ctx, const IrValue *value, unsigned scratch_slot) {
  if (ctx && value && (ctx->backend == Z_DIRECT_BACKEND_MACHO64 || z_build_backend_is_aarch64_direct(ctx->backend)) && value->kind == IR_VALUE_CALL) {
    return scratch_slot + (unsigned)build_value_abi_slots(value);
  }
  return scratch_slot;
}

bool z_build_check_aarch64_function_shape(const ZBuildability *ctx, const IrFunction *fun, ZDiag *diag) {
  size_t abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) abi_slots += fun->locals[i].type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
  if (abi_slots > 8) return z_build_diag(ctx, diag, "direct AArch64 buildability found too many ABI argument slots", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !z_build_is_scalar32(fun->return_type) &&
      fun->return_type != IR_TYPE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_BYTE_VIEW) {
    return z_build_diag(ctx, diag, "direct AArch64 buildability currently supports Void, 32-bit scalars, byte views, and Maybe byte views", fun->line, fun->column, z_build_type_name(fun->return_type));
  }
  size_t frame_bytes = (fun->frame_bytes ? fun->frame_bytes : fun->local_len * 8u) + BUILD_AARCH64_SCRATCH_SLOT_COUNT * 8u;
  if (frame_bytes > 4095u) return z_build_diag(ctx, diag, "direct AArch64 stack frame exceeds current immediate-offset support", fun->line, fun->column, fun->name);
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->type == IR_TYPE_BYTE_VIEW) continue;
    if (local->is_array) {
      bool array_ok = local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_BOOL || local->element_type == IR_TYPE_U16 ||
                      local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE ||
                      local->element_type == IR_TYPE_I64 || local->element_type == IR_TYPE_U64;
      if (!array_ok) return z_build_diag(ctx, diag, "direct AArch64 object buildability does not support this fixed-array local", local->line, local->column, z_build_type_name(local->element_type));
      continue;
    }
    if (local->type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (!z_build_is_elf_scalar(local->type)) {
      return z_build_diag(ctx, diag, "direct AArch64 object buildability does not support this local type", local->line, local->column, z_build_type_name(local->type));
    }
  }
  return true;
}
