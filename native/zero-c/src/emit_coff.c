#include "zero.h"
#include "coff_emit_state.h"
#include "coff_format.h"
#include "direct_emit.h"
#include "x64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimum constant-fill run length that justifies a fill loop.
#define COFF_FILL_RUN_MIN 8u

static bool coff_diag(ZDiag *diag, const char *message) {
  if (diag) {
    diag->code = 4004;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct MIR subset");
    snprintf(diag->actual, sizeof(diag->actual), "unsupported feature");
    snprintf(diag->help, sizeof(diag->help), "reduce the program to primitive direct-backend constructs or choose a supported direct target");
  }
  return false;
}

static bool coff_diag_at(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct COFF x64 object MVP subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported feature");
    snprintf(diag->help, sizeof(diag->help), "reduce the program to primitive direct-backend constructs or choose a supported direct target");
  }
  return false;
}

static bool coff_type_is_scalar32(IrTypeKind type) { return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32; }

static bool coff_type_is_i64(IrTypeKind type) { return type == IR_TYPE_I64 || type == IR_TYPE_USIZE || type == IR_TYPE_U64; }

static bool coff_type_is_unsigned(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE || type == IR_TYPE_U64;
}

static IrTypeKind coff_view_element_type(const IrValue *view) {
  return view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8;
}

static unsigned coff_type_byte_size(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 1;
  if (type == IR_TYPE_U16) return 2;
  if (coff_type_is_i64(type)) return 8;
  return 4;
}

static bool coff_type_is_array_element(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_U32 || type == IR_TYPE_I32 || type == IR_TYPE_USIZE || coff_type_is_i64(type);
}

static bool coff_type_is_word_array_element(IrTypeKind type) {
  return type == IR_TYPE_U32 || type == IR_TYPE_I32;
}

static void coff_emit_load_ptr_element(ZBuf *text, unsigned dst_reg, unsigned base_reg, IrTypeKind element_type) {
  if (element_type == IR_TYPE_BOOL || element_type == IR_TYPE_U8) z_x64_emit_movzx_reg32_ptr_reg_u8(text, dst_reg, base_reg);
  else if (element_type == IR_TYPE_U16) z_x64_emit_movzx_reg32_ptr_reg_disp_u16(text, dst_reg, base_reg, 0);
  else if (coff_type_is_i64(element_type)) z_x64_emit_load_reg_ptr_reg(text, dst_reg, base_reg, true);
  else z_x64_emit_load_reg_ptr_reg(text, dst_reg, base_reg, false);
}

static void coff_emit_store_ptr_element(ZBuf *text, unsigned base_reg, unsigned src_reg, IrTypeKind element_type) {
  if (element_type == IR_TYPE_BOOL || element_type == IR_TYPE_U8) z_x64_emit_store_ptr_reg8_from_reg(text, base_reg, src_reg);
  else if (element_type == IR_TYPE_U16) z_x64_emit_store_ptr_reg16_from_reg(text, base_reg, src_reg);
  else if (coff_type_is_i64(element_type)) z_x64_emit_store_ptr_reg_from_reg(text, base_reg, src_reg, true);
  else z_x64_emit_store_ptr_reg_from_reg(text, base_reg, src_reg, false);
}

static void coff_emit_scale_index_into_rax(ZBuf *text, IrTypeKind element_type) {
  z_x64_emit_lea_base_index_scale_disp_reg(text, 0, 0, 1, coff_type_byte_size(element_type), 0);
}

static void coff_emit_scale_len_reg(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  unsigned size = coff_type_byte_size(element_type);
  if (size > 1) z_x64_emit_shl_reg_imm8(text, reg, size == 8 ? 3 : (size == 4 ? 2 : 1), true);
}

static void coff_emit_cast_normalize_rax(ZBuf *text, IrTypeKind target) {
  switch (target) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8:
      z_x64_emit_and_reg_u32(text, 0, 0xff, false);
      return;
    case IR_TYPE_U16:
      z_x64_emit_and_reg_u32(text, 0, 0xffff, false);
      return;
    case IR_TYPE_I32:
    case IR_TYPE_U32:
      z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
      return;
    default:
      return;
  }
}

static unsigned coff_local_offset(const IrFunction *fun, unsigned local_index) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0) return fun->locals[local_index].frame_offset;
  return (local_index + 1) * 8;
}

static unsigned coff_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset) { unsigned offset = coff_local_offset(fun, local_index); return offset >= slot_offset ? offset - slot_offset : offset; }

static void coff_emit_load_local_scalar_rax(ZBuf *text, const IrFunction *fun, unsigned local_index, IrTypeKind type) { z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_offset(fun, local_index), coff_type_is_i64(type)); }

static void coff_emit_load_local_slot_rax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) { z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), true); }

static void coff_emit_load_local_slot_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) { z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), false); }

static void coff_emit_load_local_slot_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned reg, bool wide) { z_x64_emit_rbp_disp_reg(text, 0x8b, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide); }

static void coff_emit_store_local_scalar_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg, IrTypeKind type) { z_x64_emit_rbp_disp_reg(text, 0x89, reg, coff_local_offset(fun, local_index), coff_type_is_i64(type)); }

static void coff_emit_store_local_slot_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg, unsigned slot_offset, bool wide) { z_x64_emit_rbp_disp_reg(text, 0x89, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide); }

static void coff_emit_load_field_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_append_u8(text, 0x0f);
    z_x64_emit_rbp_disp_reg(text, 0xb6, 0, offset, false);
  } else if (type == IR_TYPE_U16) {
    z_x64_append_u8(text, 0x0f);
    z_x64_emit_rbp_disp_reg(text, 0xb7, 0, offset, false);
  } else if (coff_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(text, 0x8b, 0, offset, true);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x8b, 0, offset, false);
  }
}

static void coff_emit_store_field_from_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_emit_rbp_disp_reg(text, 0x88, 0, offset, false);
  } else if (type == IR_TYPE_U16) {
    z_x64_append_u8(text, 0x66);
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, false);
  } else if (coff_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, true);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, false);
  }
}

static void coff_emit_array_base_rdx(ZBuf *text, const IrFunction *fun, unsigned local_index) { z_x64_emit_rbp_disp_reg(text, 0x8d, 2, coff_local_offset(fun, local_index), true); }

static bool coff_emit_trap(ZBuf *text, CoffEmitContext *ctx, ZDiag *diag, ZDirectTrapKind kind);

static bool coff_emit_u8_array_bounds_check(ZBuf *text, const IrLocal *local, CoffEmitContext *ctx, ZDiag *diag) {
  z_x64_append_u8(text, 0x3d);
  z_x64_append_u32(text, local ? local->array_len : 0);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  if (!coff_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_INDEX_BOUNDS)) return false;
  z_x64_patch_rel32(text, ok_patch, text->len);
  return true;
}

static void coff_emit_epilogue(ZBuf *text) { z_x64_emit_epilogue(text); }

static unsigned coff_setcc_opcode(IrCompareOp op, bool uns) {
  switch (op) {
    case IR_CMP_EQ: return 0x94;
    case IR_CMP_NE: return 0x95;
    case IR_CMP_LT: return uns ? 0x92 : 0x9c;
    case IR_CMP_LE: return uns ? 0x96 : 0x9e;
    case IR_CMP_GT: return uns ? 0x97 : 0x9f;
    case IR_CMP_GE: return uns ? 0x93 : 0x9d;
  }
  return 0x94;
}

static bool coff_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static bool coff_readonly_data_byte(const IrProgram *program, unsigned offset, unsigned char *out) {
  if (!program) return false;
  for (size_t i = 0; i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    if (offset >= segment->offset && offset < segment->offset + segment->len) {
      if (out) *out = segment->bytes[offset - segment->offset];
      return true;
    }
  }
  return false;
}

static bool coff_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!coff_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !coff_const_u32_value(view->index, &start)) return false;
    if (view->right && !coff_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static bool coff_byte_view_const_byte(const IrProgram *program, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    return coff_readonly_data_byte(program, view->data_offset + index, out);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned len = 0;
    unsigned start = 0;
    if (!coff_byte_view_const_len(view, &len) || index >= len) return false;
    if (view->index && !coff_const_u32_value(view->index, &start)) return false;
    return coff_byte_view_const_byte(program, view->left, start + index, out);
  }
  return false;
}

static bool coff_emit_rodata_ptr_rax(ZBuf *text, unsigned data_offset, CoffEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  uint64_t addend = data_offset - (ctx ? ctx->rodata_base_offset : 0);
  size_t patch = z_x64_emit_mov_rax_u64_patchable(text, addend);
  return z_coff_record_rodata_patch(ctx, patch, data_offset, value, diag);
}

static bool coff_emit_trap(ZBuf *text, CoffEmitContext *ctx, ZDiag *diag, ZDirectTrapKind kind) {
  /* Cold path: branch to the shared per-binary trap stub that prints a diagnostic. */
  if (ctx && ctx->trap_messages.lens[kind] > 0) {
    size_t patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    if (z_direct_trap_branches_record(&ctx->trap_branches[kind], patch)) return true;
    return coff_diag_at(diag, "direct COFF backend ran out of memory while recording a trap branch", 1, 1, "allocation failed");
  }
  z_x64_emit_ud2(text);
  return true;
}

static bool coff_emit_trap_stubs(ZBuf *text, CoffEmitContext *ctx, bool *emitted, ZDiag *diag) {
  if (emitted) *emitted = false;
  for (unsigned kind = 0; ctx && kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    ZDirectTrapBranchList *branches = &ctx->trap_branches[kind];
    if (branches->len == 0) continue;
    if (emitted) *emitted = true;
    size_t stub_offset = text->len;
    if (!coff_emit_rodata_ptr_rax(text, ctx->trap_messages.offsets[kind], ctx, NULL, diag)) return false;
    z_x64_emit_mov_reg_from_reg(text, 2, 0, true);
    z_x64_emit_mov_reg_u32(text, 8, ctx->trap_messages.lens[kind]);
    z_x64_emit_mov_reg_u32(text, 1, 2u);
    z_x64_emit_align_rsp_16(text);
    z_x64_emit_sub_rsp(text, 32);
    size_t patch = z_x64_emit_call32_placeholder(text);
    z_x64_emit_add_rsp(text, 32);
    if (!z_coff_record_instr_runtime_patch(ctx, COFF_RUNTIME_WORLD_WRITE, patch, NULL, diag)) return false;
    z_x64_emit_ud2(text);
    for (size_t i = 0; i < branches->len; i++) z_x64_patch_rel32(text, branches->items[i], stub_offset);
    branches->len = 0;
  }
  return true;
}

static bool coff_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag);
static bool coff_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);
static bool coff_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);
static bool coff_emit_byte_view_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, CoffEmitContext *ctx, ZDiag *diag);

static bool coff_emit_u64_upper_bound_check(ZBuf *text, unsigned value_reg, unsigned limit_reg, CoffEmitContext *ctx, ZDiag *diag) {
  z_x64_emit_cmp_reg_reg(text, value_reg, limit_reg, true);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x86);
  if (!coff_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_VALUE_BOUNDS)) return false;
  z_x64_patch_rel32(text, ok_patch, text->len);
  return true;
}

static bool coff_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag) {
  unsigned len = 0;
  if (coff_byte_view_const_len(view, &len)) {
    z_x64_emit_mov_eax_u32(text, len);
    return true;
  }
  if (view && view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    coff_emit_load_local_slot_eax(text, fun, view->local_index, 8);
    return true;
  }
  if (view && view->kind == IR_VALUE_VEC_BYTES && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_VEC) {
    coff_emit_load_local_slot_eax(text, fun, view->local_index, 8);
    return true;
  }
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    coff_emit_load_local_slot_eax(text, fun, view->local_index, 16);
    return true;
  }
  if (view && view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->local_index];
    if (local->is_record_ref) {
      coff_emit_load_local_slot_rax(text, fun, view->local_index, 0);
      z_x64_emit_add_rax_u32(text, view->field_offset + 8u, true);
      coff_emit_load_ptr_element(text, 0, 0, IR_TYPE_U64);
      return true;
    }
    if (!local->is_record) return coff_diag_at(diag, "direct COFF byte-view field load requires record local", view->line, view->column, "non-record local");
    coff_emit_load_field_eax(text, fun, view->local_index, view->field_offset + 8u, IR_TYPE_U64);
    return true;
  }
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!coff_emit_value(text, fun, view, ctx, diag)) return false;
    z_x64_emit_mov_reg_from_reg(text, 0, 2, true);
    return true;
  }
  if (view && view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) {
    if (!coff_emit_value(text, fun, view, ctx, diag)) return false;
    z_x64_emit_mov_reg_from_reg(text, 0, 2, true);
    return true;
  }
  if (view && view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    return coff_emit_byte_view_pair(text, fun, view, 8, 0, ctx, diag);
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE) {
    return coff_emit_byte_view_pair(text, fun, view, 8, 0, ctx, diag);
  }
  (void)ctx;
  return coff_diag_at(diag, "direct COFF byte-view length currently requires a literal, constant slice, or byte-view local", view ? view->line : 1, view ? view->column : 1, "unsupported byte view length");
}

static bool coff_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag) {
  if (!view) return coff_diag_at(diag, "direct COFF byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    coff_emit_load_local_slot_rax(text, fun, view->local_index, 0);
    return true;
  }
  if (view->kind == IR_VALUE_VEC_BYTES && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_VEC) {
    coff_emit_load_local_slot_rax(text, fun, view->local_index, 0);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    coff_emit_load_local_slot_rax(text, fun, view->local_index, 8);
    return true;
  }
  if (view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->local_index];
    if (local->is_record_ref) {
      coff_emit_load_local_slot_rax(text, fun, view->local_index, 0);
      if (view->field_offset > 0) z_x64_emit_add_rax_u32(text, view->field_offset, true);
      coff_emit_load_ptr_element(text, 0, 0, IR_TYPE_U64);
      return true;
    }
    if (!local->is_record) return coff_diag_at(diag, "direct COFF byte-view field load requires record local", view->line, view->column, "non-record local");
    coff_emit_load_field_eax(text, fun, view->local_index, view->field_offset, IR_TYPE_U64);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return coff_emit_value(text, fun, view, ctx, diag);
  if (view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) return coff_emit_value(text, fun, view, ctx, diag);
  if (view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) return coff_emit_byte_view_pair(text, fun, view, 0, 2, ctx, diag);
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (local->is_record_ref) {
      coff_emit_load_local_slot_rax(text, fun, view->array_index, 0);
      if (view->field_offset > 0) z_x64_emit_add_rax_u32(text, view->field_offset, true);
      return true;
    }
    if (!((local->is_array && view->field_offset == 0) || local->is_record)) return coff_diag_at(diag, "direct COFF byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    z_x64_emit_rbp_disp_reg(text, 0x8d, 0, coff_local_slot_offset(fun, view->array_index, view->field_offset), true);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    return coff_emit_rodata_ptr_rax(text, view->data_offset, ctx, view, diag);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    if (!coff_emit_byte_view_ptr(text, fun, view->left, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (view->index) {
      if (!coff_emit_value(text, fun, view->index, ctx, diag)) return false;
    } else {
      z_x64_emit_mov_eax_u32(text, 0);
    }
    z_x64_emit_mov_rcx_from_rax(text, false);
    z_x64_emit_pop_reg64(text, 0);
    coff_emit_scale_index_into_rax(text, coff_view_element_type(view));
    return true;
  }
  return coff_diag_at(diag, "direct COFF value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static void coff_emit_move_byte_view_pair(ZBuf *text, unsigned ptr_reg, unsigned len_reg, unsigned src_ptr_reg, unsigned src_len_reg) {
  if (ptr_reg == src_len_reg && len_reg == src_ptr_reg) {
    z_x64_emit_push_reg64(text, src_ptr_reg);
    z_x64_emit_mov_reg_from_reg(text, len_reg, src_len_reg, true);
    z_x64_emit_pop_reg64(text, ptr_reg);
    return;
  }
  if (ptr_reg == src_len_reg) {
    if (len_reg != src_len_reg) z_x64_emit_mov_reg_from_reg(text, len_reg, src_len_reg, true);
    if (ptr_reg != src_ptr_reg) z_x64_emit_mov_reg_from_reg(text, ptr_reg, src_ptr_reg, true);
    return;
  }
  if (ptr_reg != src_ptr_reg) z_x64_emit_mov_reg_from_reg(text, ptr_reg, src_ptr_reg, true);
  if (len_reg != src_len_reg) z_x64_emit_mov_reg_from_reg(text, len_reg, src_len_reg, true);
}

static bool coff_emit_json_error_label_arm(ZBuf *text, const IrValue *view, unsigned index, CoffEmitContext *ctx, ZDiag *diag) {
  if (!view || index >= view->arg_len || !view->args[index] || view->args[index]->kind != IR_VALUE_STRING_LITERAL) {
    return coff_diag_at(diag, "direct COFF JSON error label requires string literal arms", view ? view->line : 1, view ? view->column : 1, "invalid JSON error label");
  }
  const IrValue *label = view->args[index];
  if (!coff_emit_rodata_ptr_rax(text, label->data_offset, ctx, label, diag)) return false;
  z_x64_emit_mov_reg_u32(text, 2, label->data_len);
  return true;
}

static bool coff_emit_json_error_label_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, CoffEmitContext *ctx, ZDiag *diag) {
  if (!view || !view->left) return coff_diag_at(diag, "direct COFF JSON error label requires a status code", view ? view->line : 1, view ? view->column : 1, "missing JSON status");
  if (view->arg_len != 4) return coff_diag_at(diag, "direct COFF JSON error label requires four labels", view->line, view->column, "invalid JSON error label");
  if (!coff_emit_value(text, fun, view->left, ctx, diag)) return false;
  z_x64_emit_cmp_reg_i8(text, 0, 0, false);
  size_t code0 = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_cmp_reg_i8(text, 0, 1, false);
  size_t code1 = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_cmp_reg_i8(text, 0, 2, false);
  size_t code2 = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!coff_emit_json_error_label_arm(text, view, 3, ctx, diag)) return false;
  size_t done3 = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, code0, text->len);
  if (!coff_emit_json_error_label_arm(text, view, 0, ctx, diag)) return false;
  size_t done0 = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, code1, text->len);
  if (!coff_emit_json_error_label_arm(text, view, 1, ctx, diag)) return false;
  size_t done1 = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, code2, text->len);
  if (!coff_emit_json_error_label_arm(text, view, 2, ctx, diag)) return false;
  size_t done2 = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, done3, text->len);
  z_x64_patch_rel32(text, done0, text->len);
  z_x64_patch_rel32(text, done1, text->len);
  z_x64_patch_rel32(text, done2, text->len);
  coff_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 2);
  return true;
}

static bool coff_emit_byte_view_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, CoffEmitContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return coff_diag_at(diag, "direct COFF byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!coff_emit_value(text, fun, view, ctx, diag)) return false;
    coff_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 2);
    return true;
  }
  if (view && view->kind == IR_VALUE_STR_RUNTIME && view->type == IR_TYPE_BYTE_VIEW) {
    if (!coff_emit_value(text, fun, view, ctx, diag)) return false;
    coff_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 2);
    return true;
  }
  if (view && view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    return coff_emit_json_error_label_pair(text, fun, view, ptr_reg, len_reg, ctx, diag);
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE) {
    if (!view->index && !view->right) return coff_emit_byte_view_pair(text, fun, view->left, ptr_reg, len_reg, ctx, diag);
    if (!coff_emit_byte_view_pair(text, fun, view->left, 8, 10, ctx, diag)) return false;
    z_x64_emit_push_reg64(text, 8);
    z_x64_emit_push_reg64(text, 10);
    if (view->index) {
      if (!coff_emit_value(text, fun, view->index, ctx, diag)) return false;
    } else {
      z_x64_emit_mov_eax_u32(text, 0);
    }
    z_x64_emit_mov_rcx_from_rax(text, true);
    if (view->right) {
      z_x64_emit_push_reg64(text, 1);
      if (!coff_emit_value(text, fun, view->right, ctx, diag)) return false;
      z_x64_emit_pop_reg64(text, 1);
      z_x64_emit_pop_reg64(text, 10);
      if (!coff_emit_u64_upper_bound_check(text, 1, 0, ctx, diag)) return false;
      if (!coff_emit_u64_upper_bound_check(text, 0, 10, ctx, diag)) return false;
      z_x64_emit_sub_reg_reg(text, 0, 1, true);
    } else {
      z_x64_emit_pop_reg64(text, 10);
      if (!coff_emit_u64_upper_bound_check(text, 1, 10, ctx, diag)) return false;
      z_x64_emit_mov_reg_from_reg(text, 0, 10, true);
      z_x64_emit_sub_reg_reg(text, 0, 1, true);
    }
    z_x64_emit_pop_reg64(text, 8);
    z_x64_emit_lea_base_index_scale_disp_reg(text, 8, 8, 1, coff_type_byte_size(coff_view_element_type(view)), 0);
    coff_emit_move_byte_view_pair(text, ptr_reg, len_reg, 8, 0);
    return true;
  }
  if (!coff_emit_byte_view_ptr(text, fun, view, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!coff_emit_byte_view_len(text, fun, view, ctx, diag)) return false;
  if (len_reg != 0) z_x64_emit_mov_reg_from_reg(text, len_reg, 0, true);
  z_x64_emit_pop_reg64(text, ptr_reg);
  return true;
}

static bool coff_emit_local_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF local index is out of range", value->line, value->column, "invalid local");
  if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) return coff_diag_at(diag, "direct COFF byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
  if (fun->locals[value->local_index].type == IR_TYPE_MAYBE_SCALAR) return coff_diag_at(diag, "direct COFF Maybe scalar local cannot be used as a scalar", value->line, value->column, "Maybe scalar local");
  coff_emit_load_local_scalar_rax(text, fun, value->local_index, fun->locals[value->local_index].type);
  return true;
}

static bool coff_emit_binary_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->binary_op == IR_BIN_AND) {
    if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t left_false = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t right_false = z_x64_emit_jcc32_placeholder(text, 0x84);
    z_x64_emit_mov_eax_u32(text, 1);
    size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, left_false, text->len);
    z_x64_patch_rel32(text, right_false, text->len);
    z_x64_emit_mov_eax_u32(text, 0);
    z_x64_patch_rel32(text, end_patch, text->len);
    return true;
  }
  if (value->binary_op == IR_BIN_OR) {
    if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t eval_right = z_x64_emit_jcc32_placeholder(text, 0x84);
    z_x64_emit_mov_eax_u32(text, 1);
    size_t left_true_end = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, eval_right, text->len);
    if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t right_false = z_x64_emit_jcc32_placeholder(text, 0x84);
    z_x64_emit_mov_eax_u32(text, 1);
    size_t right_true_end = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, right_false, text->len);
    z_x64_emit_mov_eax_u32(text, 0);
    z_x64_patch_rel32(text, left_true_end, text->len);
    z_x64_patch_rel32(text, right_true_end, text->len);
    return true;
  }
  if (value->binary_op != IR_BIN_ADD && value->binary_op != IR_BIN_SUB && value->binary_op != IR_BIN_MUL &&
      value->binary_op != IR_BIN_DIV && value->binary_op != IR_BIN_MOD) {
    return coff_diag_at(diag, "direct COFF binary operator is unsupported", value->line, value->column, "unsupported operator");
  }
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
  bool wide = coff_type_is_i64(value->type);
  z_x64_emit_mov_rcx_from_rax(text, wide);
  z_x64_emit_pop_rax(text);
  if (value->binary_op == IR_BIN_ADD) z_x64_emit_add_rax_rcx(text, wide);
  else if (value->binary_op == IR_BIN_SUB) z_x64_emit_sub_rax_rcx(text, wide);
  else if (value->binary_op == IR_BIN_MUL) z_x64_emit_imul_rax_rcx(text, wide);
  else z_x64_emit_div_rax_rcx(text, wide, coff_type_is_unsigned(value->type), value->binary_op == IR_BIN_MOD);
  return true;
}

static bool coff_emit_compare_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF comparison requires two operands", value->line, value->column, "invalid comparison");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
  bool wide = coff_type_is_i64(value->left ? value->left->type : value->type);
  z_x64_emit_mov_rcx_from_rax(text, wide);
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx_to_bool(text, coff_setcc_opcode(value->compare_op, coff_type_is_unsigned(value->left ? value->left->type : value->type)), wide);
  return true;
}

static bool coff_emit_call_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {1, 2, 8, 9};
  const IrFunction *callee = ctx && ctx->program && !value->external_call && value->callee_index < ctx->program->function_len ? &ctx->program->functions[value->callee_index] : NULL;
  const IrExternalFunction *external = ctx && ctx->program && value->external_call && value->external_index < ctx->program->external_function_len ? &ctx->program->external_functions[value->external_index] : NULL;
  if (!callee && !external) return coff_diag_at(diag, "direct COFF call target is unavailable", value->line, value->column, "invalid callee");
  size_t param_count = external ? external->param_len : callee->param_count;
  size_t abi_slots = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (i >= param_count) return coff_diag_at(diag, "direct COFF call parameter metadata is unavailable", value->line, value->column, "invalid callee parameter");
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    abi_slots += param_type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
  }
  if (abi_slots > 8) return coff_diag_at(diag, "direct COFF call supports at most eight ABI argument slots", value->line, value->column, "too many arguments");
  size_t stack_slots = abi_slots > 4 ? abi_slots - 4 : 0;
  unsigned call_frame = (unsigned)(32u + stack_slots * 8u);
  unsigned temp_base = call_frame;
  unsigned total_stack = (unsigned)z_coff_align(call_frame + abi_slots * 8u, 16);
  z_x64_emit_sub_rsp(text, total_stack);

  size_t abi_slot = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    if (param_type == IR_TYPE_BYTE_VIEW) {
      if (!coff_emit_byte_view_pair(text, fun, arg, 0, 2, ctx, diag)) return false;
      z_x64_emit_store_rsp_offset_reg(text, 0, temp_base + (unsigned)abi_slot * 8u, true);
      z_x64_emit_store_rsp_offset_reg(text, 2, temp_base + (unsigned)(abi_slot + 1u) * 8u, true);
      abi_slot += 2;
      continue;
    }
    if (!coff_emit_value(text, fun, arg, ctx, diag)) return false;
    z_x64_emit_store_rsp_offset_reg(text, 0, temp_base + (unsigned)abi_slot * 8u, true);
    abi_slot++;
  }
  for (size_t slot = 4; slot < abi_slots; slot++) {
    z_x64_emit_load_rsp_offset_reg(text, 0, temp_base + (unsigned)slot * 8u, true);
    z_x64_emit_store_rsp_offset_reg(text, 0, 32u + (unsigned)(slot - 4u) * 8u, true);
  }
  size_t register_slots = abi_slots < 4 ? abi_slots : 4;
  for (size_t slot = 0; slot < register_slots; slot++) {
    z_x64_emit_load_rsp_offset_reg(text, param_regs[slot], temp_base + (unsigned)slot * 8u, true);
  }
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_add_rsp(text, total_stack);
  if (value->external_call) coff_emit_cast_normalize_rax(text, value->type);
  return z_coff_record_call_patch(ctx, patch, value->external_call ? 0 : value->callee_index, value, diag);
}

static bool coff_emit_vec_push_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
  coff_emit_load_local_slot_eax(text, fun, value->local_index, 8);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 12, 1, false);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_mov_eax_u32(text, 0);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_push_rax(text);
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  z_x64_emit_add_rdx_rcx(text, true);
  z_x64_emit_store_ptr_reg8_from_reg(text, 2, 0);
  z_x64_emit_mov_eax_from_ecx(text);
  z_x64_emit_add_reg_i8(text, 0, 1, false);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_vec_clear_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec clear requires a Vec local", value->line, value->column, "invalid Vec local");
  z_x64_emit_mov_eax_u32(text, 0);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
  return true;
}

static bool coff_emit_vec_get_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec get requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec get requires an index", value->line, value->column, "missing Vec index");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_pop_reg64(text, 0);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_mov_eax_u32(text, 0);
  z_x64_emit_mov_reg_from_rax(text, 2, true);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, ok_patch, text->len);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 1, true);
  z_x64_emit_movzx_reg32_base_index_u8(text, 2, 1, 0);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_vec_set_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec set requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec set requires an index", value->line, value->column, "missing Vec index");
  if (!value->right) return coff_diag_at(diag, "direct COFF Vec set requires a value", value->line, value->column, "missing Vec value");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_pop_reg64(text, 0);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_mov_eax_u32(text, 0);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_push_rax(text);
  if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  z_x64_emit_store_base_index_reg8(text, 2, 1, 0);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_vec_pop_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec pop requires a Vec local", value->line, value->column, "invalid Vec local");
  coff_emit_load_local_slot_eax(text, fun, value->local_index, 8);
  z_x64_emit_test_rax_rax(text, false);
  size_t empty_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_add_reg_i8(text, 0, -1, false);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
  z_x64_emit_mov_eax_u32(text, 1);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, empty_patch, text->len);
  z_x64_emit_mov_eax_u32(text, 0);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_vec_truncate_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec truncate requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec truncate requires a length", value->line, value->column, "missing Vec length");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_pop_reg64(text, 0);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t requested_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_mov_eax_from_ecx(text);
  z_x64_patch_rel32(text, requested_patch, text->len);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
  return true;
}

static bool coff_emit_vec_remove_swap_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec swap-remove requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec swap-remove requires an index", value->line, value->column, "missing Vec index");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_pop_reg64(text, 0);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_mov_eax_u32(text, 0);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_add_reg_i8(text, 1, -1, false);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  z_x64_emit_movzx_reg32_base_index_u8(text, 8, 2, 1);
  z_x64_emit_store_base_index_reg8(text, 2, 0, 8);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 1, 8, false);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_vec_lookup_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec lookup requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec lookup requires a value", value->line, value->column, "missing Vec value");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_rax(text, 8, false);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_vec_lookup_loop(text, value->kind == IR_VALUE_VEC_CONTAINS);
  return true;
}

static bool coff_emit_vec_insert_unique_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec insert-unique requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec insert-unique requires a value", value->line, value->column, "missing Vec value");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_rax(text, 8, false);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 12, 9, false);
  z_x64_emit_vec_insert_unique_loop(text);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 1, 8, false);
  return true;
}

static bool coff_emit_vec_remove_value_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec remove-value requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return coff_diag_at(diag, "direct COFF Vec remove-value requires a value", value->line, value->column, "missing Vec value");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_rax(text, 8, false);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
  coff_emit_load_local_slot_reg(text, fun, value->local_index, 8, 1, false);
  z_x64_emit_vec_remove_value_loop(text);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 1, 8, false);
  return true;
}

static bool coff_emit_byte_view_index_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  unsigned const_index = 0;
  unsigned char byte = 0;
  if (coff_view_element_type(value->left) == IR_TYPE_U8 &&
      coff_const_u32_value(value->index, &const_index) &&
      coff_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
    z_x64_emit_mov_eax_u32(text, byte);
    return true;
  }
  if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!coff_emit_byte_view_pair(text, fun, value->left, 8, 1, ctx, diag)) return false;
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  if (!coff_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_INDEX_BOUNDS)) return false;
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  IrTypeKind element_type = coff_view_element_type(value->left);
  coff_emit_scale_index_into_rax(text, element_type);
  coff_emit_load_ptr_element(text, 0, 0, element_type);
  return true;
}

static bool coff_emit_byte_copy_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
  z_x64_emit_push_reg64(text, 7); z_x64_emit_push_reg64(text, 6);
  if (!coff_emit_byte_view_pair(text, fun, value->left, 0, 2, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  z_x64_emit_push_reg64(text, 2);
  if (!coff_emit_byte_view_pair(text, fun, value->right, 7, 0, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  z_x64_emit_pop_reg64(text, 6);
  z_x64_emit_byte_copy_min_loop(text);
  z_x64_emit_pop_reg64(text, 6); z_x64_emit_pop_reg64(text, 7);
  return true;
}

static bool coff_emit_byte_fill_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF byte fill requires a fill byte and destination byte view", value->line, value->column, "missing byte fill input");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text); z_x64_emit_push_reg64(text, 7);
  if (!coff_emit_byte_view_pair(text, fun, value->right, 7, 2, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 11);
  z_x64_emit_pop_reg64(text, 9);
  z_x64_emit_push_reg64(text, 11);
  z_x64_emit_byte_fill_loop(text); z_x64_emit_pop_reg64(text, 7);
  return true;
}

static void coff_emit_item_copy_loop(ZBuf *text, IrTypeKind element_type) {
  // Inputs: rsi=source ptr, rcx=source len, rdi=destination ptr, rdx=destination len. Output: rax=copied item count.
  z_x64_emit_mov_reg_from_reg(text, 0, 2, true);
  z_x64_emit_cmp_rax_rcx(text, true);
  size_t keep_dst_len = z_x64_emit_jcc32_placeholder(text, 0x86);
  z_x64_emit_mov_rax_from_rcx(text);
  z_x64_patch_rel32(text, keep_dst_len, text->len);
  z_x64_emit_mov_rdx_from_rax(text);
  z_x64_emit_xor_r8d_r8d(text);
  size_t loop = text->len;
  z_x64_emit_cmp_reg_reg(text, 2, 8, true);
  size_t done = z_x64_emit_jcc32_placeholder(text, 0x86);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  coff_emit_scale_index_into_rax(text, element_type);
  z_x64_emit_mov_reg_from_reg(text, 11, 6, true);
  z_x64_emit_add_reg_reg(text, 11, 0, true);
  coff_emit_load_ptr_element(text, 10, 11, element_type);
  z_x64_emit_mov_reg_from_reg(text, 11, 7, true);
  z_x64_emit_add_reg_reg(text, 11, 0, true);
  coff_emit_store_ptr_element(text, 11, 10, element_type);
  z_x64_emit_inc_r8(text);
  size_t back = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, back, loop);
  z_x64_patch_rel32(text, done, text->len);
  z_x64_emit_mov_rax_from_rdx(text);
}

static bool coff_emit_item_copy_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF item copy requires source and destination views", value->line, value->column, "missing item view");
  z_x64_emit_push_reg64(text, 7);
  z_x64_emit_push_reg64(text, 6);
  if (!coff_emit_byte_view_pair(text, fun, value->left, 6, 1, ctx, diag)) return false;
  z_x64_emit_push_reg64(text, 6);
  z_x64_emit_push_reg64(text, 1);
  if (!coff_emit_byte_view_pair(text, fun, value->right, 7, 2, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  z_x64_emit_pop_reg64(text, 6);
  coff_emit_item_copy_loop(text, value->element_type == IR_TYPE_UNSUPPORTED ? coff_view_element_type(value->left) : value->element_type);
  z_x64_emit_pop_reg64(text, 6);
  z_x64_emit_pop_reg64(text, 7);
  return true;
}

static void coff_emit_item_fill_loop(ZBuf *text, IrTypeKind element_type) {
  // Inputs: rdi=destination ptr, rdx=len, r10=item value. Output: rax=filled item count.
  z_x64_emit_xor_r8d_r8d(text);
  size_t loop = text->len;
  z_x64_emit_cmp_reg_reg(text, 2, 8, true);
  size_t done = z_x64_emit_jcc32_placeholder(text, 0x86);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  coff_emit_scale_index_into_rax(text, element_type);
  z_x64_emit_mov_reg_from_reg(text, 11, 7, true);
  z_x64_emit_add_reg_reg(text, 11, 0, true);
  coff_emit_store_ptr_element(text, 11, 10, element_type);
  z_x64_emit_inc_r8(text);
  size_t back = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, back, loop);
  z_x64_patch_rel32(text, done, text->len);
  z_x64_emit_mov_rax_from_rdx(text);
}

static bool coff_emit_item_fill_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF item fill requires a value and destination view", value->line, value->column, "missing item fill input");
  if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_reg(text, 10, 0, true);
  z_x64_emit_push_reg64(text, 7);
  z_x64_emit_push_reg64(text, 10);
  if (!coff_emit_byte_view_pair(text, fun, value->right, 7, 2, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 10);
  coff_emit_item_fill_loop(text, value->element_type == IR_TYPE_UNSUPPORTED ? coff_view_element_type(value->right) : value->element_type);
  z_x64_emit_pop_reg64(text, 7);
  return true;
}

static bool coff_emit_item_contains_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF item contains requires an input view and needle", value->line, value->column, "missing item contains input");
  IrTypeKind element_type = value->element_type == IR_TYPE_UNSUPPORTED ? coff_view_element_type(value->left) : value->element_type;
  if (!coff_emit_byte_view_pair(text, fun, value->left, 6, 2, ctx, diag)) return false;
  z_x64_emit_push_reg64(text, 6);
  z_x64_emit_push_reg64(text, 2);
  if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_reg(text, 10, 0, true);
  z_x64_emit_pop_reg64(text, 2);
  z_x64_emit_pop_reg64(text, 6);
  z_x64_emit_xor_r8d_r8d(text);
  size_t loop = text->len;
  z_x64_emit_cmp_reg_reg(text, 2, 8, true);
  size_t done_without_match = z_x64_emit_jcc32_placeholder(text, 0x86);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  coff_emit_scale_index_into_rax(text, element_type);
  z_x64_emit_mov_reg_from_reg(text, 11, 6, true);
  z_x64_emit_add_reg_reg(text, 11, 0, true);
  coff_emit_load_ptr_element(text, 9, 11, element_type);
  z_x64_emit_cmp_reg_reg(text, 9, 10, coff_type_is_i64(element_type));
  size_t found = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_inc_r8(text);
  size_t back = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, back, loop);
  z_x64_patch_rel32(text, done_without_match, text->len);
  z_x64_emit_mov_reg_u32(text, 0, 0);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, found, text->len);
  z_x64_emit_mov_reg_u32(text, 0, 1);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool coff_emit_byte_view_eq_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF byte-view equality requires two byte views", value->line, value->column, "missing byte view");
  if (!coff_emit_byte_view_pair(text, fun, value->left, 8, 10, ctx, diag)) return false;
  z_x64_emit_push_reg64(text, 8);
  z_x64_emit_push_reg64(text, 10);
  if (!coff_emit_byte_view_pair(text, fun, value->right, 9, 0, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 10);
  z_x64_emit_cmp_reg_reg(text, 10, 0, false);
  size_t same_len = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_pop_reg64(text, 8);
  z_x64_emit_mov_eax_u32(text, 0);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, same_len, text->len);
  z_x64_emit_pop_reg64(text, 8);
  coff_emit_scale_len_reg(text, 10, coff_view_element_type(value->left));
  z_x64_emit_byte_eq_loop(text);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool coff_emit_crc32_bytes_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value->left) return coff_diag_at(diag, "direct COFF CRC32 requires a byte view", value->line, value->column, "missing byte view");
  if (!coff_emit_byte_view_pair(text, fun, value->left, 11, 9, ctx, diag)) return false;
  z_x64_emit_crc32_bytes_loop(text, 11, 9);
  return true;
}

static bool coff_emit_index_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value->array_index >= fun->local_len) return coff_diag_at(diag, "direct COFF indexed load array is out of range", value->line, value->column, "invalid array local");
  const IrLocal *local = &fun->locals[value->array_index];
  unsigned const_index = 0;
  if (local->is_array && coff_type_is_word_array_element(local->element_type) && coff_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
    coff_emit_load_local_slot_eax(text, fun, value->array_index, const_index * 4u);
    return true;
  }
  if (local->is_array && coff_type_is_array_element(local->element_type)) {
    if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
    if (!coff_emit_u8_array_bounds_check(text, local, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    coff_emit_array_base_rdx(text, fun, value->array_index);
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_lea_base_index_scale_disp_reg(text, 2, 2, 1, coff_type_byte_size(local->element_type), 0);
    coff_emit_load_ptr_element(text, 0, 2, local->element_type);
    return true;
  }
  return coff_diag_at(diag, "direct COFF indexed load requires [N]u8 or integer arrays", value->line, value->column, "unsupported array local");
}

static bool coff_emit_field_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF field load record is out of range", value->line, value->column, "invalid record local");
  if (fun->locals[value->local_index].is_record_ref) {
    coff_emit_load_local_slot_rax(text, fun, value->local_index, 0);
    if (value->field_offset > 0) z_x64_emit_add_rax_u32(text, value->field_offset, true);
    coff_emit_load_ptr_element(text, 0, 0, value->type);
    return true;
  }
  if (!fun->locals[value->local_index].is_record) return coff_diag_at(diag, "direct COFF field load requires record local", value->line, value->column, "non-record local");
  coff_emit_load_field_eax(text, fun, value->local_index, value->field_offset, value->type); return true;
}

static bool coff_emit_record_addr_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF record address local is out of range", value->line, value->column, "invalid record local");
  if (!fun->locals[value->local_index].is_record) return coff_diag_at(diag, "direct COFF record address requires record local", value->line, value->column, "non-record local");
  z_x64_emit_rbp_disp_reg(text, 0x8d, 0, coff_local_offset(fun, value->local_index), true);
  return true;
}

static CoffRuntimeHelper coff_str_runtime_helper(IrStrOp op) {
  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
      return COFF_RUNTIME_STR_BUFFER_OP;
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      return COFF_RUNTIME_CRYPTO_DIGEST;
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
      return COFF_RUNTIME_CRYPTO_HMAC_SHA256;
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      return COFF_RUNTIME_CRYPTO_HMAC_SHA256_HEX;
    case IR_STR_OP_CONCAT:
      return COFF_RUNTIME_STR_CONCAT;
    case IR_STR_OP_REPEAT:
      return COFF_RUNTIME_STR_REPEAT;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      return COFF_RUNTIME_STR_TRIM_OP;
    case IR_STR_OP_COUNT_BYTE:
      return COFF_RUNTIME_STR_COUNT_BYTE;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      return COFF_RUNTIME_STR_PAIR_OP;
    case IR_STR_OP_WORD_COUNT_ASCII:
      return COFF_RUNTIME_STR_WORD_COUNT_ASCII;
  }
  return COFF_RUNTIME_HELPER_COUNT;
}

static uint32_t coff_crypto_digest_op(IrStrOp op) {
  return op == IR_STR_OP_CRYPTO_SHA256_HEX ? 1u : 0u;
}

static void coff_emit_runtime_call_begin_with_temps(ZBuf *text, unsigned abi_slots, unsigned temp_slots, unsigned *temp_base, unsigned *total_stack) {
  unsigned stack_slots = abi_slots > 4u ? abi_slots - 4u : 0u;
  unsigned call_frame = 32u + stack_slots * 8u;
  *temp_base = call_frame;
  *total_stack = (unsigned)z_coff_align(call_frame + temp_slots * 8u, 16);
  z_x64_emit_sub_rsp(text, *total_stack);
}

static void coff_emit_runtime_call_begin(ZBuf *text, unsigned abi_slots, unsigned *temp_base, unsigned *total_stack) {
  coff_emit_runtime_call_begin_with_temps(text, abi_slots, abi_slots, temp_base, total_stack);
}

static void coff_emit_runtime_temp_slot_store(ZBuf *text, unsigned temp_base, unsigned slot, unsigned reg) {
  z_x64_emit_store_rsp_offset_reg(text, reg, temp_base + slot * 8u, true);
}

static void coff_emit_runtime_temp_slot_load(ZBuf *text, unsigned temp_base, unsigned slot, unsigned reg) {
  z_x64_emit_load_rsp_offset_reg(text, reg, temp_base + slot * 8u, true);
}

static bool coff_emit_runtime_arg_byte_view(ZBuf *text, const IrFunction *fun, const IrValue *arg, unsigned temp_base, unsigned *slot, CoffEmitContext *ctx, ZDiag *diag) {
  if (!coff_emit_byte_view_pair(text, fun, arg, 0, 2, ctx, diag)) return false;
  coff_emit_runtime_temp_slot_store(text, temp_base, *slot, 0);
  coff_emit_runtime_temp_slot_store(text, temp_base, *slot + 1u, 2);
  *slot += 2u;
  return true;
}

static bool coff_emit_runtime_arg_value(ZBuf *text, const IrFunction *fun, const IrValue *arg, unsigned temp_base, unsigned *slot, CoffEmitContext *ctx, ZDiag *diag) {
  if (!coff_emit_value(text, fun, arg, ctx, diag)) return false;
  coff_emit_runtime_temp_slot_store(text, temp_base, *slot, 0);
  *slot += 1u;
  return true;
}

static void coff_emit_runtime_arg_u32(ZBuf *text, uint32_t value, unsigned temp_base, unsigned *slot) {
  z_x64_emit_mov_reg_u32(text, 0, value);
  coff_emit_runtime_temp_slot_store(text, temp_base, *slot, 0);
  *slot += 1u;
}

static void coff_emit_runtime_arg_stack_ptr(ZBuf *text, unsigned temp_base, unsigned pointer_slot, unsigned *slot) {
  z_x64_emit_lea_rsp_offset_reg(text, 0, temp_base + pointer_slot * 8u);
  coff_emit_runtime_temp_slot_store(text, temp_base, *slot, 0);
  *slot += 1u;
}

static bool coff_emit_runtime_call(ZBuf *text, CoffEmitContext *ctx, CoffRuntimeHelper helper, unsigned abi_slots, unsigned temp_base, const IrValue *value, ZDiag *diag) {
  static const unsigned param_regs[] = {1, 2, 8, 9};
  for (unsigned slot = 4; slot < abi_slots; slot++) {
    coff_emit_runtime_temp_slot_load(text, temp_base, slot, 0);
    z_x64_emit_store_rsp_offset_reg(text, 0, 32u + (slot - 4u) * 8u, true);
  }
  unsigned register_slots = abi_slots < 4u ? abi_slots : 4u;
  for (unsigned slot = 0; slot < register_slots; slot++) {
    coff_emit_runtime_temp_slot_load(text, temp_base, slot, param_regs[slot]);
  }
  size_t patch = z_x64_emit_call32_placeholder(text);
  return z_coff_record_value_runtime_patch(ctx, helper, patch, value, diag);
}

static void coff_emit_encoded_len_to_maybe_byte_view_regs(ZBuf *text) {
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_test_rax_rax(text, true);
  size_t none = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_add_reg_i8(text, 1, -1, true);
  z_x64_patch_rel32(text, none, text->len);
  z_x64_emit_test_rax_rax(text, true);
  z_x64_emit_setcc_al_to_bool(text, 0x95);
}

static bool coff_emit_str_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value) return coff_diag_at(diag, "direct COFF std.str helper requires an operation", 1, 1, "missing std.str helper");
  IrStrOp op = (IrStrOp)value->int_value;
  CoffRuntimeHelper helper = coff_str_runtime_helper(op);
  if (helper == COFF_RUNTIME_HELPER_COUNT) return coff_diag_at(diag, "direct COFF std.str runtime helper is unsupported", value->line, value->column, "unsupported std.str op");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      if (value->arg_len != 2) return coff_diag_at(diag, "direct COFF std.str buffer helper requires two arguments", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 5, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
      coff_emit_runtime_arg_u32(text, helper == COFF_RUNTIME_CRYPTO_DIGEST ? coff_crypto_digest_op(op) : (uint32_t)op, temp_base, &slot);
      if (!coff_emit_runtime_call(text, ctx, helper, 5, temp_base, value, diag)) return false;
      coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
      z_x64_emit_add_rsp(text, total_stack);
      coff_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_CONCAT:
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      if (value->arg_len != 3) return coff_diag_at(diag, "direct COFF std.str three-view helper requires three arguments", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 6, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[2], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_call(text, ctx, helper, 6, temp_base, value, diag)) return false;
      coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
      z_x64_emit_add_rsp(text, total_stack);
      coff_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_REPEAT:
      if (value->arg_len != 3) return coff_diag_at(diag, "direct COFF std.str.repeat requires three arguments", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 5, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_value(text, fun, value->args[2], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_call(text, ctx, helper, 5, temp_base, value, diag)) return false;
      coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
      z_x64_emit_add_rsp(text, total_stack);
      coff_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      if (value->arg_len != 1) return coff_diag_at(diag, "direct COFF std.str borrowed-slice helper requires one argument", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 3, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      coff_emit_runtime_arg_u32(text, (uint32_t)op, temp_base, &slot);
      if (!coff_emit_runtime_call(text, ctx, helper, 3, temp_base, value, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(text, true);
      z_x64_emit_shr_reg_imm8(text, 1, 32, true);
      z_x64_emit_mov_reg_from_reg(text, 2, 0, false);
      coff_emit_runtime_temp_slot_load(text, temp_base, 0, 0);
      z_x64_emit_add_rsp(text, total_stack);
      z_x64_emit_add_reg_reg(text, 0, 1, true);
      return true;
    case IR_STR_OP_COUNT_BYTE:
      if (value->arg_len != 2) return coff_diag_at(diag, "direct COFF std.str.countByte requires two arguments", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 3, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_value(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_call(text, ctx, helper, 3, temp_base, value, diag)) return false;
      z_x64_emit_add_rsp(text, total_stack);
      return true;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      if (value->arg_len != 2) return coff_diag_at(diag, "direct COFF std.str pair helper requires two arguments", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 5, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
      coff_emit_runtime_arg_u32(text, (uint32_t)op, temp_base, &slot);
      if (!coff_emit_runtime_call(text, ctx, helper, 5, temp_base, value, diag)) return false;
      z_x64_emit_add_rsp(text, total_stack);
      if (value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
      return true;
    case IR_STR_OP_WORD_COUNT_ASCII:
      if (value->arg_len != 1) return coff_diag_at(diag, "direct COFF std.str.wordCountAscii requires one argument", value->line, value->column, "invalid std.str arity");
      coff_emit_runtime_call_begin(text, 2, &temp_base, &total_stack);
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
      if (!coff_emit_runtime_call(text, ctx, helper, 2, temp_base, value, diag)) return false;
      z_x64_emit_add_rsp(text, total_stack);
      return true;
    default:
      return coff_diag_at(diag, "direct COFF std.str runtime helper is unsupported", value->line, value->column, "unsupported std.str op");
  }
}

static void coff_emit_normalize_u32_maybe_result(ZBuf *text) {
  z_x64_emit_mov_reg_from_reg(text, 2, 0, true);
  z_x64_emit_mov_reg_from_reg(text, 2, 2, false);
  z_x64_emit_shr_reg_imm8(text, 0, 32, true);
}

static void coff_emit_normalize_ascii_maybe_result(ZBuf *text) {
  z_x64_emit_mov_reg_from_reg(text, 8, 0, true);
  z_x64_emit_mov_reg_from_reg(text, 2, 8, false);
  z_x64_emit_and_reg_u32(text, 2, 0xffu, false);
  z_x64_emit_test_rax_rax(text, false);
  z_x64_emit_setcc_al_to_bool(text, 0x95);
}

static void coff_emit_normalize_len_plus_one_maybe_result(ZBuf *text) {
  z_x64_emit_mov_reg_from_reg(text, 8, 0, true);
  z_x64_emit_mov_reg_from_reg(text, 2, 8, true);
  z_x64_emit_mov_reg_u32(text, 1, 1);
  z_x64_emit_sub_reg_reg(text, 2, 1, true);
  z_x64_emit_test_reg_reg(text, 8, true);
  z_x64_emit_setcc_al_to_bool(text, 0x95);
}

static bool coff_emit_ascii_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return coff_diag_at(diag, "direct COFF std.ascii helper requires one byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.ascii arity");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 2, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_value(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_ASCII_OP, 2, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_MAYBE_SCALAR) coff_emit_normalize_ascii_maybe_result(text);
  else if (value->type == IR_TYPE_BOOL || value->type == IR_TYPE_U8) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_text_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return coff_diag_at(diag, "direct COFF std.text helper requires one byte-view argument", value ? value->line : 1, value ? value->column : 1, "invalid std.text arity");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 3, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_TEXT_OP, 3, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_MAYBE_SCALAR) coff_emit_normalize_len_plus_one_maybe_result(text);
  else if (value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_parse_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 1 || value->arg_len > 2) return coff_diag_at(diag, "direct COFF std.parse helper requires one byte-view argument and optional byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.parse arity");
  bool maybe_usize = value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE;
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  if (maybe_usize) {
    const unsigned abi_slots = 3;
    const unsigned result_slot = 3;
    coff_emit_runtime_call_begin_with_temps(text, abi_slots, result_slot + 2u, &temp_base, &total_stack);
    coff_emit_runtime_arg_stack_ptr(text, temp_base, result_slot, &slot);
    if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
    if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_PARSE_USIZE, abi_slots, temp_base, value, diag)) return false;
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot, 0);
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot + 1u, 2);
    z_x64_emit_add_rsp(text, total_stack);
    return true;
  }
  coff_emit_runtime_call_begin(text, 4, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
  if (value->arg_len == 2) {
    if (!coff_emit_runtime_arg_value(text, fun, value->args[1], temp_base, &slot, ctx, diag)) return false;
  } else {
    coff_emit_runtime_arg_u32(text, 0, temp_base, &slot);
  }
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_PARSE_OP, 4, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_MAYBE_SCALAR) coff_emit_normalize_u32_maybe_result(text);
  else if (value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_parse_u32_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value) return coff_diag_at(diag, "direct COFF parse value is missing", 1, 1, "missing parse value");
  if (value->kind != IR_VALUE_PARSE_I32 && value->kind != IR_VALUE_PARSE_U32) {
    return coff_diag_at(diag, "direct COFF parse value kind is invalid for this helper", value->line, value->column, "invalid parse value");
  }
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 2, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_call(text, ctx, value->kind == IR_VALUE_PARSE_I32 ? COFF_RUNTIME_PARSE_I32 : COFF_RUNTIME_PARSE_U32, 2, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  coff_emit_normalize_u32_maybe_result(text);
  return true;
}

static CoffRuntimeHelper coff_fmt_runtime_helper(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_FMT_BOOL: return COFF_RUNTIME_FMT_BOOL;
    case IR_VALUE_FMT_HEX_U32: return COFF_RUNTIME_FMT_HEX_U32;
    case IR_VALUE_FMT_I32: return COFF_RUNTIME_FMT_I32;
    case IR_VALUE_FMT_U32: return COFF_RUNTIME_FMT_U32;
    case IR_VALUE_FMT_USIZE: return COFF_RUNTIME_FMT_USIZE;
    default: return COFF_RUNTIME_HELPER_COUNT;
  }
}

static bool coff_emit_fmt_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return coff_diag_at(diag, "direct COFF std.fmt helper requires a buffer and value", value ? value->line : 1, value ? value->column : 1, "missing fmt input");
  CoffRuntimeHelper helper = coff_fmt_runtime_helper(value->kind);
  if (helper == COFF_RUNTIME_HELPER_COUNT) return coff_diag_at(diag, "direct COFF std.fmt helper is unsupported", value->line, value->column, "unsupported std.fmt helper");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 3, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_value(text, fun, value->right, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_call(text, ctx, helper, 3, temp_base, value, diag)) return false;
  coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
  z_x64_emit_add_rsp(text, total_stack);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_test_rax_rax(text, true);
  z_x64_emit_setcc_al_to_bool(text, 0x95);
  return true;
}

static bool coff_emit_proc_capture_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) {
    return coff_diag_at(diag, "direct COFF std.proc.capture requires a command and output buffer", value ? value->line : 1, value ? value->column : 1, "missing process capture input");
  }
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  const unsigned abi_slots = 5;
  const unsigned result_slot = 5;
  coff_emit_runtime_call_begin_with_temps(text, abi_slots, result_slot + 2u, &temp_base, &total_stack);
  coff_emit_runtime_arg_stack_ptr(text, temp_base, result_slot, &slot);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->right, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_PROC_CAPTURE, abi_slots, temp_base, value, diag)) return false;
  coff_emit_runtime_temp_slot_load(text, temp_base, result_slot, 0);
  coff_emit_runtime_temp_slot_load(text, temp_base, result_slot + 1u, 2);
  z_x64_emit_add_rsp(text, total_stack);
  return true;
}

static bool coff_emit_proc_capture_files_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) {
    return coff_diag_at(diag, "direct COFF std.proc.captureFiles requires a command, stdout path, and stderr path", value ? value->line : 1, value ? value->column : 1, "missing process capture files input");
  }
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  const unsigned abi_slots = 6;
  coff_emit_runtime_call_begin(text, abi_slots, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->right, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->index, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_PROC_CAPTURE_FILES, abi_slots, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  return true;
}

static bool coff_emit_math_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) {
    return coff_diag_at(diag, "direct COFF std.math helper supports at most three scalar arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.math arity");
  }

  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  bool maybe_usize = value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE;
  if (maybe_usize) {
    const unsigned abi_slots = 5;
    const unsigned result_slot = 5;
    coff_emit_runtime_call_begin_with_temps(text, abi_slots, result_slot + 2u, &temp_base, &total_stack);
    coff_emit_runtime_arg_stack_ptr(text, temp_base, result_slot, &slot);
    for (size_t i = 0; i < 3; i++) {
      if (i < value->arg_len) {
        if (!coff_emit_runtime_arg_value(text, fun, value->args[i], temp_base, &slot, ctx, diag)) return false;
      } else {
        coff_emit_runtime_arg_u32(text, 0, temp_base, &slot);
      }
    }
    coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
    if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_MATH_USIZE_OP, abi_slots, temp_base, value, diag)) return false;
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot, 0);
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot + 1u, 2);
    z_x64_emit_add_rsp(text, total_stack);
    return true;
  }

  coff_emit_runtime_call_begin(text, 4, &temp_base, &total_stack);
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!coff_emit_runtime_arg_value(text, fun, value->args[i], temp_base, &slot, ctx, diag)) return false;
    } else {
      coff_emit_runtime_arg_u32(text, 0, temp_base, &slot);
    }
  }
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_MATH_OP, 4, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_MAYBE_SCALAR) coff_emit_normalize_u32_maybe_result(text);
  else if (value->type == IR_TYPE_I32 || value->type == IR_TYPE_U32 || value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_search_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return coff_diag_at(diag, "direct COFF std.search helper requires a span and needle", value ? value->line : 1, value ? value->column : 1, "invalid std.search input");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 4, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_value(text, fun, value->right, temp_base, &slot, ctx, diag)) return false;
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_SEARCH_OP, 4, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  return true;
}

static bool coff_emit_sort_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return coff_diag_at(diag, "direct COFF std.sort helper requires a span", value ? value->line : 1, value ? value->column : 1, "invalid std.sort input");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 3, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  CoffRuntimeHelper helper = value->type == IR_TYPE_BOOL ? COFF_RUNTIME_SORT_IS_SORTED_OP : COFF_RUNTIME_SORT_OP;
  if (!coff_emit_runtime_call(text, ctx, helper, 3, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static void coff_emit_http_packed_span_result(ZBuf *text);

static CoffRuntimeHelper coff_json_write_runtime_helper(IrJsonWriteOp op) {
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW: return COFF_RUNTIME_JSON_WRITE_FIELD_RAW;
    case IR_JSON_WRITE_FIELD_STRING: return COFF_RUNTIME_JSON_WRITE_FIELD_STRING;
    case IR_JSON_WRITE_FIELD_U32: return COFF_RUNTIME_JSON_WRITE_FIELD_U32;
    case IR_JSON_WRITE_FIELD_BOOL: return COFF_RUNTIME_JSON_WRITE_FIELD_BOOL;
    case IR_JSON_WRITE_OBJECT1_STRING: return COFF_RUNTIME_JSON_WRITE_OBJECT1_STRING;
    case IR_JSON_WRITE_OBJECT1_U32: return COFF_RUNTIME_JSON_WRITE_OBJECT1_U32;
    case IR_JSON_WRITE_OBJECT1_BOOL: return COFF_RUNTIME_JSON_WRITE_OBJECT1_BOOL;
    case IR_JSON_WRITE_OBJECT2_FIELDS: return COFF_RUNTIME_JSON_WRITE_OBJECT2_FIELDS;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD: return COFF_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD: return COFF_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD;
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD: return COFF_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD;
    case IR_JSON_WRITE_ARRAY2_STRINGS: return COFF_RUNTIME_JSON_WRITE_ARRAY2_STRINGS;
    case IR_JSON_WRITE_ARRAY2_U32: return COFF_RUNTIME_JSON_WRITE_ARRAY2_U32;
    case IR_JSON_WRITE_ARRAY2_BOOLS: return COFF_RUNTIME_JSON_WRITE_ARRAY2_BOOLS;
  }
  return COFF_RUNTIME_HELPER_COUNT;
}

static unsigned coff_json_write_runtime_abi_slots(IrJsonWriteOp op) {
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW:
    case IR_JSON_WRITE_FIELD_STRING:
    case IR_JSON_WRITE_OBJECT1_STRING:
    case IR_JSON_WRITE_OBJECT2_FIELDS:
    case IR_JSON_WRITE_ARRAY2_STRINGS:
      return 6u;
    case IR_JSON_WRITE_FIELD_U32:
    case IR_JSON_WRITE_FIELD_BOOL:
    case IR_JSON_WRITE_OBJECT1_U32:
    case IR_JSON_WRITE_OBJECT1_BOOL:
      return 5u;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD:
      return 8u;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD:
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD:
      return 7u;
    case IR_JSON_WRITE_ARRAY2_U32:
    case IR_JSON_WRITE_ARRAY2_BOOLS:
      return 4u;
  }
  return 0u;
}

static bool coff_emit_json_write_runtime_args(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned temp_base, unsigned *slot, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 3) return coff_diag_at(diag, "direct COFF JSON writer requires arguments", value ? value->line : 1, value ? value->column : 1, "invalid JSON writer");
  IrJsonWriteOp op = (IrJsonWriteOp)value->int_value;
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[0], temp_base, slot, ctx, diag)) return false;
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW:
    case IR_JSON_WRITE_FIELD_STRING:
    case IR_JSON_WRITE_OBJECT1_STRING:
    case IR_JSON_WRITE_OBJECT2_FIELDS:
    case IR_JSON_WRITE_ARRAY2_STRINGS:
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, slot, ctx, diag)) return false;
      return coff_emit_runtime_arg_byte_view(text, fun, value->args[2], temp_base, slot, ctx, diag);
    case IR_JSON_WRITE_FIELD_U32:
    case IR_JSON_WRITE_FIELD_BOOL:
    case IR_JSON_WRITE_OBJECT1_U32:
    case IR_JSON_WRITE_OBJECT1_BOOL:
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, slot, ctx, diag)) return false;
      return coff_emit_runtime_arg_value(text, fun, value->args[2], temp_base, slot, ctx, diag);
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD:
      if (value->arg_len < 4) return coff_diag_at(diag, "direct COFF JSON writer object2 string field requires four arguments", value->line, value->column, "invalid JSON writer arity");
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[2], temp_base, slot, ctx, diag)) return false;
      return coff_emit_runtime_arg_byte_view(text, fun, value->args[3], temp_base, slot, ctx, diag);
    case IR_JSON_WRITE_OBJECT2_U32_FIELD:
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD:
      if (value->arg_len < 4) return coff_diag_at(diag, "direct COFF JSON writer object2 scalar field requires four arguments", value->line, value->column, "invalid JSON writer arity");
      if (!coff_emit_runtime_arg_byte_view(text, fun, value->args[1], temp_base, slot, ctx, diag)) return false;
      if (!coff_emit_runtime_arg_value(text, fun, value->args[2], temp_base, slot, ctx, diag)) return false;
      return coff_emit_runtime_arg_byte_view(text, fun, value->args[3], temp_base, slot, ctx, diag);
    case IR_JSON_WRITE_ARRAY2_U32:
    case IR_JSON_WRITE_ARRAY2_BOOLS:
      if (!coff_emit_runtime_arg_value(text, fun, value->args[1], temp_base, slot, ctx, diag)) return false;
      return coff_emit_runtime_arg_value(text, fun, value->args[2], temp_base, slot, ctx, diag);
  }
  return coff_diag_at(diag, "direct COFF JSON writer op is invalid", value->line, value->column, "invalid JSON writer");
}

static bool coff_emit_json_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value) return coff_diag_at(diag, "direct COFF JSON helper requires a value", 1, 1, "invalid JSON input");
  if (value->kind == IR_VALUE_JSON_WRITE_RUNTIME) {
    IrJsonWriteOp op = (IrJsonWriteOp)value->int_value;
    CoffRuntimeHelper helper = coff_json_write_runtime_helper(op);
    unsigned arg_count = coff_json_write_runtime_abi_slots(op);
    if (helper == COFF_RUNTIME_HELPER_COUNT || arg_count == 0) return coff_diag_at(diag, "direct COFF JSON writer op is invalid", value->line, value->column, "invalid JSON writer");
    unsigned temp_base = 0;
    unsigned total_stack = 0;
    unsigned slot = 0;
    coff_emit_runtime_call_begin(text, arg_count, &temp_base, &total_stack);
    if (!coff_emit_json_write_runtime_args(text, fun, value, temp_base, &slot, ctx, diag)) return false;
    if (!coff_emit_runtime_call(text, ctx, helper, arg_count, temp_base, value, diag)) return false;
    coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
    coff_emit_http_packed_span_result(text);
    z_x64_emit_add_rsp(text, total_stack);
    return true;
  }
  if (!value->left) return coff_diag_at(diag, "direct COFF JSON helper requires a byte view", value->line, value->column, "invalid JSON input");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  unsigned arg_count = value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES ? 3u :
                       (value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ? 5u :
                        (value->kind == IR_VALUE_JSON_STRING_FIELD ? 6u :
                         ((value->kind == IR_VALUE_JSON_FIELD || value->kind == IR_VALUE_JSON_STRING_DECODE || value->kind == IR_VALUE_JSON_WRITE_STRING) ? 4u : 2u)));
  coff_emit_runtime_call_begin(text, arg_count, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES) {
    coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  } else if (value->kind == IR_VALUE_JSON_FIELD || value->kind == IR_VALUE_JSON_LOOKUP_SCALAR || value->kind == IR_VALUE_JSON_STRING_DECODE || value->kind == IR_VALUE_JSON_STRING_FIELD || value->kind == IR_VALUE_JSON_WRITE_STRING) {
    if (!coff_emit_runtime_arg_byte_view(text, fun, value->right, temp_base, &slot, ctx, diag)) return false;
    if (value->kind == IR_VALUE_JSON_LOOKUP_SCALAR) coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
    if (value->kind == IR_VALUE_JSON_STRING_FIELD && !coff_emit_runtime_arg_byte_view(text, fun, value->index, temp_base, &slot, ctx, diag)) return false;
  }
  CoffRuntimeHelper helper = value->kind == IR_VALUE_JSON_DIAGNOSTIC_BYTES ? COFF_RUNTIME_JSON_DIAGNOSTIC :
                             (value->kind == IR_VALUE_JSON_FIELD ? COFF_RUNTIME_JSON_FIELD :
                              (value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ? COFF_RUNTIME_JSON_LOOKUP_SCALAR :
                               (value->kind == IR_VALUE_JSON_STRING_DECODE ? COFF_RUNTIME_JSON_STRING_DECODE :
                                (value->kind == IR_VALUE_JSON_STRING_FIELD ? COFF_RUNTIME_JSON_STRING_FIELD :
                                 (value->kind == IR_VALUE_JSON_WRITE_STRING ? COFF_RUNTIME_JSON_WRITE_STRING : COFF_RUNTIME_JSON_PARSE_BYTES)))));
  if (!coff_emit_runtime_call(text, ctx, helper, arg_count, temp_base, value, diag)) return false;
  if (value->kind == IR_VALUE_JSON_FIELD || value->kind == IR_VALUE_JSON_STRING_DECODE || value->kind == IR_VALUE_JSON_STRING_FIELD || value->kind == IR_VALUE_JSON_WRITE_STRING) {
    coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
    coff_emit_http_packed_span_result(text);
    z_x64_emit_add_rsp(text, total_stack);
    return true;
  }
  z_x64_emit_add_rsp(text, total_stack);
  if (value->kind == IR_VALUE_JSON_LOOKUP_SCALAR) {
    coff_emit_normalize_u32_maybe_result(text);
  }
  if (value->kind == IR_VALUE_JSON_VALIDATE_BYTES) {
    z_x64_emit_cmp_reg_i8(text, 0, 0, true);
    z_x64_emit_setcc_al_to_bool(text, 0x9d);
  } else if (value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES) {
    z_x64_emit_test_rax_rax(text, true);
    size_t ok = z_x64_emit_jcc32_placeholder(text, 0x89);
    z_x64_emit_xor_rax_rax(text);
    z_x64_patch_rel32(text, ok, text->len);
  }
  return true;
}

/* Unpack a runtime packed span result (bit 63 = has, bits 32..62 = len, low 32 = offset)
   into the Maybe<Span<u8>> register convention: rax = has, rdx = base + offset, rcx = len.
   Expects the view base pointer in rdx and the packed result in rax. */
static void coff_emit_http_packed_span_result(ZBuf *text) {
  z_x64_emit_mov_reg_from_reg(text, 8, 0, true);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  z_x64_emit_shr_reg_imm8(text, 0, 32, true);
  z_x64_emit_and_reg_u32(text, 0, 0x7fffffffu, false);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, false);
  z_x64_emit_add_reg_reg(text, 2, 0, true);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  z_x64_emit_shr_reg_imm8(text, 0, 63, true);
}

static bool coff_emit_http_request_span_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return coff_diag_at(diag, "direct COFF HTTP request helper requires a request span", value ? value->line : 1, value ? value->column : 1, "invalid HTTP request input");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 2, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  CoffRuntimeHelper helper = value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ? COFF_RUNTIME_HTTP_REQUEST_METHOD_NAME : COFF_RUNTIME_HTTP_REQUEST_PATH;
  if (!coff_emit_runtime_call(text, ctx, helper, 2, temp_base, value, diag)) return false;
  coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
  coff_emit_http_packed_span_result(text);
  z_x64_emit_add_rsp(text, total_stack);
  return true;
}

static bool coff_emit_http_request_body_within_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return coff_diag_at(diag, "direct COFF HTTP request helper requires a request span", value ? value->line : 1, value ? value->column : 1, "invalid HTTP request input");
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 4, &temp_base, &total_stack);
  if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
  if (!coff_emit_runtime_arg_value(text, fun, value->index, temp_base, &slot, ctx, diag)) return false;
  coff_emit_runtime_arg_u32(text, value->int_value ? 1u : 0u, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_HTTP_REQUEST_BODY_WITHIN, 4, temp_base, value, diag)) return false;
  coff_emit_runtime_temp_slot_load(text, temp_base, 0, 2);
  coff_emit_http_packed_span_result(text);
  z_x64_emit_add_rsp(text, total_stack);
  return true;
}

static bool coff_emit_time_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) {
    return coff_diag_at(diag, "direct COFF std.time helper supports at most three scalar arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.time arity");
  }

  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 4, &temp_base, &total_stack);
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!coff_emit_runtime_arg_value(text, fun, value->args[i], temp_base, &slot, ctx, diag)) return false;
    } else {
      coff_emit_runtime_arg_u32(text, 0, temp_base, &slot);
    }
  }
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_TIME_OP, 4, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_I32 || value->type == IR_TYPE_U32 || value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_term_runtime_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (value && (IrTermOp)value->int_value == IR_TERM_OP_READ_INPUT) {
    if (!value->left) {
      return coff_diag_at(diag, "direct COFF std.term.readInput requires a caller buffer", value->line, value->column, "missing terminal input buffer");
    }
    unsigned temp_base = 0;
    unsigned total_stack = 0;
    unsigned slot = 0;
    const unsigned abi_slots = 3;
    const unsigned result_slot = 3;
    coff_emit_runtime_call_begin_with_temps(text, abi_slots, result_slot + 2u, &temp_base, &total_stack);
    coff_emit_runtime_arg_stack_ptr(text, temp_base, result_slot, &slot);
    if (!coff_emit_runtime_arg_byte_view(text, fun, value->left, temp_base, &slot, ctx, diag)) return false;
    if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_TERM_READ_INPUT, abi_slots, temp_base, value, diag)) return false;
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot, 0);
    coff_emit_runtime_temp_slot_load(text, temp_base, result_slot + 1u, 2);
    z_x64_emit_add_rsp(text, total_stack);
    return true;
  }
  if (!value || value->arg_len > 1) {
    return coff_diag_at(diag, "direct COFF std.term helper supports at most one fallback argument", value ? value->line : 1, value ? value->column : 1, "invalid std.term arity");
  }
  unsigned temp_base = 0;
  unsigned total_stack = 0;
  unsigned slot = 0;
  coff_emit_runtime_call_begin(text, 2, &temp_base, &total_stack);
  if (value->arg_len == 1) {
    if (!coff_emit_runtime_arg_value(text, fun, value->args[0], temp_base, &slot, ctx, diag)) return false;
  } else {
    coff_emit_runtime_arg_u32(text, 0, temp_base, &slot);
  }
  coff_emit_runtime_arg_u32(text, (uint32_t)value->int_value, temp_base, &slot);
  if (!coff_emit_runtime_call(text, ctx, COFF_RUNTIME_TERM_OP, 2, temp_base, value, diag)) return false;
  z_x64_emit_add_rsp(text, total_stack);
  if (value->type == IR_TYPE_BOOL) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool coff_emit_rand_bounded_from_r8(ZBuf *text, const IrFunction *fun, const IrValue *value, bool add_low) {
  z_x64_emit_test_reg_reg(text, 8, false);
  size_t none = z_x64_emit_jcc32_placeholder(text, 0x84);

  z_x64_emit_xor_reg_reg(text, 0, false);
  z_x64_emit_sub_reg_reg(text, 0, 8, false);
  z_x64_emit_mov_reg_from_reg(text, 1, 8, false);
  z_x64_emit_div_rax_rcx(text, false, true, true);
  z_x64_emit_mov_reg_from_rax(text, 9, false);

  size_t loop = text->len;
  coff_emit_load_local_slot_eax(text, fun, value->local_index, 0);
  z_x64_emit_imul_reg_i32(text, 0, 1664525, false);
  z_x64_emit_add_rax_u32(text, 1013904223u, false);
  coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 0, false);
  z_x64_emit_cmp_reg_reg(text, 0, 9, false);
  size_t retry = z_x64_emit_jcc32_placeholder(text, 0x82);

  z_x64_emit_mov_reg_from_reg(text, 1, 8, false);
  z_x64_emit_div_rax_rcx(text, false, true, true);
  if (add_low) z_x64_emit_add_reg_reg(text, 0, 10, false);
  z_x64_emit_mov_reg_from_rax(text, 2, false);
  z_x64_emit_mov_eax_u32(text, 1);
  size_t done = z_x64_emit_jmp32_placeholder(text, 0xe9);

  z_x64_patch_rel32(text, retry, loop);
  z_x64_patch_rel32(text, none, text->len);
  z_x64_emit_xor_reg_reg(text, 2, false);
  z_x64_emit_xor_reg_reg(text, 0, false);
  z_x64_patch_rel32(text, done, text->len);
  return true;
}

static bool coff_emit_rand_maybe_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!fun || !value || value->local_index >= fun->local_len) {
    return coff_diag_at(diag, "direct COFF std.rand bounded local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid RandSource");
  }
  if (value->kind == IR_VALUE_RAND_NEXT_BELOW) {
    if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_mov_reg_from_rax(text, 8, false);
    return coff_emit_rand_bounded_from_r8(text, fun, value, false);
  }
  if (value->kind == IR_VALUE_RAND_RANGE_U32) {
    if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
    z_x64_emit_mov_reg_from_rax(text, 8, false);
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_mov_reg_from_reg(text, 10, 1, false);
    z_x64_emit_cmp_reg_reg(text, 8, 1, false);
    size_t empty = z_x64_emit_jcc32_placeholder(text, 0x86);
    z_x64_emit_sub_reg_reg(text, 8, 1, false);
    bool ok = coff_emit_rand_bounded_from_r8(text, fun, value, true);
    size_t done = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, empty, text->len);
    z_x64_emit_xor_reg_reg(text, 2, false);
    z_x64_emit_xor_reg_reg(text, 0, false);
    z_x64_patch_rel32(text, done, text->len);
    return ok;
  }
  return coff_diag_at(diag, "direct COFF std.rand bounded helper is invalid", value->line, value->column, "invalid rand helper");
}

static bool coff_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value) return coff_diag_at(diag, "direct COFF expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
      z_x64_emit_mov_eax_u32(text, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_INT:
      if (coff_type_is_i64(value->type)) z_x64_emit_mov_rax_u64(text, (uint64_t)value->int_value);
      else z_x64_emit_mov_eax_u32(text, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL: return coff_emit_local_value(text, fun, value, diag);
    case IR_VALUE_CAST:
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      coff_emit_cast_normalize_rax(text, value->type);
      return true;
    case IR_VALUE_BINARY: return coff_emit_binary_value(text, fun, value, ctx, diag);
    case IR_VALUE_COMPARE: return coff_emit_compare_value(text, fun, value, ctx, diag);
    case IR_VALUE_CALL: return coff_emit_call_value(text, fun, value, ctx, diag);
    case IR_VALUE_RAND_NEXT_U32:
      if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      coff_emit_load_local_slot_eax(text, fun, value->local_index, 0);
      z_x64_emit_imul_reg_i32(text, 0, 1664525, false);
      z_x64_emit_add_rax_u32(text, 1013904223u, false);
      coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 0, false);
      return true;
    case IR_VALUE_RAND_NEXT_BELOW:
    case IR_VALUE_RAND_RANGE_U32:
      return coff_emit_rand_maybe_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      coff_emit_load_local_slot_eax(text, fun, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12);
      return true;
    case IR_VALUE_VEC_PUSH: return coff_emit_vec_push_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_GET: return coff_emit_vec_get_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_SET: return coff_emit_vec_set_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_CLEAR: return coff_emit_vec_clear_value(text, fun, value, diag);
    case IR_VALUE_VEC_POP: return coff_emit_vec_pop_value(text, fun, value, diag);
    case IR_VALUE_VEC_TRUNCATE: return coff_emit_vec_truncate_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_REMOVE_SWAP: return coff_emit_vec_remove_swap_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_INDEX:
    case IR_VALUE_VEC_CONTAINS:
      return coff_emit_vec_lookup_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_INSERT_UNIQUE: return coff_emit_vec_insert_unique_value(text, fun, value, ctx, diag);
    case IR_VALUE_VEC_REMOVE_VALUE: return coff_emit_vec_remove_value_value(text, fun, value, ctx, diag);
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len ||
          (fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW && fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR)) {
        return coff_diag_at(diag, "direct COFF maybe helper requires a Maybe local", value->line, value->column, "invalid maybe local");
      }
      coff_emit_load_local_slot_eax(text, fun, value->local_index, 0);
      return true;
    case IR_VALUE_MAYBE_VALUE:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR) {
        return coff_diag_at(diag, "direct COFF maybe scalar value requires a Maybe scalar local", value->line, value->column, "invalid maybe value");
      }
      coff_emit_load_local_slot_rax(text, fun, value->local_index, 8);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN: return coff_emit_byte_view_len(text, fun, value->left, ctx, diag);
    case IR_VALUE_BYTE_COPY: return coff_emit_byte_copy_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_FILL: return coff_emit_byte_fill_value(text, fun, value, ctx, diag);
    case IR_VALUE_ITEM_COPY: return coff_emit_item_copy_value(text, fun, value, ctx, diag);
    case IR_VALUE_ITEM_FILL: return coff_emit_item_fill_value(text, fun, value, ctx, diag);
    case IR_VALUE_ITEM_CONTAINS: return coff_emit_item_contains_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ: return coff_emit_byte_view_eq_value(text, fun, value, ctx, diag);
    case IR_VALUE_CRC32_BYTES: return coff_emit_crc32_bytes_value(text, fun, value, ctx, diag);
    case IR_VALUE_ASCII_RUNTIME: return coff_emit_ascii_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_TEXT_RUNTIME: return coff_emit_text_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_PARSE_RUNTIME: return coff_emit_parse_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32:
      return coff_emit_parse_u32_value(text, fun, value, ctx, diag);
    case IR_VALUE_FMT_BOOL:
    case IR_VALUE_FMT_HEX_U32:
    case IR_VALUE_FMT_I32:
    case IR_VALUE_FMT_U32:
    case IR_VALUE_FMT_USIZE:
      return coff_emit_fmt_value(text, fun, value, ctx, diag);
    case IR_VALUE_PROC_CAPTURE:
      return coff_emit_proc_capture_value(text, fun, value, ctx, diag);
    case IR_VALUE_PROC_CAPTURE_FILES:
      return coff_emit_proc_capture_files_value(text, fun, value, ctx, diag);
    case IR_VALUE_STR_RUNTIME: return coff_emit_str_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_TIME_RUNTIME: return coff_emit_time_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_TERM_RUNTIME: return coff_emit_term_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_MATH_RUNTIME: return coff_emit_math_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_SEARCH_RUNTIME: return coff_emit_search_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_SORT_RUNTIME: return coff_emit_sort_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
    case IR_VALUE_JSON_FIELD:
    case IR_VALUE_JSON_LOOKUP_SCALAR:
    case IR_VALUE_JSON_STRING_DECODE:
    case IR_VALUE_JSON_STRING_FIELD:
    case IR_VALUE_JSON_WRITE_STRING:
    case IR_VALUE_JSON_WRITE_RUNTIME:
      return coff_emit_json_runtime_value(text, fun, value, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME:
    case IR_VALUE_HTTP_REQUEST_PATH:
      return coff_emit_http_request_span_value(text, fun, value, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN:
      return coff_emit_http_request_body_within_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: return coff_emit_byte_view_index_load_value(text, fun, value, ctx, diag);
    case IR_VALUE_INDEX_LOAD: return coff_emit_index_load_value(text, fun, value, ctx, diag);
    case IR_VALUE_FIELD_LOAD: return coff_emit_field_load_value(text, fun, value, diag);
    case IR_VALUE_RECORD_ADDR: return coff_emit_record_addr_value(text, fun, value, diag);
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported value kind %d", value ? (int)value->kind : -1);
      return coff_diag_at(diag, "direct COFF value kind is unsupported", value->line, value->column, actual);
    }
  }
}

static bool coff_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, CoffEmitContext *ctx, ZDiag *diag);

static bool coff_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return coff_diag_at(diag, "direct COFF World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!coff_emit_byte_view_pair(text, fun, instr->value, 2, 8, ctx, diag)) return false;
  z_x64_emit_mov_reg_u32(text, 1, instr->field_offset == 2 ? 2u : 1u); // ecx = fd
  z_x64_emit_sub_rsp(text, 32);
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_add_rsp(text, 32);
  z_x64_emit_test_rax_rax(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!coff_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_WRITE_FAILED)) return false; // trap on runtime write failure
  z_x64_patch_rel32(text, ok_patch, text->len);
  return z_coff_record_instr_runtime_patch(ctx, COFF_RUNTIME_WORLD_WRITE, patch, instr, diag);
}

static bool coff_emit_local_set_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!coff_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, false);
  return true;
}
static bool coff_emit_local_set_alloc(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return coff_diag_at(diag, "direct COFF FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
  if (!coff_emit_byte_view_pair(text, fun, instr->value->left, 0, 2, ctx, diag)) return false;
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, false);
  z_x64_emit_mov_eax_u32(text, 0);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 12, false);
  return true;
}
static bool coff_emit_local_set_vec(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return coff_diag_at(diag, "direct COFF Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
  if (!coff_emit_byte_view_pair(text, fun, instr->value->left, 0, 2, ctx, diag)) return false;
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 12, false);
  z_x64_emit_mov_eax_u32(text, 0);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, false);
  return true;
}
static bool coff_emit_local_set_maybe_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (instr->value && instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
    z_x64_emit_mov_eax_u32(text, instr->value->data_len ? 1u : 0u);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    if (!instr->value->data_len) {
      z_x64_emit_xor_eax_eax(text);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
      return true;
    }
    if (!coff_emit_byte_view_pair(text, fun, instr->value->left, 0, 2, ctx, diag)) return false;
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 16, false);
    return true;
  }
  if (instr->value && (instr->value->kind == IR_VALUE_CALL ||
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
                       instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
                       instr->value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN) && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 1, 16, false);
    return true;
  }
  if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return coff_diag_at(diag, "direct COFF allocation source is invalid", instr->line, instr->column, "invalid allocation");
  if (!coff_emit_value(text, fun, instr->value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_eax(text, fun, instr->value->local_index, 12);
  coff_emit_load_local_slot_reg(text, fun, instr->value->local_index, 8, 1, false);
  z_x64_emit_pop_rax(text);
  z_x64_emit_mov_reg_from_rax(text, 2, false);
  z_x64_emit_add_rax_rcx(text, false);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x86);
  z_x64_emit_mov_eax_u32(text, 0);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
  size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_mov_eax_u32(text, 1);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
  coff_emit_load_local_slot_reg(text, fun, instr->value->local_index, 0, 2, true);
  z_x64_emit_add_reg_reg(text, 2, 2, true);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
  z_x64_emit_mov_reg_from_reg(text, 0, 2, false);
  coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
  z_x64_emit_mov_eax_from_ecx(text);
  coff_emit_store_local_slot_from_reg(text, fun, instr->value->local_index, 0, 12, false);
  z_x64_patch_rel32(text, end_patch, text->len);
  return true;
}

static bool coff_emit_local_set_maybe_scalar(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!instr->value) return coff_diag_at(diag, "direct COFF Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
  if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
    z_x64_emit_mov_eax_u32(text, instr->value->data_len ? 1u : 0u);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    z_x64_emit_mov_rax_u64(text, (uint64_t)instr->value->int_value);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
    return true;
  }
  if ((instr->value->kind == IR_VALUE_CALL ||
       instr->value->kind == IR_VALUE_VEC_GET ||
       instr->value->kind == IR_VALUE_ASCII_RUNTIME ||
       instr->value->kind == IR_VALUE_TEXT_RUNTIME ||
       instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
       instr->value->kind == IR_VALUE_PARSE_I32 ||
       instr->value->kind == IR_VALUE_PARSE_U32 ||
       instr->value->kind == IR_VALUE_RAND_NEXT_BELOW ||
       instr->value->kind == IR_VALUE_RAND_RANGE_U32 ||
       instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ||
       instr->value->kind == IR_VALUE_PROC_CAPTURE ||
       instr->value->kind == IR_VALUE_TERM_RUNTIME ||
       instr->value->kind == IR_VALUE_MATH_RUNTIME) && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
    return true;
  }
  if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len && fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
    coff_emit_load_local_slot_eax(text, fun, instr->value->local_index, 0);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    coff_emit_load_local_slot_rax(text, fun, instr->value->local_index, 8);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
    return true;
  }
  if (coff_type_is_scalar32(instr->value->type) || coff_type_is_i64(instr->value->type)) {
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
    z_x64_emit_mov_eax_u32(text, 1);
    coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    return true;
  }
  return coff_diag_at(diag, "direct COFF Maybe scalar initializer is unsupported", instr->line, instr->column, "unsupported Maybe scalar initializer");
}

static bool coff_emit_local_set_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF local store is out of range", instr->line, instr->column, "invalid local");
  switch (fun->locals[instr->local_index].type) {
    case IR_TYPE_BYTE_VIEW: return coff_emit_local_set_byte_view(text, fun, instr, ctx, diag);
    case IR_TYPE_ALLOC: return coff_emit_local_set_alloc(text, fun, instr, ctx, diag);
    case IR_TYPE_VEC: return coff_emit_local_set_vec(text, fun, instr, ctx, diag);
    case IR_TYPE_MAYBE_BYTE_VIEW: return coff_emit_local_set_maybe_byte_view(text, fun, instr, ctx, diag);
    case IR_TYPE_MAYBE_SCALAR: return coff_emit_local_set_maybe_scalar(text, fun, instr, ctx, diag);
    default: if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false; coff_emit_store_local_scalar_from_reg(text, fun, instr->local_index, 0, fun->locals[instr->local_index].type); return true;
  }
}
static bool coff_emit_field_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF field store record is out of range", instr->line, instr->column, "invalid record local");
  const IrLocal *local = &fun->locals[instr->local_index];
  if (instr->value && instr->value->type == IR_TYPE_BYTE_VIEW) {
    if (local->is_record_ref) {
      if (!coff_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
      z_x64_emit_push_rax(text);
      z_x64_emit_push_reg64(text, 2);
      coff_emit_load_local_slot_rax(text, fun, instr->local_index, 0);
      if (instr->field_offset > 0) z_x64_emit_add_rax_u32(text, instr->field_offset, true);
      z_x64_emit_mov_reg_from_reg(text, 1, 0, true);
      z_x64_emit_pop_reg64(text, 2);
      z_x64_emit_pop_rax(text);
      coff_emit_store_ptr_element(text, 1, 0, IR_TYPE_U64);
      z_x64_emit_add_reg_i8(text, 1, 8, true);
      coff_emit_store_ptr_element(text, 1, 2, IR_TYPE_U64);
      return true;
    }
    if (!local->is_record) return coff_diag_at(diag, "direct COFF byte-view field store requires record local", instr->line, instr->column, "non-record local");
    if (!coff_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
    coff_emit_store_field_from_eax(text, fun, instr->local_index, instr->field_offset, IR_TYPE_U64);
    z_x64_emit_mov_reg_from_reg(text, 0, 2, true);
    coff_emit_store_field_from_eax(text, fun, instr->local_index, instr->field_offset + 8u, IR_TYPE_U64);
    return true;
  }
  if (local->is_record_ref) {
    coff_emit_load_local_slot_rax(text, fun, instr->local_index, 0);
    if (instr->field_offset > 0) z_x64_emit_add_rax_u32(text, instr->field_offset, true);
    z_x64_emit_push_rax(text);
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    coff_emit_store_ptr_element(text, 1, 0, instr->value ? instr->value->type : IR_TYPE_I32);
    return true;
  }
  if (!local->is_record) return coff_diag_at(diag, "direct COFF field store requires record local", instr->line, instr->column, "non-record local");
  if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
  coff_emit_store_field_from_eax(text, fun, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32); return true;
}

static bool coff_emit_byte_view_index_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  const IrLocal *local = instr->array_index < fun->local_len ? &fun->locals[instr->array_index] : NULL;
  IrTypeKind element_type = local && local->element_type != IR_TYPE_UNSUPPORTED ? local->element_type : IR_TYPE_U8;
  if (!instr->value || instr->value->type != element_type) return coff_diag_at(diag, "direct COFF byte-view indexed store value type does not match span element", instr->line, instr->column, "unsupported byte-view store value");
  if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!instr->index || !coff_emit_value(text, fun, instr->index, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_eax(text, fun, instr->array_index, 8);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  if (!coff_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_INDEX_BOUNDS)) return false;
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_push_rax(text);
  coff_emit_load_local_slot_rax(text, fun, instr->array_index, 0);
  z_x64_emit_pop_reg64(text, 1);
  coff_emit_scale_index_into_rax(text, element_type);
  z_x64_emit_mov_reg_from_rax(text, 2, true);
  z_x64_emit_pop_reg64(text, 0);
  coff_emit_store_ptr_element(text, 2, 0, element_type);
  return true;
}

static bool coff_emit_index_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (instr->array_index >= fun->local_len) return coff_diag_at(diag, "direct COFF indexed store array is out of range", instr->line, instr->column, "invalid array local");
  const IrLocal *local = &fun->locals[instr->array_index];
  if (local->type == IR_TYPE_BYTE_VIEW) return coff_emit_byte_view_index_store_instr(text, fun, instr, ctx, diag);
  unsigned const_index = 0;
  if (local->is_array && coff_type_is_word_array_element(local->element_type) && coff_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_local_slot_from_reg(text, fun, instr->array_index, 0, const_index * 4u, false);
    return true;
  }
  if (local->is_array && coff_type_is_array_element(local->element_type)) {
    if (!instr->index || !coff_emit_value(text, fun, instr->index, ctx, diag)) return false;
    if (!coff_emit_u8_array_bounds_check(text, local, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    coff_emit_array_base_rdx(text, fun, instr->array_index);
    z_x64_emit_lea_base_index_scale_disp_reg(text, 2, 2, 1, coff_type_byte_size(local->element_type), 0);
    coff_emit_store_ptr_element(text, 2, 0, local->element_type);
    return true;
  }
  return coff_diag_at(diag, "direct COFF indexed store requires [N]u8 or integer arrays", instr->line, instr->column, "unsupported array local");
}

static bool coff_emit_if_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_test_rax_rax(text, false);
  size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
  if (instr->else_len > 0) {
    size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, false_patch, text->len);
    if (!coff_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
    z_x64_patch_rel32(text, end_patch, text->len);
  } else z_x64_patch_rel32(text, false_patch, text->len);
  return true;
}

static bool coff_emit_while_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  size_t loop_start = text->len;
  if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_test_rax_rax(text, false);
  size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  ZDirectLoopFrame frame = {.continue_target = loop_start};
  ZDirectLoopFrame *parent = ctx->loop;
  ctx->loop = &frame;
  bool body_ok = coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag);
  ctx->loop = parent;
  if (!body_ok) {
    free(frame.break_patches);
    return false;
  }
  size_t loop_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, loop_patch, loop_start);
  z_x64_patch_rel32(text, false_patch, text->len);
  for (size_t i = 0; i < frame.break_len; i++) z_x64_patch_rel32(text, frame.break_patches[i], text->len);
  free(frame.break_patches);
  return true;
}

static bool coff_emit_loop_exit_instr(ZBuf *text, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (!ctx->loop) return coff_diag_at(diag, "direct COFF x64 break or continue requires an enclosing loop", instr->line, instr->column, instr->kind == IR_INSTR_BREAK ? "break" : "continue");
  size_t patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  if (instr->kind == IR_INSTR_CONTINUE) z_x64_patch_rel32(text, patch, ctx->loop->continue_target);
  else if (!z_direct_loop_frame_add_break(ctx->loop, patch)) return coff_diag_at(diag, "direct COFF x64 break patch list allocation failed", instr->line, instr->column, "out of memory");
  return true;
}

static bool coff_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  switch (instr->kind) {
    case IR_INSTR_WORLD_WRITE: return coff_emit_world_write(text, fun, instr, ctx, diag);
    case IR_INSTR_LOCAL_SET: return coff_emit_local_set_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_FIELD_STORE: return coff_emit_field_store_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_INDEX_STORE: return coff_emit_index_store_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_EXPR: return !instr->value || coff_emit_value(text, fun, instr->value, ctx, diag);
    case IR_INSTR_RETURN:
      if (fun->return_type == IR_TYPE_BYTE_VIEW && instr->value) {
        if ((instr->value->kind == IR_VALUE_CALL || instr->value->kind == IR_VALUE_STR_RUNTIME) && instr->value->type == IR_TYPE_BYTE_VIEW) {
          if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
        } else {
          if (!coff_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
        }
        coff_emit_epilogue(text);
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
             instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
             instr->value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN) && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
          if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
        } else if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
          if (!instr->value->data_len) {
            z_x64_emit_xor_eax_eax(text);
            z_x64_emit_xor_reg_reg(text, 2, true);
            z_x64_emit_xor_ecx_ecx(text);
          } else {
            if (!coff_emit_byte_view_pair(text, fun, instr->value->left, 2, 1, ctx, diag)) return false;
            z_x64_emit_mov_eax_u32(text, 1);
          }
        } else {
          return coff_diag_at(diag, "direct COFF Maybe byte-view return requires a Maybe byte-view value", instr->line, instr->column, "unsupported Maybe byte-view return");
        }
        coff_emit_epilogue(text);
        return true;
      }
      if (fun->return_type == IR_TYPE_MAYBE_SCALAR && instr->value) {
        if ((instr->value->kind == IR_VALUE_CALL ||
             instr->value->kind == IR_VALUE_VEC_GET ||
             instr->value->kind == IR_VALUE_ASCII_RUNTIME ||
             instr->value->kind == IR_VALUE_TEXT_RUNTIME ||
             instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
             instr->value->kind == IR_VALUE_PARSE_I32 ||
             instr->value->kind == IR_VALUE_PARSE_U32 ||
             instr->value->kind == IR_VALUE_RAND_NEXT_BELOW ||
             instr->value->kind == IR_VALUE_RAND_RANGE_U32 ||
             instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR ||
             instr->value->kind == IR_VALUE_PROC_CAPTURE ||
             instr->value->kind == IR_VALUE_TERM_RUNTIME ||
             instr->value->kind == IR_VALUE_MATH_RUNTIME) && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
          if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
        } else if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
          z_x64_emit_mov_rax_u64(text, (uint64_t)instr->value->int_value);
          z_x64_emit_mov_reg_from_rax(text, 2, true);
          z_x64_emit_mov_eax_u32(text, instr->value->data_len ? 1u : 0u);
        } else if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len && fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
          coff_emit_load_local_slot_rax(text, fun, instr->value->local_index, 8);
          z_x64_emit_mov_reg_from_rax(text, 2, true);
          coff_emit_load_local_slot_eax(text, fun, instr->value->local_index, 0);
        } else if (coff_type_is_scalar32(instr->value->type) || coff_type_is_i64(instr->value->type)) {
          if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
          z_x64_emit_mov_reg_from_rax(text, 2, true);
          z_x64_emit_mov_eax_u32(text, 1);
        } else {
          return coff_diag_at(diag, "direct COFF Maybe scalar return requires a Maybe scalar or scalar value", instr->line, instr->column, "unsupported Maybe scalar return");
        }
        coff_emit_epilogue(text);
        return true;
      }
      if (instr->value && !coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
      coff_emit_epilogue(text);
      return true;
    case IR_INSTR_IF: return coff_emit_if_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_WHILE: return coff_emit_while_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_BREAK:
    case IR_INSTR_CONTINUE: return coff_emit_loop_exit_instr(text, instr, ctx, diag);
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
      return coff_diag_at(diag, "direct COFF instruction kind is unsupported", instr->line, instr->column, actual);
    }
  }
}

// Register-only fill loop replacing an unrolled run of constant-index,
// constant-value array stores. rdx holds the running element pointer, rcx the
// fill value, rax the remaining count.
static void coff_emit_fill_run(ZBuf *text, const IrFunction *fun, const ZDirectFillRun *run) {
  unsigned elem_size = coff_type_byte_size(run->element_type);
  coff_emit_array_base_rdx(text, fun, run->array_index);
  z_x64_emit_mov_reg_u64(text, 1, run->fill_value);
  z_x64_emit_mov_reg_u64(text, 0, run->count);
  size_t loop = text->len;
  coff_emit_store_ptr_element(text, 2, 1, run->element_type);
  z_x64_emit_add_reg_i8(text, 2, (int8_t)elem_size, true);
  z_x64_emit_add_reg_i8(text, 0, -1, true);
  size_t back = z_x64_emit_jcc32_placeholder(text, 0x85); // jnz -> loop
  z_x64_patch_rel32(text, back, loop);
}

static bool coff_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, CoffEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    ZDirectFillRun run;
    if (z_direct_detect_fill_run(fun, instrs, len, i, COFF_FILL_RUN_MIN, &run)) {
      coff_emit_fill_run(text, fun, &run);
      i += run.count - 1;
      continue;
    }
    if (!coff_emit_instr(text, fun, &instrs[i], ctx, diag)) return false;
  }
  return true;
}

static bool coff_validate_function(const IrFunction *fun, ZDiag *diag) {
  size_t abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) abi_slots += fun->locals[i].type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
  if (abi_slots > 8) return coff_diag_at(diag, "direct COFF object backend supports at most eight ABI parameter slots", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !coff_type_is_scalar32(fun->return_type) && !coff_type_is_i64(fun->return_type) &&
      fun->return_type != IR_TYPE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_SCALAR) {
    return coff_diag_at(diag, "direct COFF object backend currently supports Void, integer, byte-view, Maybe byte-view, and Maybe scalar returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      continue;
    }
    if (fun->locals[i].is_array && coff_type_is_array_element(fun->locals[i].element_type)) continue;
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW || fun->locals[i].type == IR_TYPE_MAYBE_SCALAR) continue;
    if (fun->locals[i].type == IR_TYPE_VEC) continue;
    if (fun->locals[i].is_array || (!coff_type_is_scalar32(fun->locals[i].type) && !coff_type_is_i64(fun->locals[i].type))) {
      return coff_diag_at(diag, "direct COFF object backend currently supports only primitive scalar locals", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
    }
  }
  return true;
}

static bool coff_emit_function_text(ZBuf *text, const IrFunction *fun, CoffEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {1, 2, 8, 9};
  unsigned frame_size = (unsigned)z_coff_align(fun ? fun->frame_bytes : 0, 16);
  z_x64_emit_prologue(text, frame_size);
  size_t abi_slot = 0;
  for (size_t i = 0; i < fun->param_count; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      if (abi_slot < 4) {
        coff_emit_store_local_slot_from_reg(text, fun, (unsigned)i, param_regs[abi_slot], 0, true);
      } else {
        z_x64_emit_load_rbp_positive_reg(text, 0, 48u + (unsigned)(abi_slot - 4u) * 8u, true);
        coff_emit_store_local_slot_from_reg(text, fun, (unsigned)i, 0, 0, true);
      }
      abi_slot++;
      if (abi_slot < 4) {
        coff_emit_store_local_slot_from_reg(text, fun, (unsigned)i, param_regs[abi_slot], 8, false);
      } else {
        z_x64_emit_load_rbp_positive_reg(text, 0, 48u + (unsigned)(abi_slot - 4u) * 8u, true);
        coff_emit_store_local_slot_from_reg(text, fun, (unsigned)i, 0, 8, false);
      }
      abi_slot++;
      continue;
    }
    if (abi_slot < 4) coff_emit_store_local_scalar_from_reg(text, fun, (unsigned)i, param_regs[abi_slot], fun->locals[i].type);
    else {
      z_x64_emit_load_rbp_positive_reg(text, 0, 48u + (unsigned)(abi_slot - 4u) * 8u, coff_type_is_i64(fun->locals[i].type));
      coff_emit_store_local_scalar_from_reg(text, fun, (unsigned)i, 0, fun->locals[i].type);
    }
    abi_slot++;
  }
  if (!coff_emit_instrs(text, fun, fun->instrs, fun->instr_len, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) coff_emit_epilogue(text);
  return true;
}

static unsigned coff_rodata_base_offset(const IrProgram *program) {
  if (!program || program->data_segment_len == 0) return 0;
  unsigned base = program->data_segments[0].offset;
  for (size_t i = 1; i < program->data_segment_len; i++) {
    if (program->data_segments[i].offset < base) base = program->data_segments[i].offset;
  }
  return base;
}

static void coff_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    while (rodata->len < segment->offset - base_offset) z_coff_append_u8(rodata, 0);
    z_coff_append_bytes(rodata, (const char *)segment->bytes, segment->len);
  }
}

static void coff_append_trap_messages(ZBuf *rodata, unsigned base_offset, ZDirectTrapMessages *messages) {
  for (unsigned kind = 0; kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    const char *text = z_direct_trap_message((ZDirectTrapKind)kind);
    size_t len = strlen(text);
    messages->offsets[kind] = base_offset + (unsigned)rodata->len;
    messages->lens[kind] = (unsigned)len;
    for (size_t i = 0; i < len; i++) zbuf_append_char(rodata, text[i]);
  }
}

static bool coff_object_emit_text(ZBuf *text, const IrProgram *program, size_t *offsets, CoffEmitContext *ctx, bool *trap_stubs_emitted, ZDiag *diag) {
  for (size_t i = 0; i < program->function_len; i++) {
    while (text->len % 16 != 0) z_x64_append_u8(text, 0x90);
    offsets[i] = text->len;
    if (!coff_emit_function_text(text, &program->functions[i], ctx, diag)) return false;
  }
  return coff_emit_trap_stubs(text, ctx, trap_stubs_emitted, diag);
}

bool z_emit_coff_x64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return coff_diag(diag, "direct COFF backend received no program");
  if (!program->mir_valid) {
    bool ok = coff_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (program->function_len == 0) return coff_diag_at(diag, "direct COFF object backend requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (program->functions[i].is_exported) has_export = true;
    if (!coff_validate_function(&program->functions[i], diag)) return false;
  }
  if (!has_export) return coff_diag_at(diag, "direct COFF object backend requires at least one exported function", 1, 1, "no exported function");

  ZBuf text;
  ZBuf rodata;
  ZBuf relocs;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&relocs);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = coff_rodata_base_offset(program);
  ZDirectTrapMessages trap_messages = {0};
  coff_append_rodata(&rodata, program, rodata_base_offset);
  coff_append_trap_messages(&rodata, rodata_base_offset, &trap_messages);
  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!offsets) {
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return coff_diag(diag, "out of memory while emitting COFF object");
  }
  CoffEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .trap_messages = trap_messages
  };
  bool trap_stubs_emitted = false;
  if (!coff_object_emit_text(&text, program, offsets, &ctx, &trap_stubs_emitted, diag)) {
    z_coff_emit_context_free(&ctx);
    free(offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return false;
  }
  has_rodata = has_rodata || trap_stubs_emitted;
  unsigned section_symbol_count = has_rodata ? 2u : 1u;
  size_t runtime_symbol_count = 0;
  for (unsigned helper = 0; helper < COFF_RUNTIME_HELPER_COUNT; helper++) {
    if (z_coff_runtime_patch_count(&ctx, (CoffRuntimeHelper)helper) > 0) runtime_symbol_count++;
  }
  size_t external_symbol_base = section_symbol_count + program->function_len + runtime_symbol_count;
  size_t symbol_len = program->function_len + runtime_symbol_count + program->external_function_len;
  ZCoffSymbol *symbols = z_checked_calloc(symbol_len, sizeof(ZCoffSymbol));
  if (!symbols) {
    z_coff_emit_context_free(&ctx);
    free(offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return coff_diag(diag, "out of memory while emitting COFF object");
  }
  z_coff_patch_call_patches(&text, &ctx);
  z_coff_append_call_relocations(&relocs, &ctx, section_symbol_count, (uint32_t)external_symbol_base);
  z_coff_append_rodata_relocations(&relocs, &ctx, 1u);
  uint32_t runtime_symbol_index = section_symbol_count + (uint32_t)program->function_len;
  for (unsigned helper = 0; helper < COFF_RUNTIME_HELPER_COUNT; helper++) {
    CoffRuntimeHelper runtime_helper = (CoffRuntimeHelper)helper;
    if (z_coff_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    z_coff_append_runtime_relocations(&relocs, &ctx, runtime_helper, runtime_symbol_index++);
  }

  for (size_t i = 0; i < program->function_len; i++) {
    symbols[i] = (ZCoffSymbol){
      .name = program->functions[i].name ? program->functions[i].name : "zero_fn",
      .value = (uint32_t)offsets[i],
      .section_number = 1,
      .type = 0x20,
      .storage_class = program->functions[i].is_exported ? Z_COFF_SYMBOL_EXTERNAL : Z_COFF_SYMBOL_STATIC
    };
  }
  size_t symbol_index = program->function_len;
  for (unsigned helper = 0; helper < COFF_RUNTIME_HELPER_COUNT; helper++) {
    CoffRuntimeHelper runtime_helper = (CoffRuntimeHelper)helper;
    if (z_coff_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    symbols[symbol_index++] = (ZCoffSymbol){
      .name = z_coff_runtime_helper_symbol(runtime_helper),
      .section_number = 0,
      .type = 0x20,
      .storage_class = Z_COFF_SYMBOL_EXTERNAL
    };
  }
  for (size_t i = 0; i < program->external_function_len; i++) {
    symbols[symbol_index + i] = (ZCoffSymbol){
      .name = program->external_functions[i].symbol ? program->external_functions[i].symbol : "zero_external",
      .section_number = 0,
      .type = 0x20,
      .storage_class = Z_COFF_SYMBOL_EXTERNAL
    };
  }
  ZCoffObjectImage image = {
    .machine = Z_COFF_MACHINE_AMD64,
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .text_relocs = &relocs,
    .text_reloc_count = (uint16_t)z_coff_text_relocation_count(&ctx),
    .symbols = symbols,
    .symbol_len = symbol_len
  };
  z_coff_write_object(out, &image);

  z_coff_emit_context_free(&ctx);
  free(offsets);
  free(symbols);
  zbuf_free(&relocs);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}

static const IrFunction *coff_find_executable_main(const IrProgram *program, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    if (program->functions[i].is_exported && strcmp(program->functions[i].name, "main") == 0) {
      if (fun) {
        coff_diag_at(diag, "direct COFF x64 executable backend requires exactly one exported main function", program->functions[i].line, program->functions[i].column, program->functions[i].name);
        return NULL;
      }
      fun = &program->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    coff_diag_at(diag, "direct COFF x64 executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (fun->param_count != 0) {
    coff_diag_at(diag, "direct COFF x64 executable main must not take parameters", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (fun->return_type != IR_TYPE_VOID && !coff_type_is_scalar32(fun->return_type) && !coff_type_is_i64(fun->return_type)) {
    coff_diag_at(diag, "direct COFF x64 executable main must return Void or a primitive scalar", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

static void coff_emit_import_call(ZBuf *text, ZCoffImportPatch *patches, size_t *patch_len, unsigned import_index) {
  size_t patch = z_x64_emit_call_rip32_placeholder(text);
  if (patches && patch_len && *patch_len < 8) {
    patches[*patch_len] = (ZCoffImportPatch){.patch_offset = patch, .import_index = import_index};
    *patch_len += 1;
  }
}

static size_t coff_emit_exe_start_stub(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  z_x64_emit_sub_rsp(text, 40);
  size_t main_patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_mov_rcx_from_rax(text, false);
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_EXIT_PROCESS);
  z_x64_append_u8(text, 0xcc);
  return main_patch;
}

static size_t coff_emit_exe_world_write(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  size_t offset = text->len;
  z_x64_emit_sub_rsp(text, 72);
  z_x64_emit_store_rsp_offset_reg(text, 2, 40, true);
  z_x64_emit_store_rsp_offset_reg(text, 8, 48, true);
  z_x64_emit_mov_rsp_offset_u32(text, 56, 0, false); // DWORD bytes_written = 0
  z_x64_emit_cmp_reg_i8(text, 1, 2, false); // cmp ecx, 2
  z_x64_emit_mov_reg_u32(text, 1, 0xfffffff5u); // STD_OUTPUT_HANDLE
  z_x64_append_u8(text, 0x75);
  z_x64_append_u8(text, 0x05); // jne after stderr handle
  z_x64_emit_mov_reg_u32(text, 1, 0xfffffff4u); // STD_ERROR_HANDLE
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_GET_STD_HANDLE);
  z_x64_emit_mov_rcx_from_rax(text, true);
  z_x64_emit_load_rsp_offset_reg(text, 2, 40, true);
  z_x64_emit_load_rsp_offset_reg(text, 8, 48, true);
  z_x64_emit_lea_rsp_offset_reg(text, 9, 56);
  z_x64_emit_mov_rsp_offset_u32(text, 32, 0, true); // lpOverlapped = NULL
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_WRITE_FILE);
  z_x64_emit_xor_eax_eax(text);
  z_x64_emit_add_rsp(text, 72);
  z_x64_append_u8(text, 0xc3);
  return offset;
}

bool z_emit_coff_x64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return coff_diag(diag, "direct COFF executable backend received no program");
  if (!program->mir_valid) {
    bool ok = coff_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (!z_direct_exe_reject_c_import_calls(program, diag, "COFF x64")) return false;
  unsigned main_index = 0;
  if (!coff_find_executable_main(program, diag, &main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (!coff_validate_function(&program->functions[i], diag)) return false;
  }

  ZBuf text;
  ZBuf rdata;
  zbuf_init(&text);
  zbuf_init(&rdata);
  unsigned rodata_base_offset = coff_rodata_base_offset(program);
  ZDirectTrapMessages trap_messages = {0};
  coff_append_rodata(&rdata, program, rodata_base_offset);
  coff_append_trap_messages(&rdata, rodata_base_offset, &trap_messages);

  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!offsets) {
    zbuf_free(&rdata);
    zbuf_free(&text);
    return coff_diag(diag, "out of memory while emitting COFF executable");
  }

  CoffEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .trap_messages = trap_messages
  };
  ZCoffImportPatch import_patches[8];
  size_t import_patch_len = 0;
  size_t start_main_patch = coff_emit_exe_start_stub(&text, import_patches, &import_patch_len);
  while (text.len % 16 != 0) z_x64_append_u8(&text, 0x90);
  for (size_t i = 0; i < program->function_len; i++) {
    while (text.len % 16 != 0) z_x64_append_u8(&text, 0x90);
    offsets[i] = text.len;
    if (!coff_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      z_coff_emit_context_free(&ctx);
      free(offsets);
      zbuf_free(&rdata);
      zbuf_free(&text);
      return false;
    }
  }
  if (!coff_emit_trap_stubs(&text, &ctx, NULL, diag)) {
    z_coff_emit_context_free(&ctx);
    free(offsets);
    zbuf_free(&rdata);
    zbuf_free(&text);
    return false;
  }
  size_t world_write_offset = 0;
  if (z_coff_runtime_patch_count(&ctx, COFF_RUNTIME_WORLD_WRITE) > 0) {
    while (text.len % 16 != 0) z_x64_append_u8(&text, 0x90);
    world_write_offset = coff_emit_exe_world_write(&text, import_patches, &import_patch_len);
  }

  z_x64_patch_rel32(&text, start_main_patch, offsets[main_index]);
  z_coff_patch_call_patches(&text, &ctx);
  z_coff_patch_runtime_patches(&text, &ctx, COFF_RUNTIME_WORLD_WRITE, world_write_offset);

  ZCoffExecutableImage image = {
    .machine = Z_COFF_MACHINE_AMD64,
    .image_base = 0x140000000ull,
    .section_alignment = 0x1000,
    .file_alignment = 0x200,
    .text = &text,
    .rdata = &rdata,
    .rodata_base_offset = rodata_base_offset,
    .rodata_patches = ctx.rodata_patches,
    .rodata_patch_len = ctx.rodata_patch_len,
    .import_patches = import_patches,
    .import_patch_len = import_patch_len
  };
  z_coff_write_pe64_executable(out, &image);

  z_coff_emit_context_free(&ctx);
  free(offsets);
  zbuf_free(&rdata);
  zbuf_free(&text);
  return true;
}
