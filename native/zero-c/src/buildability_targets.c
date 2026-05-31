#include "buildability_internal.h"

#include <stdint.h>

enum {
  BUILD_AARCH64_IMM12_MAX = 4095u
};

static bool build_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
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

static bool build_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!build_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !build_const_u32_value(view->index, &start)) return false;
    if (view->right && !build_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static bool build_array_byte_view_has_storage(const IrFunction *fun, const IrValue *view) {
  if (!view || view->kind != IR_VALUE_ARRAY_BYTE_VIEW || !fun || view->array_index >= fun->local_len) return false;
  const IrLocal *local = &fun->locals[view->array_index];
  return (local->is_array && view->field_offset == 0) || local->is_record;
}

static bool build_check_coff_byte_view_ptr(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!view) return z_build_diag(ctx, diag, "direct COFF byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && fun && view->array_index < fun->local_len) {
    if (!build_array_byte_view_has_storage(fun, view)) return z_build_diag(ctx, diag, "direct COFF byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    return build_check_coff_byte_view_ptr(ctx, fun, view->left, diag);
  }
  return z_build_diag(ctx, diag, "direct COFF value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

bool z_build_check_coff_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  unsigned len = 0;
  if (build_byte_view_const_len(view, &len)) return true;
  if (view && view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_BYTE_SLICE) return true;
  return z_build_diag(ctx, diag, "direct COFF byte-view length currently requires a literal, constant slice, or byte-view local", view ? view->line : 1, view ? view->column : 1, "unsupported byte view length");
}

bool z_build_check_coff_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  return build_check_coff_byte_view_ptr(ctx, fun, view, diag) && z_build_check_coff_byte_view_len(ctx, fun, view, diag);
}

static bool build_check_macho_x64_byte_view_ptr(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!view) return z_build_diag(ctx, diag, "direct x86_64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && fun && view->array_index < fun->local_len) {
    if (!build_array_byte_view_has_storage(fun, view)) return z_build_diag(ctx, diag, "direct x86_64 Mach-O byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    return build_check_macho_x64_byte_view_ptr(ctx, fun, view->left, diag);
  }
  return z_build_diag(ctx, diag, "direct x86_64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

bool z_build_check_macho_x64_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  unsigned len = 0;
  if (build_byte_view_const_len(view, &len)) return true;
  if (view && view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view && view->kind == IR_VALUE_BYTE_SLICE) return true;
  return z_build_diag(ctx, diag, "direct x86_64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view ? view->line : 1, view ? view->column : 1, "unsupported byte view length");
}

bool z_build_check_macho_x64_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  return build_check_macho_x64_byte_view_ptr(ctx, fun, view, diag) && z_build_check_macho_x64_byte_view_len(ctx, fun, view, diag);
}

static bool build_check_macho_byte_view_ptr(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!view) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && fun && view->array_index < fun->local_len) {
    if (!build_array_byte_view_has_storage(fun, view)) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!build_check_macho_byte_view_ptr(ctx, fun, view->left, diag)) return false;
    if (build_const_u32_value(view->index, &start) && build_scaled_index_exceeds_imm12(start, build_view_element_type(view))) {
      return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte slice constant start is too large", view->line, view->column, "unsupported byte slice");
    }
    return true;
  }
  return z_build_diag(ctx, diag, "direct AArch64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

bool z_build_check_macho_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  if (!view) return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > 65535u) {
      return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte-view length is too large for the this backend", view->line, view->column, "large byte view");
    }
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_MAYBE_VALUE && fun && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return true;
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    unsigned end = 0;
    bool const_start = !view->index || build_const_u32_value(view->index, &start);
    bool const_end = build_const_u32_value(view->right, &end);
    if (!view->right) return z_build_check_macho_byte_view_len(ctx, fun, view->left, diag);
    if (const_start && const_end && end >= start && end - start <= 65535u) return true;
    if (const_start && view->right) {
      if (start > BUILD_AARCH64_IMM12_MAX) {
        return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte slice constant start is too large", view->line, view->column, "unsupported byte view length");
      }
      return true;
    }
    if (view->index && view->right) return true;
  }
  return z_build_diag(ctx, diag, "direct AArch64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

bool z_build_check_macho_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag) {
  return build_check_macho_byte_view_ptr(ctx, fun, view, diag) && z_build_check_macho_byte_view_len(ctx, fun, view, diag);
}
