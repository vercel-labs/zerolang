#include "zero.h"
#include "aarch64_direct.h"
#include "aarch64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void a64_emit_load_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_load_b_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_load_local_h(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_load_h_imm(text, reg, 31u, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_load_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_load_x_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_w_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_b_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_h(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_h_imm(text, reg, 31u, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_store_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  z_aarch64_emit_store_x_sp(text, reg, a64_local_slot_offset(fun, local_index, slot_offset, frame_size));
}

static void a64_emit_load_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    a64_emit_load_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else if (type == IR_TYPE_U16) {
    a64_emit_load_local_h(text, fun, reg, local_index, field_offset, frame_size);
  } else if (a64_type_is_scalar64(type)) {
    a64_emit_load_local_x(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    a64_emit_load_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void a64_emit_store_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    a64_emit_store_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else if (type == IR_TYPE_U16) {
    a64_emit_store_local_h(text, fun, reg, local_index, field_offset, frame_size);
  } else if (a64_type_is_scalar64(type)) {
    a64_emit_store_local_x(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    a64_emit_store_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void a64_emit_load_field_indirect(ZBuf *text, unsigned reg, unsigned base, unsigned field_offset, IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_aarch64_emit_load_b_imm(text, reg, base, field_offset);
  } else if (type == IR_TYPE_U16) {
    z_aarch64_emit_load_h_imm(text, reg, base, field_offset);
  } else if (a64_type_is_scalar64(type)) {
    z_aarch64_emit_load_x_imm(text, reg, base, field_offset);
  } else {
    z_aarch64_emit_load_w_imm(text, reg, base, field_offset);
  }
}

static void a64_emit_store_field_indirect(ZBuf *text, unsigned reg, unsigned base, unsigned field_offset, IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_aarch64_emit_store_b_imm(text, reg, base, field_offset);
  } else if (type == IR_TYPE_U16) {
    z_aarch64_emit_store_h_imm(text, reg, base, field_offset);
  } else if (a64_type_is_scalar64(type)) {
    z_aarch64_emit_store_x_imm(text, reg, base, field_offset);
  } else {
    z_aarch64_emit_store_w_imm(text, reg, base, field_offset);
  }
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

static bool a64_emit_trap(ZBuf *text, ZAArch64DirectContext *ctx, ZDiag *diag, ZDirectTrapKind kind);

static bool a64_emit_u32_bounds_check(ZBuf *text, unsigned index_reg, unsigned len_reg, ZAArch64DirectContext *ctx, ZDiag *diag) {
  z_aarch64_emit_cmp_w(text, index_reg, len_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3);
  if (!a64_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_INDEX_BOUNDS)) return false;
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
}

static bool a64_emit_u32_upper_bound_check(ZBuf *text, unsigned value_reg, unsigned max_reg, ZAArch64DirectContext *ctx, ZDiag *diag) {
  z_aarch64_emit_cmp_w(text, value_reg, max_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 9);
  if (!a64_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_VALUE_BOUNDS)) return false;
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
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

static bool a64_record_runtime_patch(ZAArch64DirectContext *ctx, size_t patch_offset, ZAArch64DirectRuntimeHelper helper, ZDiag *diag, const IrValue *value) {
  if (!ctx || !ctx->record_runtime_patch) return a64_diag(diag, "direct AArch64 runtime helper requires an object target that can emit runtime relocations", value ? value->line : 1, value ? value->column : 1, "missing runtime relocation support");
  return ctx->record_runtime_patch(ctx->patch_user, patch_offset, helper, value, diag);
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

static bool a64_emit_trap(ZBuf *text, ZAArch64DirectContext *ctx, ZDiag *diag, ZDirectTrapKind kind) {
  /* Cold path: branch to the shared per-binary trap stub that prints a diagnostic. */
  if (ctx && ctx->emit_world_write && ctx->record_data_patch && ctx->trap_messages.lens[kind] > 0) {
    size_t patch = z_aarch64_emit_b_placeholder(text);
    if (z_direct_trap_branches_record(&ctx->trap_branches[kind], patch)) return true;
    return a64_diag(diag, "direct AArch64 backend ran out of memory while recording a trap branch", 1, 1, "allocation failed");
  }
  z_aarch64_emit_brk(text);
  return true;
}

bool z_aarch64_direct_emit_trap_stubs(ZBuf *text, ZAArch64DirectContext *ctx, ZDiag *diag) {
  for (unsigned kind = 0; ctx && kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    ZDirectTrapBranchList *branches = &ctx->trap_branches[kind];
    if (branches->len == 0) continue;
    while ((text->len % 4) != 0) zbuf_append_char(text, 0);
    size_t stub_offset = text->len;
    if (!a64_emit_rodata_ptr_literal(text, 1, ctx->trap_messages.offsets[kind], ctx, NULL, diag)) return false;
    z_aarch64_emit_movz_w(text, 2, ctx->trap_messages.lens[kind]);
    z_aarch64_emit_movz_w(text, 0, 2u);
    if (!ctx->emit_world_write(text, NULL, ctx, diag)) return false;
    z_aarch64_emit_brk(text);
    for (size_t i = 0; i < branches->len; i++) z_aarch64_patch_branch26(text, branches->items[i], stub_offset);
    branches->len = 0;
  }
  if (ctx) z_direct_trap_branches_free(ctx->trap_branches, Z_DIRECT_TRAP_KIND_COUNT);
  return true;
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
    if (const_len > Z_DIRECT_FRAME_LOCAL_LIMIT_BYTES) return a64_diag(diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, const_len);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > Z_DIRECT_FRAME_LOCAL_LIMIT_BYTES) return a64_diag(diag, "direct AArch64 byte-view length is too large for this backend", view->line, view->column, "large byte view");
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
  if (view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *record_local = &fun->locals[view->local_index];
    if (record_local->is_record_ref) {
      unsigned base = reg == 10 ? 11 : 10;
      a64_emit_load_local_x(text, fun, base, view->local_index, 0, frame_size);
      a64_emit_load_field_indirect(text, reg, base, view->field_offset + 8u, IR_TYPE_U32);
      return true;
    }
    if (!record_local->is_record) return a64_diag(diag, "direct AArch64 byte-view field load requires record local", view->line, view->column, "non-record local");
    a64_emit_load_field(text, fun, reg, view->local_index, view->field_offset + 8u, IR_TYPE_U32, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 1) z_aarch64_emit_mov_w(text, reg, 1);
    return true;
  }
  if (view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 1) z_aarch64_emit_mov_w(text, reg, 1);
    return true;
  }
  if (view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    unsigned ptr_tmp = reg == 11 ? 12 : 11;
    return a64_emit_byte_view_pair_at(text, fun, view, ptr_tmp, reg, frame_size, scratch_slot, ctx, diag);
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
  if (view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *record_local = &fun->locals[view->local_index];
    if (record_local->is_record_ref) {
      unsigned base = reg == 10 ? 11 : 10;
      a64_emit_load_local_x(text, fun, base, view->local_index, 0, frame_size);
      a64_emit_load_field_indirect(text, reg, base, view->field_offset, IR_TYPE_U64);
      return true;
    }
    if (!record_local->is_record) return a64_diag(diag, "direct AArch64 byte-view field load requires record local", view->line, view->column, "non-record local");
    a64_emit_load_field(text, fun, reg, view->local_index, view->field_offset, IR_TYPE_U64, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    return true;
  }
  if (view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    return true;
  }
  if (view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    unsigned len_tmp = reg == 10 ? 11 : 10;
    return a64_emit_byte_view_pair_at(text, fun, view, reg, len_tmp, frame_size, scratch_slot, ctx, diag);
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

static bool a64_emit_json_error_label_arm(ZBuf *text, const IrValue *view, unsigned index, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!view || index >= view->arg_len || !view->args[index] || view->args[index]->kind != IR_VALUE_STRING_LITERAL) {
    return a64_diag(diag, "direct AArch64 JSON error label requires string literal arms", view ? view->line : 1, view ? view->column : 1, "invalid JSON error label");
  }
  const IrValue *label = view->args[index];
  if (!a64_emit_rodata_ptr_literal(text, 0, label->data_offset, ctx, label, diag)) return false;
  z_aarch64_emit_movz_w(text, 1, label->data_len);
  return true;
}

static bool a64_emit_json_error_label_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!view || !view->left) return a64_diag(diag, "direct AArch64 JSON error label requires a status code", view ? view->line : 1, view ? view->column : 1, "missing JSON status");
  if (view->arg_len != 4) return a64_diag(diag, "direct AArch64 JSON error label requires four labels", view->line, view->column, "invalid JSON error label");
  if (!a64_emit_value_to_reg_at(text, fun, view->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, 0);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code0 = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, 9, 1);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code1 = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, 9, 2);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code2 = z_aarch64_emit_b_cond_placeholder(text, 0);
  if (!a64_emit_json_error_label_arm(text, view, 3, ctx, diag)) return false;
  size_t done3 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code0, text->len);
  if (!a64_emit_json_error_label_arm(text, view, 0, ctx, diag)) return false;
  size_t done0 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code1, text->len);
  if (!a64_emit_json_error_label_arm(text, view, 1, ctx, diag)) return false;
  size_t done1 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code2, text->len);
  if (!a64_emit_json_error_label_arm(text, view, 2, ctx, diag)) return false;
  size_t done2 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, done3, text->len);
  z_aarch64_patch_branch26(text, done0, text->len);
  z_aarch64_patch_branch26(text, done1, text->len);
  z_aarch64_patch_branch26(text, done2, text->len);
  a64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
  return true;
}

static bool a64_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return a64_diag(diag, "direct AArch64 byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    a64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
    return true;
  }
  if (view && view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) {
    if (!a64_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    a64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
    return true;
  }
  if (view && view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    return a64_emit_json_error_label_pair_at(text, fun, view, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
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
      if (!a64_emit_u32_upper_bound_check(text, 8, 9, ctx, diag)) return false;
      if (!a64_emit_u32_upper_bound_check(text, 9, 12, ctx, diag)) return false;
      a64_emit_binary_reg(text, IR_BIN_SUB, 10, 9, 8, false);
    } else {
      if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      if (!a64_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      if (!a64_emit_u32_upper_bound_check(text, 8, 10, ctx, diag)) return false;
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

static bool a64_emit_byte_view_to_scratch(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned slot, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!a64_emit_byte_view_pair_at(text, fun, view, 8, 9, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, IR_TYPE_U64, slot, view, diag)) return false;
  return a64_emit_store_scratch(text, 9, IR_TYPE_U32, slot + 1, view, diag);
}

static bool a64_emit_load_byte_view_from_scratch(ZBuf *text, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned slot, ZDiag *diag) {
  if (!a64_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, slot, view, diag)) return false;
  return a64_emit_load_scratch(text, len_reg, IR_TYPE_U32, slot + 1, view, diag);
}

static ZAArch64DirectRuntimeHelper a64_str_runtime_helper(IrStrOp op) {
  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
      return A64_DIRECT_RUNTIME_STR_BUFFER_OP;
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      return A64_DIRECT_RUNTIME_CRYPTO_DIGEST;
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
      return A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256;
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      return A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256_HEX;
    case IR_STR_OP_CONCAT:
      return A64_DIRECT_RUNTIME_STR_CONCAT;
    case IR_STR_OP_REPEAT:
      return A64_DIRECT_RUNTIME_STR_REPEAT;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      return A64_DIRECT_RUNTIME_STR_TRIM_OP;
    case IR_STR_OP_COUNT_BYTE:
      return A64_DIRECT_RUNTIME_STR_COUNT_BYTE;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      return A64_DIRECT_RUNTIME_STR_PAIR_OP;
    case IR_STR_OP_WORD_COUNT_ASCII:
      return A64_DIRECT_RUNTIME_STR_WORD_COUNT_ASCII;
  }
  return A64_DIRECT_RUNTIME_HELPER_COUNT;
}

static uint32_t a64_crypto_digest_op(IrStrOp op) {
  return op == IR_STR_OP_CRYPTO_SHA256_HEX ? 1u : 0u;
}

static void a64_emit_encoded_len_to_maybe_byte_view_regs(ZBuf *text) {
  z_aarch64_emit_mov_w(text, 2, 0);
  z_aarch64_emit_movz_w(text, 0, 0);
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
  z_aarch64_emit_sub_w_imm(text, 2, 2, 1);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_cond19(text, none, text->len);
}

static bool a64_emit_str_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  IrStrOp op = (IrStrOp)value->int_value;
  ZAArch64DirectRuntimeHelper helper = a64_str_runtime_helper(op);
  if (helper == A64_DIRECT_RUNTIME_HELPER_COUNT) return a64_diag(diag, "direct AArch64 std.str helper is unknown", value->line, value->column, "unknown std.str op");

  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      if (value->arg_len != 2) return a64_diag(diag, "direct AArch64 std.str buffer helper requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      z_aarch64_emit_movz_w(text, 4, helper == A64_DIRECT_RUNTIME_CRYPTO_DIGEST ? a64_crypto_digest_op(op) : (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      a64_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_CONCAT:
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      if (value->arg_len != 3) return a64_diag(diag, "direct AArch64 std.str three-view helper requires three arguments", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[2], scratch_slot + 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[2], 4, 5, scratch_slot + 4, diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      a64_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_REPEAT:
      if (value->arg_len != 3) return a64_diag(diag, "direct AArch64 std.str.repeat requires three arguments", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_value_to_reg_at(text, fun, value->args[2], 8, frame_size, scratch_slot + 8, ctx, diag)) return false;
      if (!a64_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      if (!a64_emit_load_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      a64_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      if (value->arg_len != 1) return a64_diag(diag, "direct AArch64 std.str borrowed-slice helper requires one argument", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      z_aarch64_emit_movz_w(text, 2, (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      z_aarch64_emit_mov_x(text, 8, 0);
      z_aarch64_emit_lsr_x_imm(text, 9, 8, 32);
      z_aarch64_emit_mov_w(text, 1, 8);
      if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      z_aarch64_emit_add_x_reg(text, 0, 0, 9);
      if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
      return true;
    case IR_STR_OP_COUNT_BYTE:
      if (value->arg_len != 2) return a64_diag(diag, "direct AArch64 std.str.countByte requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_value_to_reg_at(text, fun, value->args[1], 8, frame_size, scratch_slot + 8, ctx, diag)) return false;
      if (!a64_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_scratch(text, 2, IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      if (value->arg_len != 2) return a64_diag(diag, "direct AArch64 std.str pair helper requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      z_aarch64_emit_movz_w(text, 4, (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    case IR_STR_OP_WORD_COUNT_ASCII:
      if (value->arg_len != 1) return a64_diag(diag, "direct AArch64 std.str.wordCountAscii requires one argument", value->line, value->column, "invalid std.str arity");
      if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
      }
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
  }
  return a64_diag(diag, "direct AArch64 std.str helper is unsupported", value->line, value->column, "unsupported std.str op");
}

static bool a64_value_returns_maybe_usize(const IrValue *value) {
  return value && value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE;
}

static void a64_emit_parse_u32_result_to_maybe_regs(ZBuf *text) {
  z_aarch64_emit_mov_w(text, 1, 0);
  z_aarch64_emit_lsr_x_imm(text, 0, 0, 32);
}

static bool a64_emit_ascii_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return a64_diag(diag, "direct AArch64 std.ascii helper requires one byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.ascii arity");
  if (!a64_emit_value_to_reg_at(text, fun, value->args[0], 0, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 1, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_ASCII_OP, diag, value)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    z_aarch64_emit_mov_w(text, 2, 0);
    z_aarch64_emit_movz_w(text, 8, 0xffu);
    z_aarch64_emit_and_w_reg(text, 1, 0, 8);
    z_aarch64_emit_movz_w(text, 0, 0);
    size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
    z_aarch64_emit_movz_w(text, 0, 1);
    z_aarch64_patch_cond19(text, none, text->len);
    return true;
  }
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool a64_emit_text_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return a64_diag(diag, "direct AArch64 std.text helper requires one byte-view argument", value ? value->line : 1, value ? value->column : 1, "invalid std.text arity");
  if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_TEXT_OP, diag, value)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    z_aarch64_emit_mov_w(text, 2, 0);
    z_aarch64_emit_sub_w_imm(text, 1, 2, 1);
    z_aarch64_emit_movz_w(text, 0, 0);
    size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
    z_aarch64_emit_movz_w(text, 0, 1);
    z_aarch64_patch_cond19(text, none, text->len);
    return true;
  }
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool a64_emit_parse_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 1 || value->arg_len > 2) return a64_diag(diag, "direct AArch64 std.parse helper requires one byte-view argument and optional byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.parse arity");
  if (!a64_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (value->arg_len == 2) {
    if (!a64_emit_value_to_reg_at(text, fun, value->args[1], 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  } else {
    z_aarch64_emit_movz_x(text, 2, 0);
  }
  if (!a64_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, a64_value_returns_maybe_usize(value) ? A64_DIRECT_RUNTIME_PARSE_USIZE : A64_DIRECT_RUNTIME_PARSE_OP, diag, value)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!a64_value_returns_maybe_usize(value)) a64_emit_parse_u32_result_to_maybe_regs(text);
  } else if (reg != 0) {
    if (a64_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool a64_emit_parse_u32_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value) return a64_diag(diag, "direct AArch64 parse value is missing", 1, 1, "missing parse value");
  if (value->kind != IR_VALUE_PARSE_I32 && value->kind != IR_VALUE_PARSE_U32) {
    return a64_diag(diag, "direct AArch64 parse value kind is invalid for this helper", value->line, value->column, "invalid parse value");
  }
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, value->kind == IR_VALUE_PARSE_I32 ? A64_DIRECT_RUNTIME_PARSE_I32 : A64_DIRECT_RUNTIME_PARSE_U32, diag, value)) return false;
  a64_emit_parse_u32_result_to_maybe_regs(text);
  return true;
}

static ZAArch64DirectRuntimeHelper a64_fmt_runtime_helper(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_FMT_BOOL: return A64_DIRECT_RUNTIME_FMT_BOOL;
    case IR_VALUE_FMT_HEX_U32: return A64_DIRECT_RUNTIME_FMT_HEX_U32;
    case IR_VALUE_FMT_I32: return A64_DIRECT_RUNTIME_FMT_I32;
    case IR_VALUE_FMT_U32: return A64_DIRECT_RUNTIME_FMT_U32;
    case IR_VALUE_FMT_USIZE: return A64_DIRECT_RUNTIME_FMT_USIZE;
    default: return A64_DIRECT_RUNTIME_HELPER_COUNT;
  }
}

static bool a64_emit_fmt_to_maybe_byte_view_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return a64_diag(diag, "direct AArch64 std.fmt helper requires a buffer and value", value ? value->line : 1, value ? value->column : 1, "missing fmt input");
  ZAArch64DirectRuntimeHelper helper = a64_fmt_runtime_helper(value->kind);
  if (helper == A64_DIRECT_RUNTIME_HELPER_COUNT) return a64_diag(diag, "direct AArch64 std.fmt helper is unsupported", value->line, value->column, "unsupported std.fmt helper");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_value_to_reg_at(text, fun, value->right, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
  z_aarch64_emit_mov_w(text, 2, 0);
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 0, 0);
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_cond19(text, none, text->len);
  return true;
}

static bool a64_emit_math_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) {
    return a64_diag(diag, "direct AArch64 std.math helper supports at most three scalar arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.math arity");
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!a64_emit_value_to_reg_at(text, fun, value->args[i], 8, frame_size, scratch_slot + 3 + (unsigned)i, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 8, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
  }
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!a64_emit_load_scratch(text, (unsigned)i, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
    } else {
      z_aarch64_emit_movz_x(text, (unsigned)i, 0);
    }
  }
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, a64_value_returns_maybe_usize(value) ? A64_DIRECT_RUNTIME_MATH_USIZE_OP : A64_DIRECT_RUNTIME_MATH_OP, diag, value)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!a64_value_returns_maybe_usize(value)) a64_emit_parse_u32_result_to_maybe_regs(text);
  } else if (a64_type_is_scalar64(value->type)) {
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  } else {
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool a64_emit_search_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return a64_diag(diag, "direct AArch64 std.search helper requires a span and needle", value ? value->line : 1, value ? value->column : 1, "invalid std.search input");
  if (!a64_emit_byte_view_to_scratch(text, fun, value->left, scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_value_to_reg_at(text, fun, value->right, 8, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, value->right->type, scratch_slot + 2, value->right, diag)) return false;
  if (!a64_emit_load_byte_view_from_scratch(text, value->left, 0, 1, scratch_slot, diag)) return false;
  if (!a64_emit_load_scratch(text, 2, value->right->type, scratch_slot + 2, value->right, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_SEARCH_OP, diag, value)) return false;
  if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  return true;
}

static bool a64_emit_sort_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return a64_diag(diag, "direct AArch64 std.sort helper requires a span", value ? value->line : 1, value ? value->column : 1, "invalid std.sort input");
  if (!a64_emit_byte_view_to_scratch(text, fun, value->left, scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_load_byte_view_from_scratch(text, value->left, 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  ZAArch64DirectRuntimeHelper helper = value->type == IR_TYPE_BOOL ? A64_DIRECT_RUNTIME_SORT_IS_SORTED_OP : A64_DIRECT_RUNTIME_SORT_OP;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
  if (value->type == IR_TYPE_BOOL && reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool a64_emit_json_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return a64_diag(diag, "direct AArch64 JSON helper requires a byte view", value ? value->line : 1, value ? value->column : 1, "invalid JSON input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES) z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES ? A64_DIRECT_RUNTIME_JSON_DIAGNOSTIC : A64_DIRECT_RUNTIME_JSON_PARSE_BYTES, diag, value)) return false;
  if (value->kind == IR_VALUE_JSON_VALIDATE_BYTES) {
    z_aarch64_emit_cmp_x(text, 0, 31);
    z_aarch64_emit_movz_w(text, reg, 0);
    size_t invalid = z_aarch64_emit_b_cond_placeholder(text, 11);
    z_aarch64_emit_movz_w(text, reg, 1);
    z_aarch64_patch_cond19(text, invalid, text->len);
  } else if (value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES) {
    z_aarch64_emit_cmp_x(text, 0, 31);
    size_t ok = z_aarch64_emit_b_cond_placeholder(text, 10);
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 31); else z_aarch64_emit_mov_x(text, 0, 31);
    size_t done = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, ok, text->len);
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    z_aarch64_patch_branch26(text, done, text->len);
  } else if (reg != 0) {
    z_aarch64_emit_mov_x(text, reg, 0);
  }
  return true;
}

static bool a64_emit_json_field_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return a64_diag(diag, "direct AArch64 JSON field helper requires bytes and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON field input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_JSON_FIELD, diag, value)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool a64_emit_json_lookup_scalar_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return a64_diag(diag, "direct AArch64 JSON scalar helper requires bytes and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON scalar input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 4, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_JSON_LOOKUP_SCALAR, diag, value)) return false;
  a64_emit_parse_u32_result_to_maybe_regs(text);
  return true;
}

static bool a64_emit_json_string_decode_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return a64_diag(diag, "direct AArch64 JSON string decode helper requires a buffer and string", value ? value->line : 1, value ? value->column : 1, "invalid JSON string decode input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  ZAArch64DirectRuntimeHelper helper = value->kind == IR_VALUE_JSON_WRITE_STRING ? A64_DIRECT_RUNTIME_JSON_WRITE_STRING : A64_DIRECT_RUNTIME_JSON_STRING_DECODE;
  if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool a64_emit_json_string_field_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) return a64_diag(diag, "direct AArch64 JSON string field helper requires a buffer, bytes, and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON string field input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!a64_emit_store_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->index, 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!a64_emit_load_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_JSON_STRING_FIELD, diag, value)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 4, value, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 4, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static ZAArch64DirectRuntimeHelper a64_json_write_runtime_helper(IrJsonWriteOp op) {
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW: return A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_RAW;
    case IR_JSON_WRITE_FIELD_STRING: return A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_STRING;
    case IR_JSON_WRITE_FIELD_U32: return A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_U32;
    case IR_JSON_WRITE_FIELD_BOOL: return A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_BOOL;
    case IR_JSON_WRITE_OBJECT1_STRING: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_STRING;
    case IR_JSON_WRITE_OBJECT1_U32: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_U32;
    case IR_JSON_WRITE_OBJECT1_BOOL: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_BOOL;
    case IR_JSON_WRITE_OBJECT2_FIELDS: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_FIELDS;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD;
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD: return A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD;
    case IR_JSON_WRITE_ARRAY2_STRINGS: return A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_STRINGS;
    case IR_JSON_WRITE_ARRAY2_U32: return A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_U32;
    case IR_JSON_WRITE_ARRAY2_BOOLS: return A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_BOOLS;
  }
  return A64_DIRECT_RUNTIME_HELPER_COUNT;
}

static bool a64_emit_json_writer_store_view_arg(ZBuf *text, const IrFunction *fun, const IrValue *value, size_t arg_index, unsigned pair_slot, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || arg_index >= value->arg_len) return a64_diag(diag, "direct AArch64 JSON writer is missing a span argument", value ? value->line : 1, value ? value->column : 1, "missing JSON writer argument");
  if (!a64_emit_byte_view_pair_at(text, fun, value->args[arg_index], 8, 9, frame_size, scratch_slot + pair_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, IR_TYPE_U64, scratch_slot + pair_slot, value->args[arg_index], diag)) return false;
  return a64_emit_store_scratch(text, 9, IR_TYPE_U32, scratch_slot + pair_slot + 1, value->args[arg_index], diag);
}

static bool a64_emit_json_writer_store_scalar_arg(ZBuf *text, const IrFunction *fun, const IrValue *value, size_t arg_index, unsigned slot, IrTypeKind type, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || arg_index >= value->arg_len) return a64_diag(diag, "direct AArch64 JSON writer is missing a scalar argument", value ? value->line : 1, value ? value->column : 1, "missing JSON writer argument");
  if (!a64_emit_value_to_reg_at(text, fun, value->args[arg_index], 8, frame_size, scratch_slot + slot, ctx, diag)) return false;
  return a64_emit_store_scratch(text, 8, type, scratch_slot + slot, value->args[arg_index], diag);
}

static bool a64_emit_json_writer_load_view_arg(ZBuf *text, const IrValue *value, size_t arg_index, unsigned pair_slot, unsigned ptr_reg, unsigned len_reg, unsigned scratch_slot, ZDiag *diag) {
  if (!a64_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot + pair_slot, value->args[arg_index], diag)) return false;
  return a64_emit_load_scratch(text, len_reg, IR_TYPE_U32, scratch_slot + pair_slot + 1, value->args[arg_index], diag);
}

static bool a64_emit_json_write_runtime_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 3) return a64_diag(diag, "direct AArch64 JSON writer requires arguments", value ? value->line : 1, value ? value->column : 1, "invalid JSON writer");
  IrJsonWriteOp op = (IrJsonWriteOp)value->int_value;
  ZAArch64DirectRuntimeHelper helper = a64_json_write_runtime_helper(op);
  if (helper == A64_DIRECT_RUNTIME_HELPER_COUNT) return a64_diag(diag, "direct AArch64 JSON writer op is invalid", value->line, value->column, "invalid JSON writer");
  if (!a64_emit_json_writer_store_view_arg(text, fun, value, 0, 0, frame_size, scratch_slot, ctx, diag)) return false;
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW:
    case IR_JSON_WRITE_FIELD_STRING:
    case IR_JSON_WRITE_OBJECT1_STRING:
    case IR_JSON_WRITE_OBJECT2_FIELDS:
    case IR_JSON_WRITE_ARRAY2_STRINGS:
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 2, 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 2, 4, 4, 5, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_FIELD_U32:
    case IR_JSON_WRITE_FIELD_BOOL:
    case IR_JSON_WRITE_OBJECT1_U32:
    case IR_JSON_WRITE_OBJECT1_BOOL:
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_scalar_arg(text, fun, value, 2, 4, op == IR_JSON_WRITE_FIELD_BOOL || op == IR_JSON_WRITE_OBJECT1_BOOL ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!a64_emit_load_scratch(text, 4, op == IR_JSON_WRITE_FIELD_BOOL || op == IR_JSON_WRITE_OBJECT1_BOOL ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      break;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD:
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 2, 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 3, 6, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 2, 4, 4, 5, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 3, 6, 6, 7, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD:
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD:
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_scalar_arg(text, fun, value, 2, 4, op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_view_arg(text, fun, value, 3, 5, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!a64_emit_load_scratch(text, 4, op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 3, 5, 5, 6, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_ARRAY2_U32:
    case IR_JSON_WRITE_ARRAY2_BOOLS:
      if (!a64_emit_json_writer_store_scalar_arg(text, fun, value, 1, 2, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_store_scalar_arg(text, fun, value, 2, 3, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!a64_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!a64_emit_load_scratch(text, 2, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      if (!a64_emit_load_scratch(text, 3, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 3, value->args[2], diag)) return false;
      break;
  }
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 7, value, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 7, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool a64_emit_http_request_span_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return a64_diag(diag, "direct AArch64 HTTP request helper requires a request span", value ? value->line : 1, value ? value->column : 1, "invalid HTTP request input");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  ZAArch64DirectRuntimeHelper helper = value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ? A64_DIRECT_RUNTIME_HTTP_REQUEST_METHOD_NAME : A64_DIRECT_RUNTIME_HTTP_REQUEST_PATH;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, helper, diag, value)) return false;
  if (!a64_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 1, value, diag)) return false;
  if (!a64_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 1, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool a64_emit_time_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) {
    return a64_diag(diag, "direct AArch64 std.time helper supports at most three scalar arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.time arity");
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!a64_emit_value_to_reg_at(text, fun, value->args[i], 8, frame_size, scratch_slot + 3 + (unsigned)i, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 8, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
  }
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!a64_emit_load_scratch(text, (unsigned)i, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
    } else {
      z_aarch64_emit_movz_x(text, (unsigned)i, 0);
    }
  }
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!a64_record_runtime_patch(ctx, patch, A64_DIRECT_RUNTIME_TIME_OP, diag, value)) return false;
  if (a64_type_is_scalar64(value->type)) {
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  } else {
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  }
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

static bool a64_emit_rand_bounded_from_w8(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, bool add_low) {
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 8);

  z_aarch64_emit_movz_w(text, 9, 0);
  z_aarch64_emit_sub_w_reg(text, 9, 9, 8);
  z_aarch64_emit_div_reg(text, 10, 9, 8, true, false);
  z_aarch64_emit_msub_reg(text, 9, 10, 8, 9, false);

  size_t loop = text->len;
  a64_emit_load_local_w(text, fun, 11, value->local_index, 0, frame_size);
  z_aarch64_emit_movz_w(text, 12, 1664525u);
  z_aarch64_emit_mul_w_reg(text, 11, 11, 12);
  z_aarch64_emit_movz_w(text, 12, 1013904223u);
  z_aarch64_emit_add_w_reg(text, 11, 11, 12);
  a64_emit_store_local_w(text, fun, 11, value->local_index, 0, frame_size);
  z_aarch64_emit_cmp_w(text, 11, 9);
  size_t retry = z_aarch64_emit_b_cond_placeholder(text, 3);

  z_aarch64_emit_div_reg(text, 12, 11, 8, true, false);
  z_aarch64_emit_msub_reg(text, 1, 12, 8, 11, false);
  if (add_low) z_aarch64_emit_add_w_reg(text, 1, 1, 13);
  z_aarch64_emit_movz_w(text, 0, 1);
  size_t done = z_aarch64_emit_b_placeholder(text);

  z_aarch64_patch_cond19(text, retry, loop);
  z_aarch64_patch_cond19(text, none, text->len);
  z_aarch64_emit_movz_w(text, 0, 0);
  z_aarch64_emit_movz_w(text, 1, 0);
  z_aarch64_patch_branch26(text, done, text->len);
  return true;
}

static bool a64_emit_rand_maybe_to_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value || value->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 std.rand bounded local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid RandSource");
  if (value->kind == IR_VALUE_RAND_NEXT_BELOW) {
    if (!a64_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
    return a64_emit_rand_bounded_from_w8(text, fun, value, frame_size, false);
  }
  if (value->kind == IR_VALUE_RAND_RANGE_U32) {
    if (!a64_emit_value_to_reg_at(text, fun, value->left, 13, frame_size, scratch_slot, ctx, diag)) return false;
    if (!a64_emit_store_scratch(text, 13, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
    if (!a64_emit_value_to_reg_at(text, fun, value->right, 8, frame_size, scratch_slot + 1, ctx, diag)) return false;
    if (!a64_emit_load_scratch(text, 13, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
    z_aarch64_emit_cmp_w(text, 8, 13);
    size_t empty = z_aarch64_emit_b_cond_placeholder(text, 9);
    z_aarch64_emit_sub_w_reg(text, 8, 8, 13);
    bool ok = a64_emit_rand_bounded_from_w8(text, fun, value, frame_size, true);
    size_t done = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, empty, text->len);
    z_aarch64_emit_movz_w(text, 0, 0);
    z_aarch64_emit_movz_w(text, 1, 0);
    z_aarch64_patch_branch26(text, done, text->len);
    return ok;
  }
  return a64_diag(diag, "direct AArch64 std.rand bounded helper is invalid", value->line, value->column, "invalid rand helper");
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

static void a64_emit_item_copy_loop(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  z_aarch64_emit_mov_w(text, 14, 13);
  z_aarch64_emit_cmp_w(text, 13, 10);
  size_t keep_dst_len = z_aarch64_emit_b_cond_placeholder(text, 9);
  z_aarch64_emit_mov_w(text, 14, 10);
  z_aarch64_patch_cond19(text, keep_dst_len, text->len);
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 14);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  a64_emit_add_scaled_index(text, 15, 11, 9, element_type);
  a64_emit_load_ptr_element(text, 15, 15, element_type);
  a64_emit_add_scaled_index(text, 13, 12, 9, element_type);
  a64_emit_store_ptr_element(text, 15, 13, element_type);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, reg, 14);
}

static bool a64_emit_item_copy_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 item copy requires source and destination views", value->line, value->column, "missing item view");
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 12, 13, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  a64_emit_item_copy_loop(text, reg, value->element_type == IR_TYPE_UNSUPPORTED ? a64_view_element_type(value->left) : value->element_type);
  return true;
}

static void a64_emit_item_fill_loop(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  a64_emit_add_scaled_index(text, 12, 11, 9, element_type);
  a64_emit_store_ptr_element(text, 8, 12, element_type);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, reg, 10);
}

static bool a64_emit_item_fill_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 item fill requires a value and destination view", value->line, value->column, "missing item fill input");
  IrTypeKind element_type = value->element_type == IR_TYPE_UNSUPPORTED ? value->left->type : value->element_type;
  if (!a64_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 8, element_type, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_byte_view_pair_at(text, fun, value->right, 11, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, element_type, scratch_slot, value->left, diag)) return false;
  a64_emit_item_fill_loop(text, reg, value->element_type == IR_TYPE_UNSUPPORTED ? a64_view_element_type(value->right) : value->element_type);
  return true;
}

static bool a64_emit_item_contains_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return a64_diag(diag, "direct AArch64 item contains requires an input view and needle", value->line, value->column, "missing item contains input");
  IrTypeKind element_type = value->element_type == IR_TYPE_UNSUPPORTED ? a64_view_element_type(value->left) : value->element_type;
  if (!a64_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!a64_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!a64_emit_value_to_reg_at(text, fun, value->right, 12, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!a64_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t done_without_match = z_aarch64_emit_b_cond_placeholder(text, 2);
  a64_emit_add_scaled_index(text, 13, 11, 9, element_type);
  a64_emit_load_ptr_element(text, 15, 13, element_type);
  if (a64_type_is_scalar64(element_type)) z_aarch64_emit_cmp_x(text, 15, 12);
  else z_aarch64_emit_cmp_w(text, 15, 12);
  size_t found = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done_without_match, text->len);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, found, text->len);
  z_aarch64_emit_movz_w(text, reg, 1);
  z_aarch64_patch_branch26(text, end, text->len);
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
  if (!a64_emit_u32_bounds_check(text, 8, 10, ctx, diag)) return false;
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
    if (!a64_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
  if (!a64_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
  if (!a64_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, value->array_index, 0, frame_size));
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  z_aarch64_emit_load_b_imm(text, reg, 9, 0);
  return true;
}

static bool a64_emit_field_load_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 field load record is out of range", value->line, value->column, "invalid record local");
  if (value->type == IR_TYPE_BYTE_VIEW) return a64_diag(diag, "direct AArch64 byte-view field load cannot be used as a scalar", value->line, value->column, "byte-view field load");
  const IrLocal *record_local = &fun->locals[value->local_index];
  if (record_local->is_record_ref) {
    unsigned base = reg == 10 ? 11 : 10;
    a64_emit_load_local_x(text, fun, base, value->local_index, 0, frame_size);
    a64_emit_load_field_indirect(text, reg, base, value->field_offset, value->type);
    return true;
  }
  if (!record_local->is_record) return a64_diag(diag, "direct AArch64 field load requires record local", value->line, value->column, "non-record local");
  a64_emit_load_field(text, fun, reg, value->local_index, value->field_offset, value->type, frame_size);
  return true;
}

static bool a64_emit_record_addr_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 record address local is out of range", value->line, value->column, "invalid record local");
  if (!fun->locals[value->local_index].is_record) return a64_diag(diag, "direct AArch64 record address requires record local", value->line, value->column, "non-record local");
  z_aarch64_emit_add_x_sp_imm(text, reg, a64_local_slot_offset(fun, value->local_index, 0, frame_size));
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
  if (value->external_call) a64_emit_cast_normalize_reg(text, 0, value->type, value->type);
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
    case IR_VALUE_ASCII_RUNTIME: return a64_emit_ascii_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_TEXT_RUNTIME: return a64_emit_text_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PARSE_RUNTIME: return a64_emit_parse_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32:
      (void)reg;
      return a64_emit_parse_u32_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE:
      (void)reg;
      return a64_emit_fmt_to_maybe_byte_view_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_STR_RUNTIME: return a64_emit_str_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_TIME_RUNTIME: return a64_emit_time_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_MATH_RUNTIME: return a64_emit_math_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_SEARCH_RUNTIME: return a64_emit_search_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_SORT_RUNTIME: return a64_emit_sort_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_PARSE_BYTES:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
      return a64_emit_json_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_FIELD:
      (void)reg;
      return a64_emit_json_field_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_LOOKUP_SCALAR:
      (void)reg;
      return a64_emit_json_lookup_scalar_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_STRING_DECODE:
      (void)reg;
      return a64_emit_json_string_decode_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_WRITE_STRING:
      (void)reg;
      return a64_emit_json_string_decode_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_WRITE_RUNTIME:
      (void)reg;
      return a64_emit_json_write_runtime_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_STRING_FIELD:
      (void)reg;
      return a64_emit_json_string_field_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME:
    case IR_VALUE_HTTP_REQUEST_PATH:
      (void)reg;
      return a64_emit_http_request_span_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
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
    case IR_VALUE_RAND_NEXT_BELOW:
    case IR_VALUE_RAND_RANGE_U32:
      (void)reg;
      return a64_emit_rand_maybe_to_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
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
    case IR_VALUE_ITEM_COPY: return a64_emit_item_copy_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ITEM_FILL: return a64_emit_item_fill_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ITEM_CONTAINS: return a64_emit_item_contains_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ: return a64_emit_byte_view_eq_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CRC32_BYTES: return a64_emit_crc32_bytes_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: return a64_emit_byte_view_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_INDEX_LOAD: return a64_emit_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FIELD_LOAD: return a64_emit_field_load_to_reg_at(text, fun, value, reg, frame_size, diag);
    case IR_VALUE_RECORD_ADDR: return a64_emit_record_addr_to_reg(text, fun, value, reg, frame_size, diag);
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
    if ((instr->value->kind == IR_VALUE_CALL ||
         instr->value->kind == IR_VALUE_STR_RUNTIME ||
         instr->value->kind == IR_VALUE_FMT_BOOL ||
         instr->value->kind == IR_VALUE_FMT_HEX_U32 ||
         instr->value->kind == IR_VALUE_FMT_I32 ||
         instr->value->kind == IR_VALUE_FMT_U32 ||
         instr->value->kind == IR_VALUE_FMT_USIZE ||
         instr->value->kind == IR_VALUE_JSON_FIELD ||
         instr->value->kind == IR_VALUE_JSON_STRING_DECODE ||
         instr->value->kind == IR_VALUE_JSON_STRING_FIELD ||
         instr->value->kind == IR_VALUE_JSON_WRITE_STRING ||
         instr->value->kind == IR_VALUE_JSON_WRITE_RUNTIME ||
         instr->value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
         instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH) && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
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
    if ((instr->value->kind == IR_VALUE_CALL ||
         instr->value->kind == IR_VALUE_ASCII_RUNTIME ||
         instr->value->kind == IR_VALUE_TEXT_RUNTIME ||
         instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
         instr->value->kind == IR_VALUE_PARSE_I32 ||
         instr->value->kind == IR_VALUE_PARSE_U32 ||
         instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ||
         instr->value->kind == IR_VALUE_MATH_RUNTIME ||
         instr->value->kind == IR_VALUE_RAND_NEXT_BELOW ||
         instr->value->kind == IR_VALUE_RAND_RANGE_U32) && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
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
    if (!a64_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
    if (!a64_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
  if (!a64_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
  z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, instr->array_index, 0, frame_size));
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  if (!a64_emit_load_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
  z_aarch64_emit_store_b_imm(text, 10, 9, 0);
  return true;
}

static bool a64_emit_field_store(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return a64_diag(diag, "direct AArch64 field store record is out of range", instr->line, instr->column, "invalid record local");
  const IrLocal *record_local = &fun->locals[instr->local_index];
  if (instr->value && instr->value->type == IR_TYPE_BYTE_VIEW) {
    if (record_local->is_record_ref) {
      if (!a64_emit_byte_view_pair_at(text, fun, instr->value, 8, 9, frame_size, 0, ctx, diag)) return false;
      a64_emit_load_local_x(text, fun, 10, instr->local_index, 0, frame_size);
      a64_emit_store_field_indirect(text, 8, 10, instr->field_offset, IR_TYPE_U64);
      a64_emit_store_field_indirect(text, 9, 10, instr->field_offset + 8u, IR_TYPE_U32);
      return true;
    }
    if (!record_local->is_record) return a64_diag(diag, "direct AArch64 byte-view field store requires record local", instr->line, instr->column, "non-record local");
    if (!a64_emit_byte_view_pair_at(text, fun, instr->value, 8, 9, frame_size, 0, ctx, diag)) return false;
    a64_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, IR_TYPE_U64, frame_size);
    a64_emit_store_field(text, fun, 9, instr->local_index, instr->field_offset + 8u, IR_TYPE_U32, frame_size);
    return true;
  }
  IrTypeKind type = instr->value ? instr->value->type : IR_TYPE_I32;
  if (record_local->is_record_ref) {
    if (!a64_emit_value_to_reg_at(text, fun, instr->value, 8, frame_size, 0, ctx, diag)) return false;
    a64_emit_load_local_x(text, fun, 10, instr->local_index, 0, frame_size);
    a64_emit_store_field_indirect(text, 8, 10, instr->field_offset, type);
    return true;
  }
  if (!record_local->is_record) return a64_diag(diag, "direct AArch64 field store requires record local", instr->line, instr->column, "non-record local");
  if (!a64_emit_value_to_reg_at(text, fun, instr->value, 8, frame_size, 0, ctx, diag)) return false;
  a64_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, type, frame_size);
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
  if (instr->kind == IR_INSTR_FIELD_STORE) return a64_emit_field_store(text, fun, instr, frame_size, ctx, diag);
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
      if ((instr->value->kind == IR_VALUE_CALL ||
           instr->value->kind == IR_VALUE_STR_RUNTIME ||
           instr->value->kind == IR_VALUE_FMT_BOOL ||
           instr->value->kind == IR_VALUE_FMT_HEX_U32 ||
           instr->value->kind == IR_VALUE_FMT_I32 ||
           instr->value->kind == IR_VALUE_FMT_U32 ||
           instr->value->kind == IR_VALUE_FMT_USIZE ||
           instr->value->kind == IR_VALUE_JSON_FIELD ||
           instr->value->kind == IR_VALUE_JSON_STRING_DECODE ||
           instr->value->kind == IR_VALUE_JSON_STRING_FIELD ||
           instr->value->kind == IR_VALUE_JSON_WRITE_STRING ||
           instr->value->kind == IR_VALUE_JSON_WRITE_RUNTIME ||
           instr->value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
           instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH) && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
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
      if ((instr->value->kind == IR_VALUE_CALL ||
           instr->value->kind == IR_VALUE_ASCII_RUNTIME ||
           instr->value->kind == IR_VALUE_TEXT_RUNTIME ||
           instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
           instr->value->kind == IR_VALUE_PARSE_I32 ||
           instr->value->kind == IR_VALUE_PARSE_U32 ||
           instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ||
           instr->value->kind == IR_VALUE_MATH_RUNTIME ||
           instr->value->kind == IR_VALUE_RAND_NEXT_BELOW ||
           instr->value->kind == IR_VALUE_RAND_RANGE_U32) && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
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
    ZDirectLoopFrame frame = {.continue_target = loop_start};
    ZDirectLoopFrame *parent = ctx->loop;
    ctx->loop = &frame;
    bool body_ok = a64_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, ctx, diag);
    ctx->loop = parent;
    if (!body_ok) {
      free(frame.break_patches);
      return false;
    }
    size_t loop_patch = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_branch26(text, loop_patch, loop_start);
    z_aarch64_patch_cond19(text, false_patch, text->len);
    for (size_t i = 0; i < frame.break_len; i++) z_aarch64_patch_branch26(text, frame.break_patches[i], text->len);
    free(frame.break_patches);
    return true;
  }
  if (instr->kind == IR_INSTR_BREAK || instr->kind == IR_INSTR_CONTINUE) {
    if (!ctx->loop) return a64_diag(diag, "direct AArch64 break or continue requires an enclosing loop", instr->line, instr->column, instr->kind == IR_INSTR_BREAK ? "break" : "continue");
    size_t patch = z_aarch64_emit_b_placeholder(text);
    if (instr->kind == IR_INSTR_CONTINUE) z_aarch64_patch_branch26(text, patch, ctx->loop->continue_target);
    else if (!z_direct_loop_frame_add_break(ctx->loop, patch)) return a64_diag(diag, "direct AArch64 break patch list allocation failed", instr->line, instr->column, "out of memory");
    return true;
  }
  return a64_diag(diag, "direct AArch64 instruction kind is unsupported", instr->line, instr->column, "unsupported instruction");
}

// Register-only fill loop replacing an unrolled run of constant-index,
// constant-value array stores. x9 carries the running element pointer, x8 the
// running index, x11 the count, x10 the fill value.
static void a64_emit_fill_run(ZBuf *text, const IrFunction *fun, const ZDirectFillRun *run, unsigned frame_size) {
  unsigned elem_size = 1u << a64_type_index_shift(run->element_type);
  z_aarch64_emit_add_x_sp_imm(text, 9, a64_local_slot_offset(fun, run->array_index, 0, frame_size));
  z_aarch64_emit_movz_x(text, 8, 0);
  z_aarch64_emit_movz_x(text, 11, run->count);
  z_aarch64_emit_movz_x(text, 10, run->fill_value);
  size_t loop = text->len;
  a64_emit_store_ptr_element(text, 10, 9, run->element_type);
  z_aarch64_emit_add_x_imm(text, 9, 9, elem_size);
  z_aarch64_emit_add_x_imm(text, 8, 8, 1);
  z_aarch64_emit_cmp_x(text, 8, 11);
  size_t back = z_aarch64_emit_b_cond_placeholder(text, 1); // b.ne -> loop
  z_aarch64_patch_cond19(text, back, loop);
}

#define A64_FILL_RUN_MIN 8u

static bool a64_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, ZAArch64DirectContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    ZDirectFillRun run;
    if (z_direct_detect_fill_run(fun, instrs, len, i, A64_FILL_RUN_MIN, &run)) {
      a64_emit_fill_run(text, fun, &run, frame_size);
      i += run.count - 1;
      continue;
    }
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
    if (local->is_record || local->is_record_ref) continue;
    if (local->is_array && (local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_BOOL || local->element_type == IR_TYPE_U16 ||
                            local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE ||
                            a64_type_is_scalar64(local->element_type))) continue;
    if (local->is_array || !a64_type_is_scalar(local->type)) {
      return a64_diag(diag, "direct AArch64 backend supports only primitive scalar locals, record locals, and fixed byte/integer arrays", local->line, local->column, local->name);
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

void z_aarch64_direct_append_trap_messages(ZBuf *rodata, unsigned base_offset, ZDirectTrapMessages *messages) {
  for (unsigned kind = 0; kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    const char *text = z_direct_trap_message((ZDirectTrapKind)kind);
    size_t len = strlen(text);
    messages->offsets[kind] = base_offset + (unsigned)rodata->len;
    messages->lens[kind] = (unsigned)len;
    for (size_t i = 0; i < len; i++) zbuf_append_char(rodata, text[i]);
  }
}
