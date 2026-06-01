#include "zero.h"
#include "aarch64_emit.h"
#include "macho_emit_state.h"
#include "macho_format.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

#define MACHO_SCRATCH_SLOT_COUNT 32u
#define MACHO_SCRATCH_SLOT_BYTES 8u

static void append_bytes(ZBuf *buf, const char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) append_u8(buf, (unsigned char)bytes[i]);
}

static bool macho_diag_at(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct AArch64 Mach-O object MVP subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported no-parameter functions returning small integer literals");
  }
  return false;
}

static bool macho_diag(ZDiag *diag, const char *message) { return macho_diag_at(diag, message, 1, 1, "unsupported feature"); }

static bool macho_return_literal(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun || fun->param_count != 0) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports exported functions without parameters", fun ? fun->line : 1, fun ? fun->column : 1, fun ? fun->name : "missing function");
  }
  if (fun->return_type != IR_TYPE_U8 && fun->return_type != IR_TYPE_I32 && fun->return_type != IR_TYPE_U32 && fun->return_type != IR_TYPE_USIZE) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports primitive 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->instr_len; i++) {
    const IrInstr *instr = &fun->instrs[i];
    if (instr->kind != IR_INSTR_RETURN || !instr->value || instr->value->kind != IR_VALUE_INT || instr->value->int_value > 65535) continue;
    *out = (uint32_t)instr->value->int_value;
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently requires a small integer literal return", fun->line, fun->column, fun->name);
}

static bool macho_is_literal_return_function(const IrFunction *fun, uint32_t *out, ZDiag *diag) { return fun && fun->local_len == 0 && fun->instr_len == 1 && macho_return_literal(fun, out, diag); }

static size_t macho_align(size_t value, size_t alignment) { return z_macho_align(value, alignment); }
static void macho_pad_to(ZBuf *buf, size_t offset) { z_macho_pad_to(buf, offset); }

static bool macho_type_is_scalar32(IrTypeKind type) { return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32; }
static bool macho_type_is_scalar64(IrTypeKind type) { return type == IR_TYPE_I64 || type == IR_TYPE_USIZE || type == IR_TYPE_U64; }
static bool macho_type_is_unsigned(IrTypeKind type) { return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_U32 || type == IR_TYPE_U64; }
static bool macho_type_is_scalar(IrTypeKind type) { return macho_type_is_scalar32(type) || macho_type_is_scalar64(type); }
static IrTypeKind macho_view_element_type(const IrValue *view) { return view && view->element_type != IR_TYPE_UNSUPPORTED ? view->element_type : IR_TYPE_U8; }

static unsigned macho_type_index_shift(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 0;
  if (type == IR_TYPE_U16) return 1;
  if (macho_type_is_scalar64(type)) return 3;
  return 2;
}

static void macho_emit_add_scaled_index(ZBuf *text, unsigned dst, unsigned base, unsigned index, IrTypeKind element_type) {
  unsigned shift = macho_type_index_shift(element_type);
  if (shift == 0) z_aarch64_emit_add_x_reg(text, dst, base, index);
  else z_aarch64_emit_add_x_reg_lsl(text, dst, base, index, shift);
}

static void macho_emit_load_ptr_element(ZBuf *text, unsigned dst, unsigned base, IrTypeKind element_type) {
  if (element_type == IR_TYPE_U8 || element_type == IR_TYPE_BOOL) z_aarch64_emit_load_b_imm(text, dst, base, 0);
  else if (element_type == IR_TYPE_U16) z_aarch64_emit_load_h_imm(text, dst, base, 0);
  else if (macho_type_is_scalar64(element_type)) z_aarch64_emit_load_x_imm(text, dst, base, 0);
  else z_aarch64_emit_load_w_imm(text, dst, base, 0);
}

static void macho_emit_store_ptr_element(ZBuf *text, unsigned src, unsigned base, IrTypeKind element_type) {
  if (element_type == IR_TYPE_U8 || element_type == IR_TYPE_BOOL) z_aarch64_emit_store_b_imm(text, src, base, 0);
  else if (element_type == IR_TYPE_U16) z_aarch64_emit_store_h_imm(text, src, base, 0);
  else if (macho_type_is_scalar64(element_type)) z_aarch64_emit_store_x_imm(text, src, base, 0);
  else z_aarch64_emit_store_w_imm(text, src, base, 0);
}

static void macho_emit_scale_len_for_element(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  unsigned shift = macho_type_index_shift(element_type);
  if (shift == 0) return;
  z_aarch64_emit_movz_w(text, 14, 1u << shift);
  z_aarch64_emit_mul_w_reg(text, reg, reg, 14);
}

static bool macho_is_main_function(const IrFunction *fun) { return fun && fun->is_exported && fun->name && strcmp(fun->name, "main") == 0; }

static bool macho_function_propagates_to_process_exit(const IrFunction *fun) { return fun && (fun->raises || (macho_is_main_function(fun) && fun->return_type == IR_TYPE_I32 && fun->value_return_type == IR_TYPE_VOID)); }

static unsigned macho_abi_slots_for_param(const IrLocal *local) { return local && local->type == IR_TYPE_BYTE_VIEW ? 2u : 1u; }

static void macho_emit_cast_normalize_reg(ZBuf *text, unsigned reg, IrTypeKind source, IrTypeKind target) {
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
      else if (!macho_type_is_scalar64(source)) z_aarch64_emit_mov_w(text, reg, reg);
      return;
    default:
      return;
  }
}

static unsigned macho_slot_offset(unsigned local_index) { return local_index * 8; }

static unsigned macho_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0 && frame_size >= fun->locals[local_index].frame_offset) {
    return frame_size - fun->locals[local_index].frame_offset + slot_offset;
  }
  return MACHO_SCRATCH_SLOT_COUNT * MACHO_SCRATCH_SLOT_BYTES + macho_slot_offset(local_index) + slot_offset;
}

static void macho_emit_load_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_load_w_sp(text, reg, offset);
}

static void macho_emit_load_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_load_x_sp(text, reg, offset);
}

static void macho_emit_store_local_w(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_store_w_sp(text, reg, offset);
}

static void macho_emit_store_local_x(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_store_x_sp(text, reg, offset);
}

static void macho_emit_load_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_load_b_sp(text, reg, offset);
}

static void macho_emit_store_local_b(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_store_b_sp(text, reg, offset);
}

static void macho_emit_load_local_h(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_load_h_imm(text, reg, 31, offset);
}

static void macho_emit_store_local_h(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned slot_offset, unsigned frame_size) {
  unsigned offset = macho_local_slot_offset(fun, local_index, slot_offset, frame_size);
  z_aarch64_emit_store_h_imm(text, reg, 31, offset);
}

static bool macho_scratch_slot(unsigned slot, unsigned *offset, const IrValue *value, ZDiag *diag) {
  if (slot >= MACHO_SCRATCH_SLOT_COUNT) {
    return macho_diag_at(diag, "direct AArch64 Mach-O expression nesting exceeds scratch register spill capacity", value ? value->line : 1, value ? value->column : 1, "expression too deep");
  }
  *offset = slot * MACHO_SCRATCH_SLOT_BYTES;
  return true;
}

static bool macho_emit_store_scratch(ZBuf *text, unsigned reg, IrTypeKind type, unsigned slot, const IrValue *value, ZDiag *diag) {
  unsigned offset = 0;
  if (!macho_scratch_slot(slot, &offset, value, diag)) return false;
  if (macho_type_is_scalar64(type)) z_aarch64_emit_store_x_sp(text, reg, offset);
  else z_aarch64_emit_store_w_sp(text, reg, offset);
  return true;
}

static bool macho_emit_load_scratch(ZBuf *text, unsigned reg, IrTypeKind type, unsigned slot, const IrValue *value, ZDiag *diag) {
  unsigned offset = 0;
  if (!macho_scratch_slot(slot, &offset, value, diag)) return false;
  if (macho_type_is_scalar64(type)) z_aarch64_emit_load_x_sp(text, reg, offset);
  else z_aarch64_emit_load_w_sp(text, reg, offset);
  return true;
}

static void macho_emit_load_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    macho_emit_load_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else if (type == IR_TYPE_U16) {
    macho_emit_load_local_h(text, fun, reg, local_index, field_offset, frame_size);
  } else if (macho_type_is_scalar64(type)) {
    macho_emit_load_local_x(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    macho_emit_load_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void macho_emit_store_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    macho_emit_store_local_b(text, fun, reg, local_index, field_offset, frame_size);
  } else if (type == IR_TYPE_U16) {
    macho_emit_store_local_h(text, fun, reg, local_index, field_offset, frame_size);
  } else if (macho_type_is_scalar64(type)) {
    macho_emit_store_local_x(text, fun, reg, local_index, field_offset, frame_size);
  } else {
    macho_emit_store_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void macho_emit_binary_reg(ZBuf *text, IrBinaryOp op, unsigned dst, unsigned lhs, unsigned rhs, bool wide) {
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

static void macho_emit_u32_bounds_check(ZBuf *text, unsigned index_reg, unsigned len_reg) {
  z_aarch64_emit_cmp_w(text, index_reg, len_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
}

static void macho_emit_u32_upper_bound_check(ZBuf *text, unsigned value_reg, unsigned max_reg) {
  z_aarch64_emit_cmp_w(text, value_reg, max_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 9);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
}

static bool macho_scaled_index_imm12(unsigned start, IrTypeKind element_type, unsigned *out) {
  unsigned shift = macho_type_index_shift(element_type);
  if (start > (4095u >> shift)) return false;
  if (out) *out = start << shift;
  return true;
}

static void macho_emit_packed_error_reg(ZBuf *text, unsigned reg, unsigned code_value) {
  z_aarch64_emit_movz_x(text, reg, ((uint64_t)code_value) << 32);
}

static void macho_emit_error_condition_reg(ZBuf *text, unsigned condition_reg, unsigned packed_reg) {
  z_aarch64_emit_lsr_x_imm(text, condition_reg, packed_reg, 32);
  z_aarch64_emit_cmp_x(text, condition_reg, 31);
}

static bool macho_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static unsigned macho_cond_for_compare(IrCompareOp op, bool uns) {
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

static unsigned macho_invert_cond(unsigned cond) { return cond ^ 1u; }

static MachORuntimeHelper macho_runtime_helper_for_value(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_HTTP_RESULT_OK: return MACHO_RUNTIME_HTTP_RESULT_OK;
    case IR_VALUE_HTTP_RESULT_STATUS: return MACHO_RUNTIME_HTTP_RESULT_STATUS;
    case IR_VALUE_HTTP_RESULT_BODY_LEN: return MACHO_RUNTIME_HTTP_RESULT_BODY_LEN;
    case IR_VALUE_HTTP_RESULT_ERROR: return MACHO_RUNTIME_HTTP_RESULT_ERROR;
    case IR_VALUE_HTTP_RESPONSE_LEN: return MACHO_RUNTIME_HTTP_RESPONSE_LEN;
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN: return MACHO_RUNTIME_HTTP_RESPONSE_HEADERS_LEN;
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: return MACHO_RUNTIME_HTTP_RESPONSE_BODY_OFFSET;
    case IR_VALUE_HTTP_HEADER_VALUE: return MACHO_RUNTIME_HTTP_HEADER_VALUE;
    case IR_VALUE_HTTP_HEADER_FOUND: return MACHO_RUNTIME_HTTP_HEADER_FOUND;
    case IR_VALUE_HTTP_HEADER_OFFSET: return MACHO_RUNTIME_HTTP_HEADER_OFFSET;
    case IR_VALUE_HTTP_HEADER_LEN: return MACHO_RUNTIME_HTTP_HEADER_LEN;
    default: return MACHO_RUNTIME_HELPER_COUNT;
  }
}

static bool macho_readonly_data_byte(const IrProgram *program, unsigned offset, unsigned char *out) {
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

static bool macho_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!macho_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !macho_const_u32_value(view->index, &start)) return false;
    if (view->right && !macho_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static bool macho_byte_view_const_byte(const IrProgram *program, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    return macho_readonly_data_byte(program, view->data_offset + index, out);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned len = 0;
    unsigned start = 0;
    if (!macho_byte_view_const_len(view, &len) || index >= len) return false;
    if (view->index && !macho_const_u32_value(view->index, &start)) return false;
    return macho_byte_view_const_byte(program, view->left, start + index, out);
  }
  return false;
}

static bool macho_emit_rodata_ptr_literal(ZBuf *text, unsigned reg, unsigned data_offset, MachOEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  if (ctx && ctx->pie_relative_data) {
    size_t patch_offset = text->len;
    z_aarch64_emit_adrp_add_placeholder(text, reg);
    return z_macho_record_data_patch(ctx, patch_offset, data_offset, value, diag);
  }
  while (((text->len + 8) % 8) != 0) z_aarch64_emit_nop(text);
  z_aarch64_emit_ldr_x_literal8(text, reg);
  z_aarch64_emit_b_offset_words(text, 3);
  size_t patch_offset = text->len;
  z_aarch64_append_u64(text, data_offset - (ctx ? ctx->rodata_base_offset : 0));
  return z_macho_record_data_patch(ctx, patch_offset, data_offset, value, diag);
}

static bool macho_emit_byte_view_ptr_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_byte_view_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) { return macho_emit_byte_view_pair_at(text, fun, view, ptr_reg, len_reg, frame_size, 0, ctx, diag); }
static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_value_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) { return macho_emit_value_to_reg_at(text, fun, value, reg, frame_size, 0, ctx, diag); }
static void macho_emit_epilogue(ZBuf *text, unsigned frame_size, bool restore_process_args);

static void macho_emit_move_byte_view_pair(ZBuf *text, unsigned ptr_reg, unsigned len_reg, unsigned src_ptr_reg, unsigned src_len_reg) {
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

static bool macho_emit_json_parse_bytes_call_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_PARSE_BYTES, patch, value, diag);
}

static bool macho_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  unsigned const_len = 0;
  if (view->kind == IR_VALUE_BYTE_SLICE && macho_byte_view_const_len(view, &const_len)) {
    if (const_len > 65535) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length is too large for the current MVP", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, const_len);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > 65535) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length is too large for the current MVP", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 16, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 1) z_aarch64_emit_mov_w(text, reg, 1);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned ptr_tmp = reg == 11 ? 12 : 11;
    return macho_emit_byte_view_pair_at(text, fun, view, ptr_tmp, reg, frame_size, scratch_slot, ctx, diag);
  }
  (void)ctx;
  return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

static bool macho_emit_byte_view_ptr_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 0, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!((local->is_array && view->field_offset == 0) || local->is_record)) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view array requires a fixed array or record array field", view->line, view->column, "unsupported array view");
    z_aarch64_emit_add_x_sp_imm(text, reg, macho_local_slot_offset(fun, view->array_index, view->field_offset, frame_size));
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    return macho_emit_rodata_ptr_literal(text, reg, view->data_offset, ctx, view, diag);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!macho_emit_byte_view_ptr_at(text, fun, view->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    if (!view->index) return true;
    if (macho_const_u32_value(view->index, &start)) {
      unsigned byte_start = 0;
      if (!macho_scaled_index_imm12(start, macho_view_element_type(view), &byte_start)) return macho_diag_at(diag, "direct AArch64 Mach-O byte slice constant start is too large", view->line, view->column, "unsupported byte slice");
      if (byte_start > 0) z_aarch64_emit_add_x_imm(text, reg, reg, byte_start);
      return true;
    }
    unsigned tmp = reg == 8 ? 9 : 8;
    if (!macho_emit_store_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    if (!macho_emit_value_to_reg_at(text, fun, view->index, tmp, frame_size, scratch_slot + 1, ctx, diag)) return false;
    if (!macho_emit_load_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    macho_emit_add_scaled_index(text, reg, reg, tmp, macho_view_element_type(view));
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool macho_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE) {
    if (!view->index && !view->right) return macho_emit_byte_view_pair_at(text, fun, view->left, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
    if (!macho_emit_byte_view_pair_at(text, fun, view->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, view->left, diag)) return false;
    if (!macho_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
    if (view->index) {
      if (!macho_emit_value_to_reg_at(text, fun, view->index, 8, frame_size, scratch_slot + 2, ctx, diag)) return false;
    } else {
      z_aarch64_emit_movz_w(text, 8, 0);
    }
    if (!macho_emit_store_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
    if (view->right) {
      if (!macho_emit_value_to_reg_at(text, fun, view->right, 9, frame_size, scratch_slot + 3, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      if (!macho_emit_load_scratch(text, 12, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      macho_emit_u32_upper_bound_check(text, 8, 9);
      macho_emit_u32_upper_bound_check(text, 9, 12);
      macho_emit_binary_reg(text, IR_BIN_SUB, 10, 9, 8, false);
    } else {
      if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      macho_emit_u32_upper_bound_check(text, 8, 10);
      macho_emit_binary_reg(text, IR_BIN_SUB, 10, 10, 8, false);
    }
    if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, view->left, diag)) return false;
    if (!macho_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
    macho_emit_add_scaled_index(text, 11, 11, 8, macho_view_element_type(view));
    macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 11, 10);
    return true;
  }
  if (!macho_emit_byte_view_ptr_at(text, fun, view, ptr_reg, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
  if (!macho_emit_byte_view_len_at(text, fun, view, len_reg, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
  return true;
}

static bool macho_emit_call_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  const IrFunction *callee = ctx && ctx->program && value->callee_index < ctx->program->function_len ? &ctx->program->functions[value->callee_index] : NULL;
  if (!callee) return macho_diag_at(diag, "direct AArch64 Mach-O call target is unavailable", value->line, value->column, "invalid callee");
  unsigned abi_slots = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (i >= callee->param_count) return macho_diag_at(diag, "direct AArch64 Mach-O call parameter metadata is unavailable", value->line, value->column, "invalid callee parameter");
    unsigned slots = macho_abi_slots_for_param(&callee->locals[i]);
    if (abi_slots + slots > 8) return macho_diag_at(diag, "direct AArch64 Mach-O call supports at most eight ABI argument slots", value->line, value->column, "too many arguments");
    abi_slots += slots;
  }
  if (scratch_slot + abi_slots >= MACHO_SCRATCH_SLOT_COUNT) {
    return macho_diag_at(diag, "direct AArch64 Mach-O call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
  }
  unsigned nested_slot = scratch_slot + abi_slots;
  unsigned arg_slot = scratch_slot;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    const IrLocal *param = &callee->locals[i];
    if (param->type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_byte_view_pair_at(text, fun, arg, 8, 9, frame_size, nested_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, IR_TYPE_U64, arg_slot, arg, diag)) return false;
      if (!macho_emit_store_scratch(text, 9, IR_TYPE_U32, arg_slot + 1, arg, diag)) return false;
      arg_slot += 2;
      continue;
    }
    if (!macho_emit_value_to_reg_at(text, fun, arg, 8, frame_size, nested_slot, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 8, arg ? arg->type : IR_TYPE_I32, arg_slot, arg, diag)) return false;
    arg_slot++;
  }
  arg_slot = scratch_slot;
  unsigned abi_slot = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    const IrLocal *param = &callee->locals[i];
    if (param->type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_load_scratch(text, abi_slot, IR_TYPE_U64, arg_slot, arg, diag)) return false;
      if (!macho_emit_load_scratch(text, abi_slot + 1, IR_TYPE_U32, arg_slot + 1, arg, diag)) return false;
      arg_slot += 2;
      abi_slot += 2;
      continue;
    }
    if (!macho_emit_load_scratch(text, abi_slot, arg ? arg->type : IR_TYPE_I32, arg_slot, arg, diag)) return false;
    arg_slot++;
    abi_slot++;
  }
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_call_patch(ctx, patch, value->callee_index, value, diag)) return false;
  if (reg != 0) {
    if (macho_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool macho_emit_cast_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_cast_normalize_reg(text, reg, value->left ? value->left->type : IR_TYPE_UNSUPPORTED, value->type);
  return true;
}

static bool macho_emit_binary_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->binary_op == IR_BIN_AND) {
    if (!macho_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t left_false = z_aarch64_emit_cbz_w_placeholder(text, reg);
    if (!macho_emit_value_to_reg_at(text, fun, value->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
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
    if (!macho_emit_value_to_reg_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag)) return false;
    size_t eval_right = z_aarch64_emit_cbz_w_placeholder(text, reg);
    z_aarch64_emit_movz_w(text, reg, 1);
    size_t left_true_end = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, eval_right, text->len);
    if (!macho_emit_value_to_reg_at(text, fun, value->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
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
      value->binary_op != IR_BIN_DIV && value->binary_op != IR_BIN_MOD) return macho_diag_at(diag, "direct AArch64 Mach-O binary operator is unsupported", value->line, value->column, "unsupported operator");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->left ? value->left->type : IR_TYPE_I32, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, value->left ? value->left->type : IR_TYPE_I32, scratch_slot, value->left, diag)) return false;
  bool wide = macho_type_is_scalar64(value->type);
  if (value->binary_op == IR_BIN_DIV) {
    z_aarch64_emit_div_reg(text, reg, 8, 9, macho_type_is_unsigned(value->type), wide);
  } else if (value->binary_op == IR_BIN_MOD) {
    z_aarch64_emit_div_reg(text, 10, 8, 9, macho_type_is_unsigned(value->type), wide);
    z_aarch64_emit_msub_reg(text, reg, 10, 9, 8, wide);
  } else {
    macho_emit_binary_reg(text, value->binary_op, reg, 8, 9, wide);
  }
  return true;
}

static bool macho_emit_compare_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O comparison requires two operands", value->line, value->column, "invalid comparison");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  if (macho_type_is_scalar64(value->left->type)) z_aarch64_emit_cmp_x(text, 8, 9);
  else z_aarch64_emit_cmp_w(text, 8, 9);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t false_patch = z_aarch64_emit_b_cond_placeholder(text, macho_invert_cond(macho_cond_for_compare(value->compare_op, macho_type_is_unsigned(value->left->type))));
  z_aarch64_emit_movz_w(text, reg, 1);
  z_aarch64_patch_cond19(text, false_patch, text->len);
  return true;
}

static bool macho_emit_byte_view_index_load_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  unsigned const_index = 0;
  unsigned char byte = 0;
  if (macho_view_element_type(value->left) == IR_TYPE_U8 &&
      macho_const_u32_value(value->index, &const_index) &&
      macho_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
    z_aarch64_emit_movz_w(text, reg, byte);
    return true;
  }
  if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 9, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  macho_emit_u32_bounds_check(text, 8, 10);
  if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  IrTypeKind element_type = macho_view_element_type(value->left);
  macho_emit_add_scaled_index(text, 9, 9, 8, element_type);
  macho_emit_load_ptr_element(text, reg, 9, element_type);
  return true;
}

static bool macho_emit_byte_copy_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 12, 13, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 12, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 12, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  z_aarch64_emit_byte_copy_min_loop(text, reg);
  return true;
}

static bool macho_emit_byte_fill_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O byte fill requires a fill byte and destination byte view", value->line, value->column, "missing byte fill input");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, IR_TYPE_U8, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 11, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U8, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->right, diag)) return false;
  z_aarch64_emit_byte_fill_loop(text, reg);
  return true;
}

static bool macho_emit_byte_view_eq_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view equality requires two byte views", value->line, value->column, "missing byte view");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 11, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 12, 9, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t same_len = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, same_len, text->len);
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
  macho_emit_scale_len_for_element(text, 10, macho_view_element_type(value->left));
  z_aarch64_emit_byte_eq_loop(text, reg);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_index_load_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->array_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load array is out of range", value->line, value->column, "invalid array local");
  const IrLocal *local = &fun->locals[value->array_index];
  unsigned const_index = 0;
  if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32) &&
      macho_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
    macho_emit_load_local_w(text, fun, reg, value->array_index, const_index * 4u, frame_size);
    return true;
  }
  if (local->is_array &&
      (local->element_type == IR_TYPE_U16 || local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 ||
       local->element_type == IR_TYPE_USIZE || macho_type_is_scalar64(local->element_type))) {
    if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, local->array_len);
    macho_emit_u32_bounds_check(text, 8, 9);
    if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
    z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
    macho_emit_add_scaled_index(text, 9, 9, 8, local->element_type);
    macho_emit_load_ptr_element(text, reg, 9, local->element_type);
    return true;
  }
  if (!local->is_array || (local->element_type != IR_TYPE_U8 && local->element_type != IR_TYPE_BOOL)) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load requires [N]u8, [N]Bool, or integer arrays", value->line, value->column, "unsupported array local");
  if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, local->array_len);
  macho_emit_u32_bounds_check(text, 8, 9);
  if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  z_aarch64_emit_load_b_imm(text, reg, 9, 0);
  return true;
}

static bool macho_emit_http_fetch_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_store_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 4, frame_size, scratch_slot + 4, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_FETCH, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  return true;
}

static bool macho_emit_http_result_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_http_response_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_http_header_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_HEADER_VALUE, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  return true;
}

static bool macho_emit_vec_push_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
  macho_emit_load_local_w(text, fun, 8, value->local_index, 8, frame_size);
  macho_emit_load_local_w(text, fun, 9, value->local_index, 12, frame_size);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
  macho_emit_load_local_x(text, fun, 9, value->local_index, 0, frame_size);
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  if (!macho_emit_store_scratch(text, 9, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 9, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  z_aarch64_emit_store_b_imm(text, 10, 9, 0);
  z_aarch64_emit_add_w_imm(text, 8, 8, 1);
  macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
  z_aarch64_emit_movz_w(text, reg, 1);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_check_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || value->left->type != IR_TYPE_I64) return macho_diag_at(diag, "direct AArch64 Mach-O check requires a packed fallible call result", value->line, value->column, "non-fallible value");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_error_condition_reg(text, 8, 0);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 0);
  bool restore_process_args = ctx && ctx->seed_main_process_args && macho_is_main_function(fun);
  if (macho_function_propagates_to_process_exit(fun)) {
    macho_emit_epilogue(text, frame_size, restore_process_args);
  } else {
    z_aarch64_emit_movz_w(text, 0, 1);
    macho_emit_epilogue(text, frame_size, restore_process_args);
  }
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  if (reg != 0) {
    if (macho_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  } else if (!macho_type_is_scalar64(value->type)) {
    z_aarch64_emit_mov_w(text, 0, 0);
  }
  return true;
}

static bool macho_emit_rescue_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right || value->left->type != IR_TYPE_I64) return macho_diag_at(diag, "direct AArch64 Mach-O rescue requires a packed fallible call and fallback", value->line, value->column, "unsupported rescue");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_error_condition_reg(text, 8, 0);
  size_t fallback_patch = z_aarch64_emit_b_cond_placeholder(text, 1);
  if (reg != 0) {
    if (macho_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  } else if (!macho_type_is_scalar64(value->type)) {
    z_aarch64_emit_mov_w(text, 0, 0);
  }
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, fallback_patch, text->len);
  if (!macho_emit_value_to_reg_at(text, fun, value->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value) return macho_diag_at(diag, "direct AArch64 Mach-O expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL: case IR_VALUE_INT:
      if (macho_type_is_scalar64(value->type)) z_aarch64_emit_movz_x(text, reg, (uint64_t)value->int_value);
      else z_aarch64_emit_movz_w(text, reg, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O local index is out of range", value->line, value->column, "invalid local");
      if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) {
        return macho_diag_at(diag, "direct AArch64 Mach-O byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
      }
      if (macho_type_is_scalar64(fun->locals[value->local_index].type)) macho_emit_load_local_x(text, fun, reg, value->local_index, 0, frame_size);
      else macho_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_CAST: return macho_emit_cast_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BINARY:
      return macho_emit_binary_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_COMPARE:
      return macho_emit_compare_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CALL:
      return macho_emit_call_to_reg(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_PARSE_BYTES:
      if (!macho_emit_json_parse_bytes_call_at(text, fun, value, frame_size, scratch_slot, ctx, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
      return true;
    case IR_VALUE_JSON_VALIDATE_BYTES:
      if (!macho_emit_json_parse_bytes_call_at(text, fun, value, frame_size, scratch_slot, ctx, diag)) return false;
      z_aarch64_emit_cmp_x(text, 0, 31);
      z_aarch64_emit_movz_w(text, reg, 0);
      {
        size_t invalid = z_aarch64_emit_b_cond_placeholder(text, 11); // signed less than
        z_aarch64_emit_movz_w(text, reg, 1);
        z_aarch64_patch_cond19(text, invalid, text->len);
      }
      return true;
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
      if (!macho_emit_json_parse_bytes_call_at(text, fun, value, frame_size, scratch_slot, ctx, diag)) return false;
      z_aarch64_emit_cmp_x(text, 0, 31);
      {
        size_t ok = z_aarch64_emit_b_cond_placeholder(text, 10); // signed greater or equal
        if (reg != 0) z_aarch64_emit_mov_x(text, reg, 31);
        else z_aarch64_emit_mov_x(text, 0, 31);
        size_t done = z_aarch64_emit_b_placeholder(text);
        z_aarch64_patch_cond19(text, ok, text->len);
        if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
        z_aarch64_patch_branch26(text, done, text->len);
      }
      return true;
    case IR_VALUE_HTTP_FETCH:
      return macho_emit_http_fetch_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_RESULT_OK:
    case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR:
    case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN:
      return macho_emit_http_result_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
      return macho_emit_http_response_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_HEADER_VALUE:
      return macho_emit_http_header_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      macho_emit_load_local_w(text, fun, reg, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12, frame_size);
      return true;
    case IR_VALUE_VEC_PUSH:
      return macho_emit_vec_push_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_RAND_NEXT_U32:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      macho_emit_load_local_w(text, fun, 8, value->local_index, 0, frame_size);
      z_aarch64_emit_movz_w(text, 9, 1664525u);
      z_aarch64_emit_mul_w_reg(text, 8, 8, 9);
      z_aarch64_emit_movz_w(text, 9, 1013904223u);
      z_aarch64_emit_add_w_reg(text, 8, 8, 9);
      macho_emit_store_local_w(text, fun, 8, value->local_index, 0, frame_size);
      if (reg != 8) z_aarch64_emit_mov_w(text, reg, 8);
      return true;
    case IR_VALUE_CHECK:
      return macho_emit_check_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_RESCUE:
      return macho_emit_rescue_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ARGS_LEN:
      z_aarch64_emit_mov_w(text, reg, 20);
      return true;
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len ||
          (fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW && fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR)) {
        return macho_diag_at(diag, "direct AArch64 Mach-O maybe helper requires a Maybe local", value->line, value->column, "invalid maybe local");
      }
      macho_emit_load_local_w(text, fun, reg, value->local_index, 0, frame_size);
      return true;
    case IR_VALUE_MAYBE_VALUE:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR) {
        return macho_diag_at(diag, "direct AArch64 Mach-O maybe scalar value requires a Maybe scalar local", value->line, value->column, "invalid maybe value");
      }
      if (macho_type_is_scalar64(value->type)) macho_emit_load_local_x(text, fun, reg, value->local_index, 8, frame_size);
      else macho_emit_load_local_w(text, fun, reg, value->local_index, 8, frame_size);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN:
      return macho_emit_byte_view_len_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_COPY:
      return macho_emit_byte_copy_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_FILL:
      return macho_emit_byte_fill_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ:
      return macho_emit_byte_view_eq_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
      return macho_emit_byte_view_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_INDEX_LOAD:
      return macho_emit_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FIELD_LOAD:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field load record is out of range", value->line, value->column, "invalid record local");
      if (!fun->locals[value->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field load requires record local", value->line, value->column, "non-record local");
      macho_emit_load_field(text, fun, reg, value->local_index, value->field_offset, value->type, frame_size);
      return true;
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported value kind %d", value ? (int)value->kind : -1);
      return macho_diag_at(diag, "direct AArch64 Mach-O value kind is unsupported", value->line, value->column, actual);
    }
  }
}

static size_t macho_function_frame_bytes(const IrFunction *fun) {
  uint32_t literal = 0;
  if (macho_is_literal_return_function(fun, &literal, NULL)) return 0;
  unsigned base = (unsigned)(fun ? (fun->frame_bytes ? fun->frame_bytes : fun->local_len * 8) : 0);
  return macho_align(base + MACHO_SCRATCH_SLOT_COUNT * MACHO_SCRATCH_SLOT_BYTES, 16);
}

size_t z_macho64_stack_bytes_from_ir(const IrProgram *program) {
  size_t total = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    total += macho_function_frame_bytes(&program->functions[i]);
  }
  return total;
}

size_t z_macho64_max_frame_bytes_from_ir(const IrProgram *program) {
  size_t max_frame = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    size_t frame = macho_function_frame_bytes(&program->functions[i]);
    if (frame > max_frame) max_frame = frame;
  }
  return max_frame;
}

static unsigned macho_frame_size(const IrFunction *fun) {
  return (unsigned)macho_function_frame_bytes(fun);
}

static void macho_emit_epilogue(ZBuf *text, unsigned frame_size, bool restore_process_args) {
  if (frame_size > 0) z_aarch64_emit_add_sp_imm(text, frame_size);
  z_aarch64_emit_ldp_x29_x30_sp_post16(text);
  if (restore_process_args) z_aarch64_emit_ldp_x20_x21_sp_post16(text);
  z_aarch64_emit_ret(text);
}

static bool macho_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag);

static bool macho_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return macho_diag_at(diag, "direct AArch64 Mach-O World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!macho_emit_byte_view_pair(text, fun, instr->value, 1, 2, frame_size, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 0, instr->field_offset == 2 ? 2u : 1u);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_instr_runtime_patch(ctx, MACHO_RUNTIME_WORLD_WRITE, patch, instr, diag)) return false;
  size_t ok_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
}

static bool macho_emit_args_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return macho_diag_at(diag, "direct AArch64 Mach-O std.args.get requires an index", value ? value->line : 1, value ? value->column : 1, "missing index");
  if (!macho_emit_value_to_reg(text, fun, value->left, 10, frame_size, ctx, diag)) return false;
  z_aarch64_emit_cmp_w(text, 10, 20);
  size_t in_range = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, local->index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, local->index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 8, local->index, 16, frame_size);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, in_range, text->len);

  z_aarch64_emit_add_x_reg_lsl(text, 12, 21, 10, 3);
  z_aarch64_emit_load_x_imm(text, 12, 12, 0);
  z_aarch64_emit_movz_w(text, 10, 0);
  size_t loop_start = text->len;
  z_aarch64_emit_add_x_reg(text, 13, 12, 10);
  z_aarch64_emit_load_b_imm(text, 14, 13, 0);
  size_t done_patch = z_aarch64_emit_cbz_w_placeholder(text, 14);
  z_aarch64_emit_add_w_imm(text, 10, 10, 1);
  size_t loop_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, loop_patch, loop_start);
  z_aarch64_patch_cond19(text, done_patch, text->len);

  z_aarch64_emit_movz_w(text, 8, 1);
  macho_emit_store_local_w(text, fun, 8, local->index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 12, local->index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 10, local->index, 16, frame_size);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_local_set_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair(text, fun, instr->value, 8, 9, frame_size, ctx, diag)) return false;
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 8, frame_size);
  return true;
}

static bool macho_emit_local_set_alloc(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return macho_diag_at(diag, "direct AArch64 Mach-O FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
  if (!macho_emit_byte_view_pair(text, fun, instr->value->left, 8, 9, frame_size, ctx, diag)) return false;
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 8, frame_size);
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, instr->local_index, 12, frame_size);
  return true;
}

static bool macho_emit_local_set_vec(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return macho_diag_at(diag, "direct AArch64 Mach-O Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
  if (!macho_emit_byte_view_pair(text, fun, instr->value->left, 8, 9, frame_size, ctx, diag)) return false;
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 12, frame_size);
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
  return true;
}

static bool macho_emit_local_set_maybe_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->value && instr->value->kind == IR_VALUE_ARGS_GET) {
    return macho_emit_args_get_to_local(text, fun, instr->value, &fun->locals[instr->local_index], frame_size, ctx, diag);
  }
  if (instr->value && instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
    z_aarch64_emit_movz_w(text, 8, instr->value->data_len ? 1u : 0u);
    macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
    if (!instr->value->data_len) {
      z_aarch64_emit_movz_w(text, 8, 0);
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 16, frame_size);
      return true;
    }
    if (!macho_emit_byte_view_pair(text, fun, instr->value->left, 8, 9, frame_size, ctx, diag)) return false;
    macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
    macho_emit_store_local_w(text, fun, 9, instr->local_index, 16, frame_size);
    return true;
  }
  if (instr->value && instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    macho_emit_store_local_w(text, fun, 0, instr->local_index, 0, frame_size);
    macho_emit_store_local_x(text, fun, 1, instr->local_index, 8, frame_size);
    macho_emit_store_local_w(text, fun, 2, instr->local_index, 16, frame_size);
    return true;
  }
  if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return macho_diag_at(diag, "direct AArch64 Mach-O allocation source is invalid", instr->line, instr->column, "invalid allocation");
  if (!macho_emit_value_to_reg(text, fun, instr->value->left, 10, frame_size, ctx, diag)) return false;
  macho_emit_load_local_w(text, fun, 8, instr->value->local_index, 12, frame_size);
  macho_emit_load_local_w(text, fun, 9, instr->value->local_index, 8, frame_size);
  z_aarch64_emit_add_w_imm(text, 11, 8, 0);
  macho_emit_binary_reg(text, IR_BIN_ADD, 11, 11, 10, false);
  z_aarch64_emit_cmp_w(text, 11, 9);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 9); // unsigned lower or same
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 8, instr->local_index, 16, frame_size);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  z_aarch64_emit_movz_w(text, 12, 1);
  macho_emit_store_local_w(text, fun, 12, instr->local_index, 0, frame_size);
  macho_emit_load_local_x(text, fun, 12, instr->value->local_index, 0, frame_size);
  z_aarch64_emit_add_x_reg(text, 12, 12, 8);
  macho_emit_store_local_x(text, fun, 12, instr->local_index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 10, instr->local_index, 16, frame_size);
  macho_emit_store_local_w(text, fun, 11, instr->value->local_index, 12, frame_size);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_local_set_maybe_scalar_json_parse(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) {
    return macho_diag_at(diag, "direct AArch64 Mach-O JSON parse allocator is invalid", instr->line, instr->column, "invalid allocator");
  }
  if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
  z_aarch64_emit_cmp_x(text, 8, 31);
  size_t fail = z_aarch64_emit_b_cond_placeholder(text, 11); // signed less than
  macho_emit_load_local_w(text, fun, 9, instr->value->local_index, 12, frame_size);
  z_aarch64_emit_mov_w(text, 10, 8);
  macho_emit_binary_reg(text, IR_BIN_ADD, 11, 9, 10, false);
  macho_emit_load_local_w(text, fun, 12, instr->value->local_index, 8, frame_size);
  z_aarch64_emit_cmp_w(text, 11, 12);
  size_t overflow = z_aarch64_emit_b_cond_placeholder(text, 8); // unsigned higher
  z_aarch64_emit_movz_w(text, 9, 1);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 11, instr->value->local_index, 12, frame_size);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, fail, text->len);
  z_aarch64_patch_cond19(text, overflow, text->len);
  z_aarch64_emit_movz_w(text, 9, 0);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 9, instr->local_index, 8, frame_size);
  z_aarch64_patch_branch26(text, end, text->len);
  return true;
}

static bool macho_emit_local_set_maybe_scalar(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value) return macho_diag_at(diag, "direct AArch64 Mach-O Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
  if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
    z_aarch64_emit_movz_w(text, 8, instr->value->data_len ? 1u : 0u);
    macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
    z_aarch64_emit_movz_x(text, 8, (uint64_t)instr->value->int_value);
    macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
    return true;
  }
  if (instr->value->kind == IR_VALUE_JSON_PARSE_BYTES) {
    return macho_emit_local_set_maybe_scalar_json_parse(text, fun, instr, frame_size, ctx, diag);
  }
  if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    macho_emit_store_local_w(text, fun, 0, instr->local_index, 0, frame_size);
    macho_emit_store_local_x(text, fun, 1, instr->local_index, 8, frame_size);
    return true;
  }
  if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len && fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
    macho_emit_load_local_w(text, fun, 8, instr->value->local_index, 0, frame_size);
    macho_emit_load_local_x(text, fun, 9, instr->value->local_index, 8, frame_size);
    macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
    macho_emit_store_local_x(text, fun, 9, instr->local_index, 8, frame_size);
    return true;
  }
  if (macho_type_is_scalar(instr->value->type) || instr->value->type == IR_TYPE_BOOL) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, 1);
    macho_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
    macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
    return true;
  }
  if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
  z_aarch64_emit_cmp_x(text, 8, 31);
  size_t fail = z_aarch64_emit_b_cond_placeholder(text, 11); // signed less than
  z_aarch64_emit_movz_w(text, 9, 1);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, fail, text->len);
  z_aarch64_emit_movz_w(text, 9, 0);
  macho_emit_store_local_w(text, fun, 9, instr->local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 9, instr->local_index, 8, frame_size);
  z_aarch64_patch_branch26(text, end, text->len);
  return true;
}

static bool macho_emit_local_set_scalar(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
  if (macho_type_is_scalar64(fun->locals[instr->local_index].type)) macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
  else macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
  return true;
}

static bool macho_emit_local_set(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O local store is out of range", instr->line, instr->column, "invalid local");
  switch (fun->locals[instr->local_index].type) {
    case IR_TYPE_BYTE_VIEW: return macho_emit_local_set_byte_view(text, fun, instr, frame_size, ctx, diag);
    case IR_TYPE_ALLOC: return macho_emit_local_set_alloc(text, fun, instr, frame_size, ctx, diag);
    case IR_TYPE_VEC: return macho_emit_local_set_vec(text, fun, instr, frame_size, ctx, diag);
    case IR_TYPE_MAYBE_BYTE_VIEW: return macho_emit_local_set_maybe_byte_view(text, fun, instr, frame_size, ctx, diag);
    case IR_TYPE_MAYBE_SCALAR: return macho_emit_local_set_maybe_scalar(text, fun, instr, frame_size, ctx, diag);
    default: return macho_emit_local_set_scalar(text, fun, instr, frame_size, ctx, diag);
  }
}

static bool macho_emit_byte_view_index_store(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  const IrLocal *local = instr->array_index < fun->local_len ? &fun->locals[instr->array_index] : NULL;
  IrTypeKind element_type = local && local->element_type != IR_TYPE_UNSUPPORTED ? local->element_type : IR_TYPE_U8;
  if (!instr->value || instr->value->type != element_type) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view indexed store value type does not match span element", instr->line, instr->column, "unsupported byte-view store value");
  if (!macho_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 10, element_type, 0, instr->value, diag)) return false;
  if (!instr->index || !macho_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
  macho_emit_load_local_w(text, fun, 9, instr->array_index, 8, frame_size);
  if (!macho_emit_load_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
  macho_emit_u32_bounds_check(text, 8, 9);
  macho_emit_load_local_x(text, fun, 9, instr->array_index, 0, frame_size);
  if (!macho_emit_load_scratch(text, 8, instr->index ? instr->index->type : IR_TYPE_U32, 1, instr->index, diag)) return false;
  macho_emit_add_scaled_index(text, 9, 9, 8, element_type);
  if (!macho_emit_load_scratch(text, 10, element_type, 0, instr->value, diag)) return false;
  macho_emit_store_ptr_element(text, 10, 9, element_type);
  return true;
}

static bool macho_emit_return_maybe_scalar(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value) return false;
  if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_SCALAR) {
    return macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag);
  }
  if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
    z_aarch64_emit_movz_w(text, 0, instr->value->data_len ? 1u : 0u);
    z_aarch64_emit_movz_x(text, 1, (uint64_t)instr->value->int_value);
    return true;
  }
  if (instr->value->kind == IR_VALUE_LOCAL && instr->value->local_index < fun->local_len &&
      fun->locals[instr->value->local_index].type == IR_TYPE_MAYBE_SCALAR) {
    macho_emit_load_local_w(text, fun, 0, instr->value->local_index, 0, frame_size);
    macho_emit_load_local_x(text, fun, 1, instr->value->local_index, 8, frame_size);
    return true;
  }
  if (macho_type_is_scalar(instr->value->type) || instr->value->type == IR_TYPE_BOOL) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 1, frame_size, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 0, 1);
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O Maybe scalar return requires a Maybe scalar or scalar value", instr->line, instr->column, "unsupported Maybe scalar return");
}

static bool macho_emit_return_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  if (fun->return_type == IR_TYPE_BYTE_VIEW && instr->value) {
    if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    } else {
      if (!macho_emit_byte_view_pair(text, fun, instr->value, 0, 1, frame_size, ctx, diag)) return false;
    }
    macho_emit_epilogue(text, frame_size, restore_process_args);
    return true;
  }
  if (fun->return_type == IR_TYPE_MAYBE_BYTE_VIEW && instr->value) {
    if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    } else if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
      if (!instr->value->data_len) {
        z_aarch64_emit_movz_w(text, 0, 0);
        z_aarch64_emit_movz_w(text, 1, 0);
        z_aarch64_emit_movz_w(text, 2, 0);
      } else {
        z_aarch64_emit_movz_w(text, 0, 1);
        if (!macho_emit_byte_view_pair(text, fun, instr->value->left, 1, 2, frame_size, ctx, diag)) return false;
      }
    } else {
      return macho_diag_at(diag, "direct AArch64 Mach-O Maybe byte-view return requires a Maybe byte-view value", instr->line, instr->column, "unsupported Maybe byte-view return");
    }
    macho_emit_epilogue(text, frame_size, restore_process_args);
    return true;
  }
  if (fun->return_type == IR_TYPE_MAYBE_SCALAR && instr->value) {
    if (!macho_emit_return_maybe_scalar(text, fun, instr, frame_size, ctx, diag)) return false;
    macho_emit_epilogue(text, frame_size, restore_process_args);
    return true;
  }
  if (instr->value && !macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
  if (fun->raises && !instr->value) z_aarch64_emit_movz_x(text, 0, 0);
  macho_emit_epilogue(text, frame_size, restore_process_args);
  return true;
}

static bool macho_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return macho_emit_world_write(text, fun, instr, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) return macho_emit_local_set(text, fun, instr, frame_size, ctx, diag);
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field store record is out of range", instr->line, instr->column, "invalid record local");
    if (!fun->locals[instr->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field store requires record local", instr->line, instr->column, "non-record local");
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    macho_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32, frame_size);
    return true;
  }
  if (instr->kind == IR_INSTR_INDEX_STORE) {
    if (instr->array_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O indexed store array is out of range", instr->line, instr->column, "invalid array local");
    const IrLocal *local = &fun->locals[instr->array_index];
    if (local->type == IR_TYPE_BYTE_VIEW) return macho_emit_byte_view_index_store(text, fun, instr, frame_size, ctx, diag);
    unsigned const_index = 0;
    if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32) &&
        macho_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 10, instr->array_index, const_index * 4u, frame_size);
      return true;
    }
    if (local->is_array &&
        (local->element_type == IR_TYPE_U16 || local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 ||
         local->element_type == IR_TYPE_USIZE || macho_type_is_scalar64(local->element_type))) {
      if (!macho_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
      if (!instr->index || !macho_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
      z_aarch64_emit_movz_w(text, 9, local->array_len);
      macho_emit_u32_bounds_check(text, 8, 9);
      z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
      macho_emit_add_scaled_index(text, 9, 9, 8, local->element_type);
      if (!macho_emit_load_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
      macho_emit_store_ptr_element(text, 10, 9, local->element_type);
      return true;
    }
    if (!local->is_array || (local->element_type != IR_TYPE_U8 && local->element_type != IR_TYPE_BOOL)) return macho_diag_at(diag, "direct AArch64 Mach-O indexed store requires [N]u8, [N]Bool, or integer arrays", instr->line, instr->column, "unsupported array local");
    if (!macho_emit_value_to_reg_at(text, fun, instr->value, 10, frame_size, 0, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
    if (!instr->index || !macho_emit_value_to_reg_at(text, fun, instr->index, 8, frame_size, 1, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, local->array_len);
    macho_emit_u32_bounds_check(text, 8, 9);
    z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
    z_aarch64_emit_add_x_reg(text, 9, 9, 8);
    if (!macho_emit_load_scratch(text, 10, instr->value ? instr->value->type : local->element_type, 0, instr->value, diag)) return false;
    z_aarch64_emit_store_b_imm(text, 10, 9, 0);
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    return !instr->value || macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_RETURN) return macho_emit_return_instr(text, fun, instr, frame_size, restore_process_args, ctx, diag);
  if (instr->kind == IR_INSTR_RAISE) {
    if (!macho_function_propagates_to_process_exit(fun)) return macho_diag_at(diag, "direct AArch64 Mach-O raise requires a fallible function context", instr->line, instr->column, "non-fallible context");
    macho_emit_packed_error_reg(text, 0, instr->error_code ? instr->error_code : IR_ERROR_UNKNOWN);
    macho_emit_epilogue(text, frame_size, restore_process_args);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    size_t false_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
    if (!macho_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, restore_process_args, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, false_patch, text->len);
      if (!macho_emit_instrs(text, fun, instr->else_instrs, instr->else_len, frame_size, restore_process_args, ctx, diag)) return false;
      z_aarch64_patch_branch26(text, end_patch, text->len);
    } else {
      z_aarch64_patch_cond19(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
    size_t false_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
    if (!macho_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, restore_process_args, ctx, diag)) return false;
    size_t loop_patch = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_branch26(text, loop_patch, loop_start);
    z_aarch64_patch_cond19(text, false_patch, text->len);
    return true;
  }
  char actual[64];
  snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
  return macho_diag_at(diag, "direct AArch64 Mach-O instruction kind is unsupported", instr->line, instr->column, actual);
}

static bool macho_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!macho_emit_instr(text, fun, &instrs[i], frame_size, restore_process_args, ctx, diag)) return false;
  }
  return true;
}

static bool macho_validate_function(const IrFunction *fun, ZDiag *diag) {
  uint32_t ignored = 0;
  if (macho_is_literal_return_function(fun, &ignored, NULL)) return true;
  unsigned abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) {
    abi_slots += macho_abi_slots_for_param(&fun->locals[i]);
    if (abi_slots > 8) return macho_diag_at(diag, "direct AArch64 Mach-O object backend supports at most eight ABI argument slots", fun->line, fun->column, fun->name);
  }
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar(fun->return_type) &&
      fun->return_type != IR_TYPE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_SCALAR) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports Void, primitive integer, byte-view, Maybe byte-view, and Maybe scalar returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      continue;
    }
    if (fun->locals[i].is_array && (fun->locals[i].element_type == IR_TYPE_U8 || fun->locals[i].element_type == IR_TYPE_BOOL ||
                                    fun->locals[i].element_type == IR_TYPE_U16 || fun->locals[i].element_type == IR_TYPE_U32 ||
                                    fun->locals[i].element_type == IR_TYPE_I32 || fun->locals[i].element_type == IR_TYPE_USIZE ||
                                    macho_type_is_scalar64(fun->locals[i].element_type))) continue;
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW || fun->locals[i].type == IR_TYPE_MAYBE_SCALAR) continue;
    if (fun->locals[i].type == IR_TYPE_VEC) continue;
    if (fun->locals[i].is_array || !macho_type_is_scalar(fun->locals[i].type)) {
      return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports only primitive scalar locals", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
    }
  }
  return true;
}

static bool macho_emit_function_text(ZBuf *text, const IrFunction *fun, MachOEmitContext *ctx, ZDiag *diag) {
  uint32_t literal = 0;
  if (macho_is_literal_return_function(fun, &literal, NULL)) {
    z_aarch64_emit_literal_return(text, literal);
    return true;
  }

  unsigned frame_size = macho_frame_size(fun);
  bool seed_process_args = ctx && ctx->seed_main_process_args && macho_is_main_function(fun);
  if (seed_process_args) z_aarch64_emit_stp_x20_x21_sp_pre16(text);
  z_aarch64_emit_stp_x29_x30_sp_pre16(text);
  z_aarch64_emit_mov_x29_sp(text);
  if (frame_size > 0) z_aarch64_emit_sub_sp_imm(text, frame_size);
  if (seed_process_args) {
    z_aarch64_emit_mov_x(text, 20, 0);
    z_aarch64_emit_mov_x(text, 21, 1);
  }
  unsigned abi_slot = 0;
  for (size_t i = 0; i < fun->param_count; i++) {
    const IrLocal *local = &fun->locals[i];
    unsigned slots = macho_abi_slots_for_param(local);
    if (abi_slot + slots > 8) return macho_diag_at(diag, "direct AArch64 Mach-O function has too many ABI argument slots", fun->line, fun->column, fun->name);
    if (local->type == IR_TYPE_BYTE_VIEW) {
      macho_emit_store_local_x(text, fun, abi_slot, (unsigned)i, 0, frame_size);
      macho_emit_store_local_w(text, fun, abi_slot + 1, (unsigned)i, 8, frame_size);
    } else if (macho_type_is_scalar64(local->type)) {
      macho_emit_store_local_x(text, fun, abi_slot, (unsigned)i, 0, frame_size);
    } else {
      macho_emit_store_local_w(text, fun, abi_slot, (unsigned)i, 0, frame_size);
    }
    abi_slot += slots;
  }
  if (!macho_emit_instrs(text, fun, fun->instrs, fun->instr_len, frame_size, seed_process_args, ctx, diag)) return false;
  if (fun->instr_len == 0 || (fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN && fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RAISE)) macho_emit_epilogue(text, frame_size, seed_process_args);
  return true;
}

static unsigned macho_rodata_base_offset(const IrProgram *program) {
  if (!program || program->data_segment_len == 0) return 0;
  unsigned base = program->data_segments[0].offset;
  for (size_t i = 1; i < program->data_segment_len; i++) {
    if (program->data_segments[i].offset < base) base = program->data_segments[i].offset;
  }
  return base;
}

static void macho_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    macho_pad_to(rodata, segment->offset - base_offset);
    append_bytes(rodata, (const char *)segment->bytes, segment->len);
  }
}

typedef struct {
  ZBuf text, rodata, relocs, strings;
  size_t *offsets;
  uint32_t *function_string_offsets, runtime_string_offsets[MACHO_RUNTIME_HELPER_COUNT];
  ZMachOSymbol *symbols;
  size_t symbol_len;
  uint32_t symbol_count, rodata_string_offset;
  bool has_rodata;
  unsigned rodata_base_offset;
  MachOEmitContext ctx;
} MachOObjectBuild;

static bool macho_validate_object_program(const IrProgram *program, ZDiag *diag) {
  if (!program) return macho_diag(diag, "direct Mach-O backend received no program");
  if (!program->mir_valid) {
    bool ok = macho_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (program->function_len == 0) return macho_diag_at(diag, "direct AArch64 Mach-O object backend requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (program->functions[i].is_exported) has_export = true;
    if (!macho_validate_function(&program->functions[i], diag)) return false;
  }
  if (!has_export) return macho_diag_at(diag, "direct AArch64 Mach-O object backend requires at least one exported function", 1, 1, "no exported function");
  return true;
}

static void macho_object_build_free(MachOObjectBuild *build) {
  if (!build) return;
  free(build->symbols);
  z_macho_emit_context_free(&build->ctx);
  free(build->function_string_offsets);
  free(build->offsets);
  zbuf_free(&build->strings);
  zbuf_free(&build->relocs);
  zbuf_free(&build->rodata);
  zbuf_free(&build->text);
}

static bool macho_object_build_init(MachOObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  memset(build, 0, sizeof(*build));
  zbuf_init(&build->text);
  zbuf_init(&build->rodata);
  zbuf_init(&build->relocs);
  zbuf_init(&build->strings);
  build->has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  build->rodata_base_offset = macho_rodata_base_offset(program);
  if (build->has_rodata) macho_append_rodata(&build->rodata, program, build->rodata_base_offset);
  build->offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  build->function_string_offsets = z_checked_calloc(program->function_len, sizeof(uint32_t));
  if (!build->offsets || !build->function_string_offsets) {
    macho_object_build_free(build);
    return macho_diag(diag, build->offsets ? "out of memory while emitting Mach-O symbols" : "out of memory while emitting Mach-O object");
  }
  append_u8(&build->strings, 0);
  build->ctx = (MachOEmitContext){
    .program = program,
    .function_offsets = build->offsets,
    .function_count = program->function_len,
    .rodata_base_offset = build->rodata_base_offset,
    .pie_relative_data = true,
    .seed_main_process_args = true
  };
  return true;
}

static bool macho_object_emit_functions(MachOObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  for (size_t i = 0; i < program->function_len; i++) {
    const IrFunction *fun = &program->functions[i];
    macho_pad_to(&build->text, macho_align(build->text.len, 4));
    build->offsets[i] = build->text.len;
    if (!macho_emit_function_text(&build->text, fun, &build->ctx, diag)) return false;
    build->function_string_offsets[i] = (uint32_t)build->strings.len;
    zbuf_append_char(&build->strings, '_');
    zbuf_append(&build->strings, fun->name ? fun->name : "zero_fn");
    append_u8(&build->strings, 0);
  }
  return true;
}

static void macho_object_append_relocations(MachOObjectBuild *build, const IrProgram *program) {
  z_macho_append_call_relocations(&build->relocs, &build->ctx);
  if (build->has_rodata) z_macho_append_data_relocations(&build->relocs, &build->ctx, (unsigned)program->function_len);
  uint32_t next_symbol = (uint32_t)program->function_len + (build->has_rodata ? 1u : 0u);
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    z_macho_append_runtime_relocations(&build->relocs, &build->ctx, runtime_helper, next_symbol++);
  }
  build->symbol_count = next_symbol;
}

static void macho_object_append_symbol_strings(MachOObjectBuild *build) {
  if (build->has_rodata) {
    build->rodata_string_offset = (uint32_t)build->strings.len;
    zbuf_append(&build->strings, "l_.zero_rodata");
    append_u8(&build->strings, 0);
  }
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    build->runtime_string_offsets[helper] = (uint32_t)build->strings.len;
    zbuf_append(&build->strings, z_macho_runtime_helper_symbol(runtime_helper));
    append_u8(&build->strings, 0);
  }
}

static bool macho_object_build_symbols(MachOObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  build->symbols = z_checked_calloc(build->symbol_count, sizeof(ZMachOSymbol));
  if (!build->symbols) return macho_diag(diag, "out of memory while emitting Mach-O symbols");
  for (size_t i = 0; i < program->function_len; i++) {
    build->symbols[build->symbol_len++] = (ZMachOSymbol){
      .string_offset = build->function_string_offsets[i],
      .type = program->functions[i].is_exported ? 0x0f : 0x0e,
      .section = 1,
      .value = build->offsets[i]
    };
  }
  if (build->has_rodata) {
    build->symbols[build->symbol_len++] = (ZMachOSymbol){
      .string_offset = build->rodata_string_offset,
      .type = 0x0e,
      .section = 2,
      .value = (uint32_t)macho_align(build->text.len, 8)
    };
  }
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    build->symbols[build->symbol_len++] = (ZMachOSymbol){ .string_offset = build->runtime_string_offsets[helper], .type = 0x01 };
  }
  return true;
}

static void macho_object_write(const MachOObjectBuild *build, ZBuf *out) {
  ZMachOObjectImage image = {
    .text = &build->text,
    .rodata = build->has_rodata ? &build->rodata : NULL,
    .relocs = &build->relocs,
    .strings = &build->strings,
    .symbols = build->symbols,
    .symbol_len = build->symbol_len,
    .text_reloc_count = (uint32_t)z_macho_text_relocation_count(&build->ctx)
  };
  z_macho_write_object64(out, &image);
}

bool z_emit_macho64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!out) return macho_diag(diag, "direct Mach-O backend received no output buffer");
  if (!macho_validate_object_program(program, diag)) return false;
  MachOObjectBuild build;
  if (!macho_object_build_init(&build, program, diag)) return false;
  bool ok = macho_object_emit_functions(&build, program, diag);
  if (ok) {
    macho_object_append_relocations(&build, program);
    macho_object_append_symbol_strings(&build);
    ok = macho_object_build_symbols(&build, program, diag);
  }
  if (ok) macho_object_write(&build, out);
  macho_object_build_free(&build);
  return ok;
}

static const IrFunction *macho_find_executable_main(const IrProgram *program, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    if (macho_is_main_function(&program->functions[i])) {
      if (fun) {
        macho_diag_at(diag, "direct AArch64 Mach-O executable backend requires exactly one exported main function", program->functions[i].line, program->functions[i].column, program->functions[i].name);
        return NULL;
      }
      fun = &program->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (fun->param_count != 0) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable main must not take parameters", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar32(fun->return_type) && !macho_type_is_scalar64(fun->return_type)) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable main must return Void or a primitive scalar", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

static size_t macho_emit_exe_start_stub(ZBuf *text) {
  z_aarch64_emit_mov_x(text, 20, 0);
  z_aarch64_emit_mov_x(text, 21, 1);
  size_t patch = z_aarch64_emit_b_placeholder(text); // tail-call main so it returns to dyld's LC_MAIN trampoline
  return patch;
}

static size_t macho_emit_exe_world_write(ZBuf *text) {
  size_t offset = text->len;
  z_aarch64_emit_movz_x(text, 16, 0x02000004u); // Darwin SYS_write(fd=x0, buf=x1, len=x2)
  z_aarch64_emit_svc(text, 0x80);
  z_aarch64_emit_movz_w(text, 0, 0);   // report success to the checked std.io shim
  z_aarch64_emit_ret(text);
  return offset;
}

typedef struct {
  ZBuf text, rodata, rebase;
  size_t *offsets, start_call_patch;
  ZMachOExecutableLayout layout;
  MachOEmitContext ctx;
  unsigned main_index, rodata_base_offset;
  bool has_rodata;
} MachOExeBuild;

static bool macho_validate_exe_program(const IrProgram *program, unsigned *main_index, ZDiag *diag) {
  if (!program->mir_valid) {
    bool ok = macho_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (!macho_find_executable_main(program, diag, main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) if (!macho_validate_function(&program->functions[i], diag)) return false;
  return true;
}

static void macho_exe_build_free(MachOExeBuild *build) {
  if (!build) return;
  z_macho_emit_context_free(&build->ctx);
  free(build->offsets);
  zbuf_free(&build->rebase); zbuf_free(&build->rodata); zbuf_free(&build->text);
}

static bool macho_exe_build_init(MachOExeBuild *build, const IrProgram *program, unsigned main_index, ZDiag *diag) {
  memset(build, 0, sizeof(*build));
  zbuf_init(&build->text); zbuf_init(&build->rodata); zbuf_init(&build->rebase);
  build->main_index = main_index;
  build->has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  build->rodata_base_offset = macho_rodata_base_offset(program);
  if (build->has_rodata) macho_append_rodata(&build->rodata, program, build->rodata_base_offset);
  build->offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!build->offsets) {
    macho_exe_build_free(build);
    return macho_diag(diag, "out of memory while emitting Mach-O executable");
  }
  build->ctx = (MachOEmitContext){
    .program = program,
    .function_offsets = build->offsets,
    .function_count = program->function_len,
    .rodata_base_offset = build->rodata_base_offset,
    .pie_relative_data = true
  };
  build->start_call_patch = macho_emit_exe_start_stub(&build->text);
  macho_pad_to(&build->text, macho_align(build->text.len, 16));
  return true;
}

static bool macho_exe_emit_functions(MachOExeBuild *build, const IrProgram *program, ZDiag *diag) {
  for (size_t i = 0; i < program->function_len; i++) {
    macho_pad_to(&build->text, macho_align(build->text.len, 4));
    build->offsets[i] = build->text.len;
    if (!macho_emit_function_text(&build->text, &program->functions[i], &build->ctx, diag)) return false;
  }
  return true;
}

static bool macho_exe_validate_runtime(const MachOExeBuild *build, ZDiag *diag) {
  return !z_macho_has_unsupported_exe_runtime_patches(&build->ctx) || macho_diag_at(diag, "direct AArch64 Mach-O executable runtime helpers require object emission and an explicit runtime link step", 1, 1, "use --emit obj and link zero_runtime.c");
}

static void macho_exe_patch_branches(MachOExeBuild *build) {
  size_t world_write_offset = 0;
  if (z_macho_runtime_patch_count(&build->ctx, MACHO_RUNTIME_WORLD_WRITE) > 0) {
    macho_pad_to(&build->text, macho_align(build->text.len, 4));
    world_write_offset = macho_emit_exe_world_write(&build->text);
  }
  z_aarch64_patch_branch26(&build->text, build->start_call_patch, build->offsets[build->main_index]);
  for (size_t i = 0; i < build->ctx.call_patch_len; i++) {
    const MachOCallPatch *patch = &build->ctx.call_patches[i];
    z_aarch64_patch_branch26(&build->text, patch->patch_offset, build->offsets[patch->callee_index]);
  }
  const MachOPatchList *world_write_patches = z_macho_runtime_patch_list(&build->ctx, MACHO_RUNTIME_WORLD_WRITE);
  for (size_t i = 0; world_write_patches && i < world_write_patches->len; i++) {
    z_aarch64_patch_branch26(&build->text, world_write_patches->items[i].patch_offset, world_write_offset);
  }
}

static void macho_exe_patch_data(MachOExeBuild *build, const char *code_signature_id) {
  z_macho_compute_executable64_layout(&build->layout, &build->text, build->has_rodata ? &build->rodata : NULL, &build->rebase, code_signature_id);
  for (size_t i = 0; i < build->ctx.data_patch_len; i++) {
    const MachODataPatch *patch = &build->ctx.data_patches[i];
    uint64_t addr = build->layout.base_addr + build->layout.rodata_offset + (patch->data_offset - build->rodata_base_offset);
    z_aarch64_patch_adrp_add(&build->text, patch->patch_offset, build->layout.base_addr + build->layout.text_offset + patch->patch_offset, addr);
  }
  z_macho_compute_executable64_layout(&build->layout, &build->text, build->has_rodata ? &build->rodata : NULL, &build->rebase, code_signature_id);
}

static void macho_exe_write(const MachOExeBuild *build, ZBuf *out, const char *code_signature_id) {
  ZMachOExecutableImage image = { .text = &build->text, .rodata = build->has_rodata ? &build->rodata : NULL, .rebase = &build->rebase, .layout = build->layout, .code_signature_id = code_signature_id };
  z_macho_write_executable64(out, &image);
}

bool z_emit_macho64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return macho_diag(diag, "direct Mach-O executable backend received no program");
  unsigned main_index = 0;
  if (!macho_validate_exe_program(program, &main_index, diag)) return false;
  MachOExeBuild build;
  if (!macho_exe_build_init(&build, program, main_index, diag)) return false;
  bool ok = macho_exe_emit_functions(&build, program, diag) && macho_exe_validate_runtime(&build, diag);
  if (ok) {
    const char *code_signature_id = "zero-direct";
    macho_exe_patch_branches(&build);
    macho_exe_patch_data(&build, code_signature_id);
    macho_exe_write(&build, out, code_signature_id);
  }
  macho_exe_build_free(&build);
  return ok;
}
