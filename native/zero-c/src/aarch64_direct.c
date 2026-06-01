#include "zero.h"
#include "aarch64_direct.h"
#include "aarch64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  A64_DIRECT_SCRATCH_SLOT_COUNT = 32u,
  A64_DIRECT_SCRATCH_SLOT_BYTES = 8u
};

static bool a64_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct AArch64 backend subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to AArch64 supported direct-backend constructs");
  }
  return false;
}

static bool a64_return_literal(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun) return a64_diag(diag, "direct AArch64 backend requires a function", 1, 1, "missing function");
  if (fun->param_count != 0) {
    return a64_diag(diag, "direct AArch64 backend supports exported functions without parameters", fun->line, fun->column, fun->name);
  }
  if (fun->return_type != IR_TYPE_VOID && fun->return_type != IR_TYPE_U8 && fun->return_type != IR_TYPE_I32 && fun->return_type != IR_TYPE_U32 && fun->return_type != IR_TYPE_USIZE) {
    return a64_diag(diag, "direct AArch64 backend supports primitive 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  *out = 0;
  if (fun->return_type == IR_TYPE_VOID) {
    if (fun->instr_len == 0) return true;
    if (fun->instr_len == 1 && fun->instrs[0].kind == IR_INSTR_RETURN && !fun->instrs[0].value) return true;
    return false;
  }
  if (fun->instr_len == 1 && fun->instrs[0].kind == IR_INSTR_RETURN && fun->instrs[0].value &&
      fun->instrs[0].value->kind == IR_VALUE_INT && fun->instrs[0].value->int_value <= 65535) {
    *out = (uint32_t)fun->instrs[0].value->int_value;
    return true;
  }
  return false;
}

static bool a64_type_is_scalar32(IrTypeKind type) { return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32; }

static bool a64_type_is_scalar64(IrTypeKind type) {
  return type == IR_TYPE_I64 || type == IR_TYPE_USIZE || type == IR_TYPE_U64;
}

static bool a64_type_is_scalar(IrTypeKind type) {
  return a64_type_is_scalar32(type) || a64_type_is_scalar64(type);
}

static bool a64_type_is_unsigned(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_U32 || type == IR_TYPE_U64;
}

static IrTypeKind a64_view_element_type(const IrValue *view) {
  return view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
}

static unsigned a64_type_index_shift(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 0;
  if (type == IR_TYPE_U16) return 1;
  if (a64_type_is_scalar64(type)) return 3;
  return 2;
}

static void a64_emit_add_scaled_index(ZBuf *text, unsigned dst, unsigned base, unsigned index, IrTypeKind element_type) {
  unsigned shift = a64_type_index_shift(element_type);
  if (shift == 0) z_aarch64_emit_add_x_reg(text, dst, base, index);
  else z_aarch64_emit_add_x_reg_lsl(text, dst, base, index, shift);
}

static void a64_emit_load_ptr_element(ZBuf *text, unsigned dst, unsigned base, IrTypeKind element_type) {
  if (element_type == IR_TYPE_U8 || element_type == IR_TYPE_BOOL) z_aarch64_emit_load_b_imm(text, dst, base, 0);
  else if (element_type == IR_TYPE_U16) z_aarch64_emit_load_h_imm(text, dst, base, 0);
  else if (a64_type_is_scalar64(element_type)) z_aarch64_emit_load_x_imm(text, dst, base, 0);
  else z_aarch64_emit_load_w_imm(text, dst, base, 0);
}

static void a64_emit_store_ptr_element(ZBuf *text, unsigned src, unsigned base, IrTypeKind element_type) {
  if (element_type == IR_TYPE_U8 || element_type == IR_TYPE_BOOL) z_aarch64_emit_store_b_imm(text, src, base, 0);
  else if (element_type == IR_TYPE_U16) z_aarch64_emit_store_h_imm(text, src, base, 0);
  else if (a64_type_is_scalar64(element_type)) z_aarch64_emit_store_x_imm(text, src, base, 0);
  else z_aarch64_emit_store_w_imm(text, src, base, 0);
}

static void a64_emit_scale_len_for_element(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  unsigned shift = a64_type_index_shift(element_type);
  if (shift == 0) return;
  z_aarch64_emit_movz_w(text, 14, 1u << shift);
  z_aarch64_emit_mul_w_reg(text, reg, reg, 14);
}

static bool a64_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static bool a64_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!a64_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !a64_const_u32_value(view->index, &start)) return false;
    if (view->right && !a64_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static unsigned a64_cond_for_compare(IrCompareOp op, bool uns) {
  switch (op) {
    case IR_CMP_EQ: return 0;
    case IR_CMP_NE: return 1;
    case IR_CMP_LT: return uns ? 3 : 11;
    case IR_CMP_LE: return uns ? 9 : 13;
    case IR_CMP_GT: return uns ? 8 : 12;
    case IR_CMP_GE: return uns ? 2 : 10;
  }
  return 0;
}

static unsigned a64_invert_cond(unsigned cond) {
  return cond ^ 1u;
}

static unsigned a64_slot_offset(unsigned local_index) {
  return local_index * 8u;
}

static unsigned a64_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0 && frame_size >= fun->locals[local_index].frame_offset) {
    return frame_size - fun->locals[local_index].frame_offset + slot_offset;
  }
  return A64_DIRECT_SCRATCH_SLOT_COUNT * A64_DIRECT_SCRATCH_SLOT_BYTES + a64_slot_offset(local_index) + slot_offset;
}

static void a64_emit_load_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_load_w_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_load_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_load_x_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_w_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_x_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static bool a64_scratch_slot(unsigned slot, unsigned *offset, const IrValue *value, ZDiag *diag) {
  if (slot >= A64_DIRECT_SCRATCH_SLOT_COUNT) {
    return a64_diag(diag, "direct AArch64 expression nesting exceeds scratch register spill capacity", value ? value->line : 1, value ? value->column : 1, "expression too deep");
  }
  *offset = slot * A64_DIRECT_SCRATCH_SLOT_BYTES;
  return true;
}

static bool a64_emit_store_scratch(ZBuf *text, unsigned reg, IrTypeKind type, unsigned slot, const IrValue *value, ZDiag *diag) {
  unsigned offset = 0;
  if (!a64_scratch_slot(slot, &offset, value, diag)) return false;
  if (a64_type_is_scalar64(type)) z_aarch64_emit_store_x_sp(text, reg, offset);
  else z_aarch64_emit_store_w_sp(text, reg, offset);
  return true;
}

static bool a64_emit_load_scratch(ZBuf *text, unsigned reg, IrTypeKind type, unsigned slot, const IrValue *value, ZDiag *diag) {
  unsigned offset = 0;
  if (!a64_scratch_slot(slot, &offset, value, diag)) return false;
  if (a64_type_is_scalar64(type)) z_aarch64_emit_load_x_sp(text, reg, offset);
  else z_aarch64_emit_load_w_sp(text, reg, offset);
  return true;
}

static void a64_emit_u32_bounds_check(ZBuf *text, unsigned index_reg, unsigned len_reg) {
  z_aarch64_emit_cmp_w(text, index_reg, len_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
}

static void a64_emit_u32_upper_bound_check(ZBuf *text, unsigned value_reg, unsigned max_reg) {
  z_aarch64_emit_cmp_w(text, value_reg, max_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 9);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
}

static bool a64_scaled_index_imm12(unsigned start, IrTypeKind element_type, unsigned *out) {
  unsigned shift = a64_type_index_shift(element_type);
  if (start > (4095u >> shift)) return false;
  if (out) *out = start << shift;
  return true;
}

static void a64_emit_cast_normalize_reg(ZBuf *text, unsigned reg, IrTypeKind source, IrTypeKind target) {
  switch (target) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8:
      z_aarch64_emit_uxtb_w(text, reg, reg);
      return;
    case IR_TYPE_U16:
      z_aarch64_emit_uxth_w(text, reg, reg);
      return;
    case IR_TYPE_I32:
    case IR_TYPE_U32:
      z_aarch64_emit_mov_w(text, reg, reg);
      return;
    case IR_TYPE_I64:
    case IR_TYPE_USIZE:
    case IR_TYPE_U64:
      if (source == IR_TYPE_I32) z_aarch64_emit_sxtw_x(text, reg, reg);
      else if (!a64_type_is_scalar64(source)) z_aarch64_emit_mov_w(text, reg, reg);
      return;
    default:
      return;
  }
}

static void a64_emit_binary_reg(ZBuf *text, IrBinaryOp op, unsigned dst, unsigned lhs, unsigned rhs, bool wide) {
  if (op == IR_BIN_ADD) {
    if (wide) z_aarch64_emit_add_x_reg(text, dst, lhs, rhs);
    else z_aarch64_emit_add_w_reg(text, dst, lhs, rhs);
  } else if (op == IR_BIN_SUB) {
    if (wide) z_aarch64_emit_sub_x_reg(text, dst, lhs, rhs);
    else z_aarch64_emit_sub_w_reg(text, dst, lhs, rhs);
  } else if (op == IR_BIN_MUL) {
    if (wide) z_aarch64_emit_mul_x_reg(text, dst, lhs, rhs);
    else z_aarch64_emit_mul_w_reg(text, dst, lhs, rhs);
  }
}

static bool a64_record_data_patch(ZAArch64DirectContext *ctx, size_t patch_offset, unsigned data_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx || !ctx->record_data_patch) return a64_diag(diag, "direct AArch64 readonly data patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return ctx->record_data_patch(ctx->patch_user, patch_offset, data_offset, value, diag);
}

static bool a64_record_call_patch(ZAArch64DirectContext *ctx, size_t patch_offset, unsigned callee_index, ZDiag *diag, const IrValue *value) {
  if (!ctx || !ctx->record_call_patch) return a64_diag(diag, "direct AArch64 call patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return ctx->record_call_patch(ctx->patch_user, patch_offset, callee_index, value, diag);
}

static unsigned a64_abi_slots_for_param(const IrLocal *param) {
  return param && param->type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
}

static bool a64_emit_rodata_ptr_literal(ZBuf *text, unsigned reg, unsigned data_offset, ZAArch64DirectContext *ctx, const IrValue *value, ZDiag *diag) {
  while (((text->len + 8) % 8) != 0) z_aarch64_emit_nop(text);
  z_aarch64_emit_ldr_x_literal8(text, reg);
  z_aarch64_emit_b_offset_words(text, 3);
  size_t patch_offset = text->len;
  z_aarch64_append_u64(text, 0);
  return a64_record_data_patch(ctx, patch_offset, data_offset, diag, value);
}

static bool a64_emit_byte_view_ptr_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag);
static bool a64_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag);
static bool a64_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag);
static bool a64_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag);

static void a64_emit_move_byte_view_pair(ZBuf *text, unsigned ptr_reg, unsigned len_reg, unsigned src_ptr_reg, unsigned src_len_reg) {
  if (ptr_reg == src_len_reg && len_reg == src_ptr_reg) {
    z_aarch64_emit_mov_x(text, 16, src_ptr_reg);
    z_aarch64_emit_mov_w(text, len_reg, src_len_reg);
    z_aarch64_emit_mov_x(text, ptr_reg, 16);
    return;
  }
  if (ptr_reg == src_len_reg) {
    if (len_reg != src_len_reg) z_aarch64_emit_mov_w(text, len_reg, src_len_reg);
    if (ptr_reg != src_ptr_reg) z_aarch64_emit_mov_x(text, ptr_reg, src_ptr_reg);
    return;
  }
  if (ptr_reg != src_ptr_reg) z_aarch64_emit_mov_x(text, ptr_reg, src_ptr_reg);
  if (len_reg != src_len_reg) z_aarch64_emit_mov_w(text, len_reg, src_len_reg);
}

static bool a64_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!view) return a64_diag(diag, "direct AArch64 byte view is missing", 1, 1, "missing byte view");
  unsigned const_len = 0;
  if (view->kind == IR_VALUE_BYTE_SLICE && a64_byte_view_const_len(view, &const_len)) {
    if (const_len > 65535) return a64_diag(diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, const_len);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > 65535) return a64_diag(diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    a64_emit_load_local_w(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    a64_emit_load_local_w(text, fun, reg, view->local_index, 16, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 1) z_aarch64_emit_mov_w(text, reg, 1);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned ptr_tmp = reg == 11 ? 12 : 11;
    return a64_emit_byte_view_pair_at(text, fun, view, ptr_tmp, reg, frame_size, scratch_slot, ctx, diag);
  }
  return a64_diag(diag, "direct AArch64 byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

static bool a64_emit_byte_view_ptr_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!view) return a64_diag(diag, "direct AArch64 byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    a64_emit_load_local_x(text, fun, reg, view->local_index, 0, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    a64_emit_load_local_x(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!((local->is_array && view->field_offset == 0) || local->is_record)) return a64_diag(diag, "direct AArch64 byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    z_aarch64_emit_add_x_sp_imm(text, reg, a64_local_slot_offset(fun, view->array_index, view->field_offset, frame_size));
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return a64_emit_rodata_ptr_literal(text, reg, view->data_offset, ctx, view, diag);
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!a64_emit_byte_view_ptr_at(text, fun, view->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    if (!view->index) return true;
    if (a64_const_u32_value(view->index, &start)) {
      unsigned byte_start = 0;
      if (!a64_scaled_index_imm12(start, a64_view_element_type(view), &byte_start)) return a64_diag(diag, "direct AArch64 byte slice constant start is too large", view->line, view->column, "unsupported byte slice");
      if (byte_start > 0) z_aarch64_emit_add_x_imm(text, reg, reg, byte_start);
      return true;
    }
    unsigned tmp = reg == 8 ? 9 : 8;
    if (!a64_emit_store_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    if (!a64_emit_value_to_reg_at(text, fun, view->index, tmp, frame_size, scratch_slot + 1, ctx, diag)) return false;
    if (!a64_emit_load_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    a64_emit_add_scaled_index(text, reg, reg, tmp, a64_view_element_type(view));
    return true;
  }
  return a64_diag(diag, "direct AArch64 value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool a64_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return a64_diag(diag, "direct AArch64 byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    a64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE) {
    if (!view->index && !view->right) return a64_emit_byte_view_pair_at(text, fun, view->left, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
    if (!a64_emit_byte_view_pair_at(text, fun, view->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, view->left, diag)) return false;
    if (!a64_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
    if (view->index) {
      if (!a64_emit_value_to_reg_at(text, fun, view->index, 8, frame_size, scratch_slot + 2, ctx, diag)) return false;
    } else {
      z_aarch64_emit_movz_w(text, 8, 0);
    }
    if (!a64_emit_store_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
    if (view->right) {
      if (!a64_emit_value_to_reg_at(text, fun, view->right, 9, frame_size, scratch_slot + 3, ctx, diag)) return false;
      if (!a64_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      if (!a64_emit_load_scratch(text, 12, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      a64_emit_u32_upper_bound_check(text, 8, 9);
      a64_emit_u32_upper_bound_check(text, 9, 12);
      a64_emit_binary_reg(text, IR_BIN_SUB, 10, 9, 8, false);
    } else {
      if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      if (!a64_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      a64_emit_u32_upper_bound_check(text, 8, 10);
      a64_emit_binary_reg(text, IR_BIN_SUB, 10, 10, 8, false);
    }
    if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, view->left, diag)) return false;
    if (!a64_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
    a64_emit_add_scaled_index(text, 11, 11, 8, a64_view_element_type(view));
    a64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 11, 10);
    return true;
  }
  if (!a64_emit_byte_view_ptr_at(text, fun, view, ptr_reg, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
  if (!a64_emit_byte_view_len_at(text, fun, view, len_reg, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
  return true;
}

static bool a64_emit_cast_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!a64_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
  a64_emit_cast_normalize_reg(text, reg, value->left ? value->left->type : IR_TYPE_UNSUPPORTED, value->type);
  return true;
}

static bool a64_emit_binary_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (value->binary_op == IR_BIN_AND) {
    if (!a64_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t left_false = z_aarch64_emit_cbz_w_placeholder(text, reg);
    if (!a64_emit_value_to_reg_at(text, fun, value->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t right_false = z_aarch64_emit_cbz_w_placeholder(text, reg);
    z_aarch64_emit_movz_w(text, reg, 1);
    size_t end_patch = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, left_false, text->len);
    z_aarch64_patch_cond19(text, right_false, text->len);
    z_aarch64_emit_movz_w(text, reg, 0);
    z_aarch64_patch_branch26(text, end_patch, text->len);
    return true;
  }
  if (value->binary_op == IR_BIN_OR) {
    if (!a64_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t eval_right = z_aarch64_emit_cbz_w_placeholder(text, reg);
    z_aarch64_emit_movz_w(text, reg, 1);
    size_t left_true_end = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, eval_right, text->len);
    if (!a64_emit_value_to_reg_at(text, fun, value->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t right_false = z_aarch64_emit_cbz_w_placeholder(text, reg);
    z_aarch64_emit_movz_w(text, reg, 1);
    size_t right_true_end = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, right_false, text->len);
    z_aarch64_emit_movz_w(text, reg, 0);
    z_aarch64_patch_branch26(text, left_true_end, text->len);
    z_aarch64_patch_branch26(text, right_true_end, text->len);
    return true;
  }
  if (value->binary_op != IR_BIN_ADD && value->binary_op != IR_BIN_SUB && value->binary_op != IR_BIN_MUL &&
      value->binary_op != IR_BIN_DIV && value->binary_op != IR_BIN_MOD) {
    return a64_diag(diag, "direct AArch64 binary operator is unsupported", value->line, value->column, "unsupported operator");
  }
  if (!a64_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, value->left ? value->left->type : IR_TYPE_I32, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_value_to_reg_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, value->left ? value->left->type : IR_TYPE_I32, scratch_slot, value->left, diag)) return false;
  bool wide = a64_type_is_scalar64(value->type);
  if (value->binary_op == IR_BIN_DIV) {
    z_aarch64_emit_div_reg(text, reg, 8, 9, a64_type_is_unsigned(value->type), wide);
  } else if (value->binary_op == IR_BIN_MOD) {
    z_aarch64_emit_div_reg(text, 10, 8, 9, a64_type_is_unsigned(value->type), wide);
    z_aarch64_emit_msub_reg(text, reg, 10, 9, 8, wide);
  } else {
    a64_emit_binary_reg(text, value->binary_op, reg, 8, 9, wide);
  }
  return true;
}

static bool a64_emit_compare_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 comparison requires two operands", value->line, value->column, "invalid comparison");
  if (!a64_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_value_to_reg_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  bool wide = a64_type_is_scalar64(value->left->type);
  bool uns = a64_type_is_unsigned(value->left->type);
  if (wide) z_aarch64_emit_cmp_x(text, 8, 9);
  else z_aarch64_emit_cmp_w(text, 8, 9);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t false_patch = z_aarch64_emit_b_cond_placeholder(text, a64_invert_cond(a64_cond_for_compare(value->compare_op, uns)));
  z_aarch64_emit_movz_w(text, reg, 1);
  z_aarch64_patch_cond19(text, false_patch, text->len);
  return true;
}

static bool a64_emit_byte_copy_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 12, 13, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 12, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 12, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  z_aarch64_emit_byte_copy_min_loop(text, reg);
  return true;
}

static bool a64_emit_byte_fill_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 byte fill requires a fill byte and destination byte view", value->line, value->column, "missing byte fill input");
  if (!a64_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, IR_TYPE_U8, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 11, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->right, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U8, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->right, diag)) return false;
  z_aarch64_emit_byte_fill_loop(text, reg);
  return true;
}

static bool a64_emit_byte_view_eq_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 byte-view equality requires two byte views", value->line, value->column, "missing byte view");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 11, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 12, 9, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t same_len = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, same_len, text->len);
  if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  a64_emit_scale_len_for_element(text, 10, a64_view_element_type(value->left));
  z_aarch64_emit_byte_eq_loop(text, reg);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool a64_emit_crc32_bytes_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left) return a64_diag(diag, "direct AArch64 CRC32 requires a byte view", value->line, value->column, "missing byte view");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_crc32_bytes_loop(text, reg);
  return true;
}

static bool a64_emit_byte_view_index_load_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->index || !a64_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 9, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  a64_emit_u32_bounds_check(text, 8, 10);
  if (!a64_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  IrTypeKind element_type = a64_view_element_type(value->left);
  a64_emit_add_scaled_index(text, 9, 9, 8, element_type);
  a64_emit_load_ptr_element(text, reg, 9, element_type);
  return true;
}

static bool a64_emit_index_load_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (value->array_index >= fun->local_len) return a64_diag(diag, "direct AArch64 indexed load array is out of range", value->line, value->column, "invalid array local");
  const IrLocal *local = &fun->locals[value->array_index];
  unsigned const_index = 0;
  if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32) &&
      a64_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
    a64_emit_load_local_w(text, fun, reg, value->array_index, const_index * 4u, frame_size);
    return true;
  }
  if (local->is_array && (local->element_type == IR_TYPE_U16 || local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 ||
                          local->element_type == IR_TYPE_USIZE || a64_type_is_scalar64(local->element_type))) {
    if (!value->index || !a64_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, local->array_len);
    a64_emit_u32_bounds_check(text, 8, 9);
    if (!a64_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
    z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, value->array_index, 0, frame_size));
    a64_emit_add_scaled_index(text, 9, 9, 8, local->element_type);
    a64_emit_load_ptr_element(text, reg, 9, local->element_type);
    return true;
  }
  if (!local->is_array || (local->element_type != IR_TYPE_U8 && local->element_type != IR_TYPE_BOOL)) return a64_diag(diag, "direct AArch64 indexed load requires [N]u8, [N]Bool, or integer arrays", value->line, value->column, "unsupported array local");
  if (!value->index || !a64_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, local->array_len);
  a64_emit_u32_bounds_check(text, 8, 9);
  if (!a64_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, value->array_index, 0, frame_size));
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  z_aarch64_emit_load_b_imm(text, reg, 9, 0);
  return true;
}

static bool a64_emit_call_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  const IrFunction *callee = ctx && ctx->program && !value->external_call && value->callee_index < ctx->program->function_len ? &ctx->program->functions[value->callee_index] : NULL;
  const IrExternalFunction *external = ctx && ctx->program && value->external_call && value->external_index < ctx->program->external_function_len ? &ctx->program->external_functions[value->external_index] : NULL;
  if (!callee && !external) return a64_diag(diag, "direct AArch64 call target is unavailable", value->line, value->column, "invalid callee");
  size_t param_count = external ? external->param_len : callee->param_count;
  unsigned abi_slots = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (i >= param_count) return a64_diag(diag, "direct AArch64 call parameter metadata is unavailable", value->line, value->column, "invalid callee parameter");
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    unsigned slots = param_type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
    if (abi_slots + slots > 8) return a64_diag(diag, "direct AArch64 call supports at most eight ABI argument slots", value->line, value->column, "too many arguments");
    abi_slots += slots;
  }
  if (scratch_slot + abi_slots >= A64_DIRECT_SCRATCH_SLOT_COUNT) {
    return a64_diag(diag, "direct AArch64 call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
  }
  unsigned nested_slot = scratch_slot + abi_slots;
  unsigned arg_slot = scratch_slot;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    if (param_type == IR_TYPE_BYTE_VIEW) {
      if (!a64_emit_byte_view_pair_at(text, fun, arg, 8, 9, frame_size, nested_slot, ctx, diag)) return false;
      if (!a64_emit_store_scratch(text, 8, IR_TYPE_U64, arg_slot, arg, diag)) return false;
      if (!a64_emit_store_scratch(text, 9, IR_TYPE_U32, arg_slot + 1, arg, diag)) return false;
      arg_slot += 2;
      continue;
    }
    if (!a64_emit_value_to_reg_at(text, fun, arg, 8, frame_size, nested_slot, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 8, arg ? arg->type : IR_TYPE_I32, arg_slot, arg, diag)) return false;
    arg_slot++;
  }
  arg_slot = scratch_slot;
  unsigned abi_slot = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    if (param_type == IR_TYPE_BYTE_VIEW) {
      if (!a64_emit_load_scratch(text, abi_slot, IR_TYPE_U64, arg_slot, arg, diag)) return false;
      if (!a64_emit_load_scratch(text, abi_slot + 1, IR_TYPE_U32, arg_slot + 1, arg, diag)) return false;
      arg_slot += 2;
      abi_slot += 2;
      continue;
    }
    if (!a64_emit_load_scratch(text, abi_slot, arg ? arg->type : IR_TYPE_I32, arg_slot, arg, diag)) return false;
    arg_slot++;
    abi_slot++;
  }
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_call_patch(ctx, patch, value->external_call ? 0 : value->callee_index, diag, value)) return false;
  if (reg != 0) {
    if (a64_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool a64_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value) return a64_diag(diag, "direct AArch64 expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      if (a64_type_is_scalar64(value->type)) z_aarch64_emit_movz_x(text, reg, (uint64_t)value->int_value);
      else z_aarch64_emit_movz_w(text, reg, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL:
      if (value->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 local index is out of range", value->line, value->column, "invalid local");
      if (fun->locals[value->local_index].is_array) return a64_diag(diag, "direct AArch64 fixed array local cannot be used as a scalar", value->line, value->column, "array local");
      if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) return a64_diag(diag, "direct AArch64 byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
      if (a64_type_is_scalar64(fun->locals[value->local_index].type)) a64_emit_load_local_x(text, fun, reg, value->local_index, 0, frame_size);
      else a64_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_CAST: return a64_emit_cast_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BINARY: return a64_emit_binary_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_COMPARE: return a64_emit_compare_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CALL: return a64_emit_call_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_RAND_NEXT_U32:
      if (value->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      a64_emit_load_local_w(text, fun, 8, value->local_index, 0, frame_size);
      z_aarch64_emit_movz_w(text, 9, 1664525u);
      z_aarch64_emit_mul_w_reg(text, 8, 8, 9);
      z_aarch64_emit_movz_w(text, 9, 1013904223u);
      z_aarch64_emit_add_w_reg(text, 8, 8, 9);
      a64_emit_store_local_w(text, fun, 8, value->local_index, 0, frame_size);
      if (reg != 8) z_aarch64_emit_mov_w(text, reg, 8);
      return true;
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len ||
          (fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW && fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR)) {
        return a64_diag(diag, "direct AArch64 maybe helper requires a Maybe local", value->line, value->column, "invalid maybe local");
      }
      a64_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_MAYBE_VALUE:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR) {
        return a64_diag(diag, "direct AArch64 maybe scalar value requires a Maybe scalar local", value->line, value->column, "invalid maybe value");
      }
      if (a64_type_is_scalar64(value->type)) a64_emit_load_local_x(text, fun, reg, value->local_index, 8, frame_size);
      else a64_emit_load_local_w(text, fun, reg, value->local_index, 8, frame_size);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN: return a64_emit_byte_view_len_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_COPY: return a64_emit_byte_copy_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_FILL: return a64_emit_byte_fill_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ: return a64_emit_byte_view_eq_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CRC32_BYTES: return a64_emit_crc32_bytes_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: return a64_emit_byte_view_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_INDEX_LOAD: return a64_emit_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    default:
      return a64_diag(diag, "direct AArch64 value kind is unsupported", value->line, value->column, "unsupported value");
  }
}

static size_t a64_function_frame_bytes(const IrFunction *fun) {
  uint32_t ignored = 0;
  if (a64_return_literal(fun, &ignored, NULL)) return 0;
  unsigned base = (unsigned)(fun ? (fun->frame_bytes ? fun->frame_bytes : fun->local_len * 8) : 0);
  return z_aarch64_align(base + A64_DIRECT_SCRATCH_SLOT_COUNT * A64_DIRECT_SCRATCH_SLOT_BYTES, 16);
}

size_t z_aarch64_direct_stack_bytes_from_ir(const IrProgram *program) {
  size_t total = 0;
  for (size_t i = 0; program && i < program->function_len; i++) total += a64_function_frame_bytes(&program->functions[i]);
  return total;
}

size_t z_aarch64_direct_max_frame_bytes_from_ir(const IrProgram *program) {
  size_t max_frame = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    size_t frame = a64_function_frame_bytes(&program->functions[i]);
    if (frame > max_frame) max_frame = frame;
  }
  return max_frame;
}

static void a64_emit_epilogue(ZBuf *text, unsigned frame_size) {
  if (frame_size > 0) z_aarch64_emit_add_sp_imm(text, frame_size);
  z_aarch64_emit_ldp_x29_x30_sp_post16(text);
  z_aarch64_emit_ret(text);
}

static void a64_emit_void_result(ZBuf *text, const IrFunction *fun) {
  if (fun && fun->return_type == IR_TYPE_VOID) z_aarch64_emit_movz_w(text, 0, 0);
}

static bool a64_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag);

static bool a64_emit_local_set(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 local store is out of range", instr->line, instr->column, "invalid local");
  const IrLocal *local = &fun->locals[instr->local_index];
  if (local->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_byte_view_pair_at(text, fun, instr->value, 8, 9, frame_size, 0, ctx, diag)) return false;
    a64_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
    a64_emit_store_local_w(text, fun, 9, instr->local_index, 8, frame_size);
    return true;
  }
  if (local->type == IR_TYPE_MAYBE_BYTE_VIEW) {
    if (!instr->value) return a64_diag(diag, "direct AArch64 Maybe byte-view initializer is missing", instr->line, instr->column, "missing maybe value");
    if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
      z_aarch64_emit_movz_w(text, 8, instr->value->data_len ? 1u : 0u);
      a64_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
      if (!instr->value->data_len) {
        z_aarch64_emit_movz_w(text, 8, 0);
        a64_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
        a64_emit_store_local_w(text, fun, 8, instr->local_index, 16, frame_size);
        return true;
      }
      if (!a64_emit_byte_view_pair_at(text, fun, instr->value->left, 8, 9, frame_size, 0, ctx, diag)) return false;
      a64_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
      a64_emit_store_local_w(text, fun, 9, instr->local_index, 16, frame_size);
      return true;
    }
    if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
      a64_emit_store_local_w(text, fun, 0, instr->local_index, 0, frame_size);
      a64_emit_store_local_x(text, fun, 1, instr->local_index, 8, frame_size);
      a64_emit_store_local_w(text, fun, 2, instr->local_index, 16, frame_size);
      return true;
    }
    return a64_diag(diag, "direct AArch64 Maybe byte-view initializer is unsupported", instr->line, instr->column, "unsupported Maybe byte-view initializer");
  }
  if (local->type == IR_TYPE_MAYBE_SCALAR) {
    if (!instr->value) return a64_diag(diag, "direct AArch64 Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
    if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
      z_aarch64_emit_movz_w(text, 8, instr->value->data_len ? 1u : 0u);
      a64_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
      z_aarch64_emit_movz_x(text, 8, (uint64_t)instr->value->int_value);
      a64_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
      return true;
    }
    if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
      if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
      a64_emit_store_local_w(text, fun, 0, instr->local_index, 0, frame_size);
      a64_emit_store_local_x(text, fun, 1, instr->local_index, 8, frame_size);
      return true;
    }
    if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len && fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
      a64_emit_load_local_w(text, fun, 8, instr->value->local_index, 0, frame_size);
      a64_emit_load_local_x(text, fun, 9, instr->value->local_index, 8, frame_size);
      a64_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
      a64_emit_store_local_x(text, fun, 9, instr->local_index, 8, frame_size);
      return true;
    }
    if (!a64_type_is_scalar(instr->value->type) && instr->value->type != IR_TYPE_BOOL) {
      return a64_diag(diag, "direct AArch64 Maybe scalar initializer is unsupported", instr->line, instr->column, "unsupported Maybe scalar initializer");
    }
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 8, frame_size, 0, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, 1);
    a64_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
    a64_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
    return true;
  }
  if (!a64_emit_value_to_reg_at(text, fun, instr->value, 8, frame_size, 0, ctx, diag)) return false;
  if (a64_type_is_scalar64(local->type)) a64_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
  else a64_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
  return true;
}

static bool a64_emit_index_store(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (instr->array_index >= fun->local_len) return a64_diag(diag, "direct AArch64 indexed store array is out of range", instr->line, instr->column, "invalid array local");
  const IrLocal *local = &fun->locals[instr->array_index];
  if (local->type == IR_TYPE_BYTE_VIEW) {
    IrTypeKind element_type = local->element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : local->element_type;
    if (!instr->value || instr->value->type != element_type) return a64_diag(diag, "direct AArch64 byte-view indexed store value type does not match span element", instr->line, instr->column, "unsupported byte-view store value");
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 10, element_type, 0, instr->value, diag)) return false;
    if (!instr->index || !a64_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
    a64_emit_load_local_w(text, fun, 9, instr->array_index, 8, frame_size);
    if (!a64_emit_load_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
    a64_emit_u32_bounds_check(text, 8, 9);
    a64_emit_load_local_x(text, fun, 9, instr->array_index, 0, frame_size);
    if (!a64_emit_load_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
    a64_emit_add_scaled_index(text, 9, 9, 8, element_type);
    if (!a64_emit_load_scratch(text, 10, element_type, 0, instr->value, diag)) return false;
    a64_emit_store_ptr_element(text, 10, 9, element_type);
    return true;
  }
  unsigned const_index = 0;
  if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32) &&
      a64_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
    a64_emit_store_local_w(text, fun, 10, instr->array_index, const_index * 4u, frame_size);
    return true;
  }
  if (local->is_array && (local->element_type == IR_TYPE_U16 || local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 ||
                          local->element_type == IR_TYPE_USIZE || a64_type_is_scalar64(local->element_type))) {
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
    if (!instr->index || !a64_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, local->array_len);
    a64_emit_u32_bounds_check(text, 8, 9);
    z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, instr->array_index, 0, frame_size));
    a64_emit_add_scaled_index(text, 9, 9, 8, local->element_type);
    if (!a64_emit_load_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
    a64_emit_store_ptr_element(text, 10, 9, local->element_type);
    return true;
  }
  if (!local->is_array || (local->element_type != IR_TYPE_U8 && local->element_type != IR_TYPE_BOOL)) return a64_diag(diag, "direct AArch64 indexed store requires [N]u8, [N]Bool, or integer arrays", instr->line, instr->column, "unsupported array local");
  if (!a64_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
  if (!instr->index || !a64_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, local->array_len);
  a64_emit_u32_bounds_check(text, 8, 9);
  z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, instr->array_index, 0, frame_size));
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  if (!a64_emit_load_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
  z_aarch64_emit_store_b_imm(text, 10, 9, 0);
  return true;
}

static bool a64_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return a64_diag(diag, "direct AArch64 World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!ctx || !ctx->emit_world_write) return a64_diag(diag, "direct AArch64 World write requires an executable target runtime", instr->line, instr->column, "unsupported instruction");
  if (!a64_emit_byte_view_pair_at(text, fun, instr->value, 1, 2, frame_size, 0, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 0, instr->field_offset == 2 ? 2u : 1u);
  return ctx->emit_world_write(text, instr, ctx, diag);
}

static bool a64_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_LOCAL_SET) return a64_emit_local_set(text, fun, instr, frame_size, ctx, diag);
  if (instr->kind == IR_INSTR_INDEX_STORE) return a64_emit_index_store(text, fun, instr, frame_size, ctx, diag);
  if (instr->kind == IR_INSTR_WORLD_WRITE) return a64_emit_world_write(text, fun, instr, frame_size, ctx, diag);
  if (instr->kind == IR_INSTR_EXPR) return !instr->value || a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag);
  if (instr->kind == IR_INSTR_RETURN) {
    if (fun->return_type == IR_TYPE_BYTE_VIEW && instr->value) {
      if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_BYTE_VIEW) {
        if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
      } else {
        if (!a64_emit_byte_view_pair_at(text, fun, instr->value, 0, 1, frame_size, 0, ctx, diag)) return false;
      }
      a64_emit_epilogue(text, frame_size);
      return true;
    }
    if (fun->return_type == IR_TYPE_MAYBE_BYTE_VIEW && instr->value) {
      if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
        if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
      } else if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
        if (!instr->value->data_len) {
          z_aarch64_emit_movz_w(text, 0, 0);
          z_aarch64_emit_movz_w(text, 1, 0);
          z_aarch64_emit_movz_w(text, 2, 0);
        } else {
          z_aarch64_emit_movz_w(text, 0, 1);
          if (!a64_emit_byte_view_pair_at(text, fun, instr->value->left, 1, 2, frame_size, 0, ctx, diag)) return false;
        }
      } else {
        return a64_diag(diag, "direct AArch64 Maybe byte-view return requires a Maybe byte-view value", instr->line, instr->column, "unsupported Maybe byte-view return");
      }
      a64_emit_epilogue(text, frame_size);
      return true;
    }
    if (fun->return_type == IR_TYPE_MAYBE_SCALAR && instr->value) {
      if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
        if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
      } else if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
        z_aarch64_emit_movz_w(text, 0, instr->value->data_len ? 1u : 0u);
        z_aarch64_emit_movz_x(text, 1, (uint64_t)instr->value->int_value);
      } else if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len && fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
        a64_emit_load_local_w(text, fun, 0, instr->value->local_index, 0, frame_size);
        a64_emit_load_local_x(text, fun, 1, instr->value->local_index, 8, frame_size);
      } else if (a64_type_is_scalar(instr->value->type) || instr->value->type == IR_TYPE_BOOL) {
        if (!a64_emit_value_to_reg_at(text, fun, instr->value, 1, frame_size, 0, ctx, diag)) return false;
        z_aarch64_emit_movz_w(text, 0, 1);
      } else {
        return a64_diag(diag, "direct AArch64 Maybe scalar return requires a Maybe scalar or scalar value", instr->line, instr->column, "unsupported Maybe scalar return");
      }
      a64_emit_epilogue(text, frame_size);
      return true;
    }
    if (instr->value && !a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
    a64_emit_void_result(text, fun);
    a64_emit_epilogue(text, frame_size);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
    size_t false_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
    if (!a64_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, false_patch, text->len);
      if (!a64_emit_instrs(text, fun, instr->else_instrs, instr->else_len, frame_size, ctx, diag)) return false;
      z_aarch64_patch_branch26(text, end_patch, text->len);
    } else {
      z_aarch64_patch_cond19(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 0, frame_size, 0, ctx, diag)) return false;
    size_t false_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
    if (!a64_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, ctx, diag)) return false;
    size_t loop_patch = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_branch26(text, loop_patch, loop_start);
    z_aarch64_patch_cond19(text, false_patch, text->len);
    return true;
  }
  return a64_diag(diag, "direct AArch64 instruction kind is unsupported", instr->line, instr->column, "unsupported instruction");
}

static bool a64_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!a64_emit_instr(text, fun, &instrs[i], frame_size, ctx, diag)) return false;
  }
  return true;
}

static bool a64_validate_function(const IrFunction *fun, ZDiag *diag) {
  if (!fun) return a64_diag(diag, "direct AArch64 backend requires a function", 1, 1, "missing function");
  size_t abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) abi_slots += a64_abi_slots_for_param(&fun->locals[i]);
  if (abi_slots > 8) return a64_diag(diag, "direct AArch64 backend supports at most eight ABI parameter slots", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !a64_type_is_scalar32(fun->return_type) && !a64_type_is_scalar64(fun->return_type) &&
      fun->return_type != IR_TYPE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_SCALAR) {
    return a64_diag(diag, "direct AArch64 backend supports Void, primitive scalars, byte-view, Maybe byte-view, and Maybe scalar returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->type == IR_TYPE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_SCALAR) continue;
    if (local->is_array && (local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_BOOL || local->element_type == IR_TYPE_U16 ||
                            local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE ||
                            a64_type_is_scalar64(local->element_type))) continue;
    if (local->is_array || !a64_type_is_scalar(local->type)) {
      return a64_diag(diag, "direct AArch64 backend supports only primitive scalar locals and fixed byte/integer arrays", local->line, local->column, local->name);
    }
  }
  return true;
}

static bool a64_emit_function_text(ZBuf *text, const IrFunction *fun, ZAArch64DirectContext *ctx, ZDiag *diag) {
  uint32_t literal = 0;
  if (a64_return_literal(fun, &literal, NULL)) {
    z_aarch64_emit_literal_return(text, literal);
    return true;
  }
  if (!a64_validate_function(fun, diag)) return false;
  unsigned frame_size = (unsigned)a64_function_frame_bytes(fun);
  z_aarch64_emit_stp_x29_x30_sp_pre16(text);
  z_aarch64_emit_mov_x29_sp(text);
  if (frame_size > 0) z_aarch64_emit_sub_sp_imm(text, frame_size);
  unsigned abi_slot = 0;
  for (size_t i = 0; i < fun->param_count; i++) {
    const IrLocal *local = &fun->locals[i];
    unsigned slots = a64_abi_slots_for_param(local);
    if (abi_slot + slots > 8) return a64_diag(diag, "direct AArch64 function has too many ABI argument slots", fun->line, fun->column, fun->name);
    if (local->type == IR_TYPE_BYTE_VIEW) {
      a64_emit_store_local_x(text, fun, abi_slot, (unsigned)i, 0, frame_size);
      a64_emit_store_local_w(text, fun, abi_slot + 1, (unsigned)i, 8, frame_size);
    } else if (a64_type_is_scalar64(local->type)) {
      a64_emit_store_local_x(text, fun, abi_slot, (unsigned)i, 0, frame_size);
    } else {
      a64_emit_store_local_w(text, fun, abi_slot, (unsigned)i, 0, frame_size);
    }
    abi_slot += slots;
  }
  if (!a64_emit_instrs(text, fun, fun->instrs, fun->instr_len, frame_size, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) {
    a64_emit_void_result(text, fun);
    a64_emit_epilogue(text, frame_size);
  }
  return true;
}

static const IrFunction *a64_find_main(const IrProgram *ir, unsigned *out_index, ZDiag *diag) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; ir && i < ir->function_len; i++) {
    if (ir->functions[i].is_exported && strcmp(ir->functions[i].name, "main") == 0) {
      if (fun) {
        a64_diag(diag, "direct AArch64 executable backend requires exactly one exported main function", ir->functions[i].line, ir->functions[i].column, ir->functions[i].name);
        return NULL;
      }
      fun = &ir->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    a64_diag(diag, "direct AArch64 executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

static unsigned a64_rodata_base_offset(const IrProgram *ir) {
  if (!ir || ir->data_segment_len == 0) return 0;
  unsigned base = ir->data_segments[0].offset;
  for (size_t i = 1; i < ir->data_segment_len; i++) {
    if (ir->data_segments[i].offset < base) base = ir->data_segments[i].offset;
  }
  return base;
}

static void a64_append_rodata(ZBuf *rodata, const IrProgram *ir, unsigned base_offset) {
  for (size_t i = 0; ir && i < ir->data_segment_len; i++) {
    const IrDataSegment *segment = &ir->data_segments[i];
    while (rodata->len < segment->offset - base_offset) zbuf_append_char(rodata, 0);
    for (size_t j = 0; j < segment->len; j++) zbuf_append_char(rodata, (char)segment->bytes[j]);
  }
}

bool z_aarch64_direct_emit_function_text(ZBuf *text, const IrFunction *fun, ZAArch64DirectContext *ctx, ZDiag *diag) {
  return a64_emit_function_text(text, fun, ctx, diag);
}

bool z_aarch64_direct_validate_function(const IrFunction *fun, ZDiag *diag) {
  return a64_validate_function(fun, diag);
}

const IrFunction *z_aarch64_direct_find_main(const IrProgram *program, unsigned *out_index, ZDiag *diag) {
  return a64_find_main(program, out_index, diag);
}

unsigned z_aarch64_direct_rodata_base_offset(const IrProgram *program) {
  return a64_rodata_base_offset(program);
}

void z_aarch64_direct_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset) {
  a64_append_rodata(rodata, program, base_offset);
}
