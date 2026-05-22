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

static void append_bytes(ZBuf *buf, const char *bytes, size_t len);
static size_t macho_align(size_t value, size_t alignment);

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

static bool macho_diag(ZDiag *diag, const char *message) {
  return macho_diag_at(diag, message, 1, 1, "unsupported feature");
}

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

static bool macho_is_literal_return_function(const IrFunction *fun, uint32_t *out, ZDiag *diag) {
  if (!fun || fun->local_len != 0 || fun->instr_len != 1) return false;
  return macho_return_literal(fun, out, diag);
}

static size_t macho_align(size_t value, size_t alignment) {
  return z_macho_align(value, alignment);
}

static void macho_pad_to(ZBuf *buf, size_t offset) {
  z_macho_pad_to(buf, offset);
}

static bool macho_type_is_scalar32(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE;
}

static bool macho_type_is_scalar64(IrTypeKind type) {
  return type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool macho_type_is_unsigned(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_U32 || type == IR_TYPE_U64;
}

static bool macho_type_is_scalar(IrTypeKind type) {
  return macho_type_is_scalar32(type) || macho_type_is_scalar64(type);
}

static unsigned macho_slot_offset(unsigned local_index) {
  return local_index * 8;
}

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
  } else {
    macho_emit_load_local_w(text, fun, reg, local_index, field_offset, frame_size);
  }
}

static void macho_emit_store_field(ZBuf *text, const IrFunction *fun, unsigned reg, unsigned local_index, unsigned field_offset, IrTypeKind type, unsigned frame_size) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    macho_emit_store_local_b(text, fun, reg, local_index, field_offset, frame_size);
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

static bool macho_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static unsigned macho_cond_for_compare(IrCompareOp op) {
  switch (op) {
    case IR_CMP_EQ: return 0;
    case IR_CMP_NE: return 1;
    case IR_CMP_LT: return 11;
    case IR_CMP_LE: return 13;
    case IR_CMP_GT: return 12;
    case IR_CMP_GE: return 10;
  }
  return 0;
}

static unsigned macho_invert_cond(unsigned cond) {
  return cond ^ 1u;
}

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
static bool macho_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  return macho_emit_byte_view_ptr_at(text, fun, view, reg, frame_size, 0, ctx, diag);
}
static bool macho_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  return macho_emit_byte_view_len_at(text, fun, view, reg, frame_size, 0, ctx, diag);
}
static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag);
static bool macho_emit_value_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  return macho_emit_value_to_reg_at(text, fun, value, reg, frame_size, 0, ctx, diag);
}

static bool macho_emit_json_parse_bytes_call_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value ? value->left : NULL, diag)) return false;
  if (!macho_emit_byte_view_len_at(text, fun, value->left, 1, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value ? value->left : NULL, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_PARSE_BYTES, patch, value, diag);
}

static bool macho_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
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
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    unsigned end = 0;
    if ((!view->index || macho_const_u32_value(view->index, &start)) &&
        macho_const_u32_value(view->right, &end) && end >= start && end - start <= 65535) {
      z_aarch64_emit_movz_w(text, reg, end - start);
      return true;
    }
    if ((!view->index || macho_const_u32_value(view->index, &start)) && view->right) {
      if (!macho_emit_value_to_reg_at(text, fun, view->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
      if (start > 0) z_aarch64_emit_sub_w_imm(text, reg, reg, start);
      return true;
    }
    if (view->index && view->right) {
      unsigned tmp = reg == 8 ? 9 : 8;
      if (!macho_emit_value_to_reg_at(text, fun, view->right, reg, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, reg, view->right ? view->right->type : IR_TYPE_U32, scratch_slot, view->right, diag)) return false;
      if (!macho_emit_value_to_reg_at(text, fun, view->index, tmp, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, reg, view->right ? view->right->type : IR_TYPE_U32, scratch_slot, view->right, diag)) return false;
      macho_emit_binary_reg(text, IR_BIN_SUB, reg, reg, tmp, false);
      return true;
    }
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
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view array requires [N]u8", view->line, view->column, "unsupported array view");
    z_aarch64_emit_add_x_sp_imm(text, reg, macho_local_slot_offset(fun, view->array_index, 0, frame_size));
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
      if (start > 4095) return macho_diag_at(diag, "direct AArch64 Mach-O byte slice constant start is too large", view->line, view->column, "unsupported byte slice");
      if (start > 0) z_aarch64_emit_add_x_imm(text, reg, reg, start);
      return true;
    }
    unsigned tmp = reg == 8 ? 9 : 8;
    if (!macho_emit_store_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    if (!macho_emit_value_to_reg_at(text, fun, view->index, tmp, frame_size, scratch_slot + 1, ctx, diag)) return false;
    if (!macho_emit_load_scratch(text, reg, IR_TYPE_U64, scratch_slot, view, diag)) return false;
    z_aarch64_emit_add_x_reg(text, reg, reg, tmp);
    return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool macho_emit_call_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->arg_len > 8) return macho_diag_at(diag, "direct AArch64 Mach-O call supports at most eight arguments", value->line, value->column, "too many arguments");
  if (scratch_slot + value->arg_len >= MACHO_SCRATCH_SLOT_COUNT) {
    return macho_diag_at(diag, "direct AArch64 Mach-O call argument nesting exceeds scratch spill capacity", value->line, value->column, "too many nested call arguments");
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    if (!macho_emit_value_to_reg_at(text, fun, arg, 8, frame_size, scratch_slot + (unsigned)value->arg_len, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 8, arg ? arg->type : IR_TYPE_I32, scratch_slot + (unsigned)i, arg, diag)) return false;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    if (!macho_emit_load_scratch(text, (unsigned)i, arg ? arg->type : IR_TYPE_I32, scratch_slot + (unsigned)i, arg, diag)) return false;
  }
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_call_patch(ctx, patch, value->callee_index, value, diag)) return false;
  if (reg != 0) {
    if (macho_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool macho_emit_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value) return macho_diag_at(diag, "direct AArch64 Mach-O expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
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
    case IR_VALUE_BINARY:
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
    case IR_VALUE_COMPARE: {
      if (!value->left || !value->right) {
        return macho_diag_at(diag, "direct AArch64 Mach-O comparison requires two operands", value->line, value->column, "invalid comparison");
      }
      if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_value_to_reg_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
      z_aarch64_emit_cmp_w(text, 8, 9);
      z_aarch64_emit_movz_w(text, reg, 0);
      size_t false_patch = z_aarch64_emit_b_cond_placeholder(text, macho_invert_cond(macho_cond_for_compare(value->compare_op)));
      z_aarch64_emit_movz_w(text, reg, 1);
      z_aarch64_patch_cond19(text, false_patch, text->len);
      return true;
    }
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
    case IR_VALUE_HTTP_FETCH: {
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->left, 1, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->right, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->right, 3, frame_size, scratch_slot + 3, ctx, diag)) return false;
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
    case IR_VALUE_HTTP_RESULT_OK:
    case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR:
    case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN: {
      if (!macho_emit_value_to_reg_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: {
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->left, 1, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
    case IR_VALUE_HTTP_HEADER_VALUE: {
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->left, 1, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->right, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->right, 3, frame_size, scratch_slot + 3, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
      if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_HEADER_VALUE, patch, value, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
      return true;
    }
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      macho_emit_load_local_w(text, fun, reg, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12, frame_size);
      return true;
    case IR_VALUE_VEC_PUSH: {
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
    case IR_VALUE_BYTE_VIEW_LEN:
      return macho_emit_byte_view_len_at(text, fun, value->left, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: {
      unsigned const_index = 0;
      unsigned char byte = 0;
      if (macho_const_u32_value(value->index, &const_index) &&
          macho_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
        z_aarch64_emit_movz_w(text, reg, byte);
        return true;
      }
      if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->left, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
      z_aarch64_emit_cmp_w(text, 8, 9);
      size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
      z_aarch64_emit_brk(text);
      z_aarch64_patch_cond19(text, ok_patch, text->len);
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
      z_aarch64_emit_add_x_reg(text, 9, 9, 8);
      z_aarch64_emit_load_b_imm(text, reg, 9, 0);
      return true;
    }
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load array is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &fun->locals[value->array_index];
      unsigned const_index = 0;
      if (local->is_array && local->element_type != IR_TYPE_U8 && macho_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
        macho_emit_load_local_w(text, fun, reg, value->array_index, const_index * 4u, frame_size);
        return true;
      }
      if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
        if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
        if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
        z_aarch64_emit_movz_w(text, 9, local->array_len);
        z_aarch64_emit_cmp_w(text, 8, 9);
        size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
        z_aarch64_emit_brk(text);
        z_aarch64_patch_cond19(text, ok_patch, text->len);
        if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
        z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
        z_aarch64_emit_add_x_reg_lsl(text, 9, 9, 8, 2);
        z_aarch64_emit_load_w_imm(text, reg, 9, 0);
        return true;
      }
      if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O indexed load requires [N]u8 or integer arrays", value->line, value->column, "unsupported array local");
      if (!value->index || !macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
      z_aarch64_emit_movz_w(text, 9, local->array_len);
      z_aarch64_emit_cmp_w(text, 8, 9);
      size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
      z_aarch64_emit_brk(text);
      z_aarch64_patch_cond19(text, ok_patch, text->len);
      if (!macho_emit_load_scratch(text, 8, value->index ? value->index->type : IR_TYPE_U32, scratch_slot, value->index, diag)) return false;
      z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, value->array_index, 0, frame_size));
      z_aarch64_emit_add_x_reg(text, 9, 9, 8);
      z_aarch64_emit_load_b_imm(text, reg, 9, 0);
      return true;
    }
    case IR_VALUE_FIELD_LOAD:
      if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field load record is out of range", value->line, value->column, "invalid record local");
      if (!fun->locals[value->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field load requires record local", value->line, value->column, "non-record local");
      macho_emit_load_field(text, fun, reg, value->local_index, value->field_offset, value->type, frame_size);
      return true;
    case IR_VALUE_BYTE_VIEW_EQ: {
      if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view equality requires two byte views", value->line, value->column, "missing byte view");
      if (!macho_emit_byte_view_len_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->right, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
      z_aarch64_emit_cmp_w(text, 8, 9);
      size_t same_len = z_aarch64_emit_b_cond_placeholder(text, 0);
      z_aarch64_emit_movz_w(text, reg, 0);
      size_t end_patch_neq = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, same_len, text->len);
      size_t zero_len = z_aarch64_emit_cbz_w_placeholder(text, 8);
      if (!macho_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 10, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->right, 11, frame_size, scratch_slot + 2, ctx, diag)) return false;
      if (!macho_emit_load_scratch(text, 10, IR_TYPE_U64, scratch_slot + 1, value->left, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
      size_t loop = text->len;
      z_aarch64_emit_load_b_imm(text, 12, 10, 0);
      z_aarch64_emit_load_b_imm(text, 13, 11, 0);
      z_aarch64_emit_cmp_w(text, 12, 13);
      size_t mismatch = z_aarch64_emit_b_cond_placeholder(text, 1);
      z_aarch64_emit_add_x_imm(text, 10, 10, 1);
      z_aarch64_emit_add_x_imm(text, 11, 11, 1);
      z_aarch64_emit_sub_w_imm(text, 8, 8, 1);
      size_t loop_back_patch = text->len;
      z_aarch64_append_u32(text, 0x35000000u | (8u & 31u));
      z_aarch64_patch_cond19(text, loop_back_patch, loop);
      z_aarch64_patch_cond19(text, zero_len, text->len);
      z_aarch64_emit_movz_w(text, reg, 1);
      size_t end_patch_eq = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, mismatch, text->len);
      z_aarch64_emit_movz_w(text, reg, 0);
      z_aarch64_patch_branch26(text, end_patch_eq, text->len);
      z_aarch64_patch_branch26(text, end_patch_neq, text->len);
      return true;
    }
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_IS_DIR:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_REMOVE_DIR: {
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
      switch (value->kind) {
        case IR_VALUE_FS_EXISTS:
          z_aarch64_emit_movz_w(text, 1, 0);                          // mode = F_OK
          z_aarch64_emit_movz_x(text, 16, 0x02000021ull);              // SYS_access
          z_aarch64_emit_svc(text, 0x80);
          break;
        case IR_VALUE_FS_MAKE_DIR:
          z_aarch64_emit_movz_w(text, 1, 0755u);                       // mode
          z_aarch64_emit_movz_x(text, 16, 0x02000088ull);              // SYS_mkdir
          z_aarch64_emit_svc(text, 0x80);
          break;
        case IR_VALUE_FS_REMOVE:
          z_aarch64_emit_movz_x(text, 16, 0x0200000aull);              // SYS_unlink
          z_aarch64_emit_svc(text, 0x80);
          break;
        case IR_VALUE_FS_REMOVE_DIR:
          z_aarch64_emit_movz_x(text, 16, 0x02000089ull);              // SYS_rmdir
          z_aarch64_emit_svc(text, 0x80);
          break;
        case IR_VALUE_FS_IS_DIR: {
          z_aarch64_emit_sub_sp_imm(text, 144);
          z_aarch64_emit_add_x_imm(text, 1, 31, 0);                    // mov x1, sp
          z_aarch64_emit_movz_x(text, 16, 0x02000152ull);              // SYS_stat64
          z_aarch64_emit_svc(text, 0x80);
          size_t fail = z_aarch64_emit_b_cond_placeholder(text, 2);    // b.cs fail
          z_aarch64_append_u32(text, 0x79400be1u);                     // ldrh w1, [sp, #4]
          z_aarch64_emit_movz_w(text, 2, 0170000u);                    // S_IFMT
          z_aarch64_append_u32(text, 0x0a020021u);                     // and w1, w1, w2
          z_aarch64_emit_movz_w(text, 2, 0040000u);                    // S_IFDIR
          z_aarch64_emit_cmp_w(text, 1, 2);
          size_t neq = z_aarch64_emit_b_cond_placeholder(text, 1);     // b.ne neq
          z_aarch64_emit_movz_w(text, 0, 1);
          size_t end_eq = z_aarch64_emit_b_placeholder(text);
          z_aarch64_patch_cond19(text, fail, text->len);
          z_aarch64_patch_cond19(text, neq, text->len);
          z_aarch64_emit_movz_w(text, 0, 0);
          z_aarch64_patch_branch26(text, end_eq, text->len);
          z_aarch64_emit_add_sp_imm(text, 144);
          if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
          return true;
        }
        default: break;
      }
      size_t err = z_aarch64_emit_b_cond_placeholder(text, 2);         // b.cs err
      z_aarch64_emit_movz_w(text, 0, 1);
      size_t end_ok = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, err, text->len);
      z_aarch64_emit_movz_w(text, 0, 0);
      z_aarch64_patch_branch26(text, end_ok, text->len);
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH: {
      // Stash data pointer and length before clobbering x0/x1/x2 with open args.
      if (!macho_emit_byte_view_ptr_at(text, fun, value->right, 19, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 19, IR_TYPE_U64, scratch_slot, value->right, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->right, 20, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 20, IR_TYPE_U32, scratch_slot + 1, value->right, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot + 2, ctx, diag)) return false;
      z_aarch64_emit_movz_w(text, 1, 0x0601u);                      // O_WRONLY|O_CREAT|O_TRUNC
      z_aarch64_emit_movz_w(text, 2, 0644u);
      z_aarch64_emit_movz_x(text, 16, 0x02000005ull);                // SYS_open
      z_aarch64_emit_svc(text, 0x80);
      size_t open_failed = z_aarch64_emit_b_cond_placeholder(text, 2);
      macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->right, diag);
      macho_emit_load_scratch(text, 2, IR_TYPE_U32, scratch_slot + 1, value->right, diag);
      z_aarch64_emit_mov_x(text, 19, 0);                              // save fd in x19
      z_aarch64_emit_movz_x(text, 16, 0x02000004ull);                // SYS_write
      z_aarch64_emit_svc(text, 0x80);
      z_aarch64_emit_mov_x(text, 20, 0);                              // save bytes written
      z_aarch64_emit_mov_x(text, 0, 19);
      z_aarch64_emit_movz_x(text, 16, 0x02000006ull);                // SYS_close
      z_aarch64_emit_svc(text, 0x80);
      z_aarch64_emit_mov_x(text, 0, 20);
      size_t end_ok = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, open_failed, text->len);
      z_aarch64_emit_movz_w(text, 0, 0);
      z_aarch64_patch_branch26(text, end_ok, text->len);
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH: {
      if (!macho_emit_byte_view_ptr_at(text, fun, value->right, 19, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 19, IR_TYPE_U64, scratch_slot, value->right, diag)) return false;
      if (!macho_emit_byte_view_len_at(text, fun, value->right, 20, frame_size, scratch_slot + 1, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 20, IR_TYPE_U32, scratch_slot + 1, value->right, diag)) return false;
      if (!macho_emit_byte_view_ptr_at(text, fun, value->left, 0, frame_size, scratch_slot + 2, ctx, diag)) return false;
      z_aarch64_emit_movz_w(text, 1, 0);                              // O_RDONLY
      z_aarch64_emit_movz_w(text, 2, 0);
      z_aarch64_emit_movz_x(text, 16, 0x02000005ull);                // SYS_open
      z_aarch64_emit_svc(text, 0x80);
      size_t open_failed = z_aarch64_emit_b_cond_placeholder(text, 2);
      macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->right, diag);
      macho_emit_load_scratch(text, 2, IR_TYPE_U32, scratch_slot + 1, value->right, diag);
      z_aarch64_emit_mov_x(text, 19, 0);
      z_aarch64_emit_movz_x(text, 16, 0x02000003ull);                // SYS_read
      z_aarch64_emit_svc(text, 0x80);
      z_aarch64_emit_mov_x(text, 20, 0);
      z_aarch64_emit_mov_x(text, 0, 19);
      z_aarch64_emit_movz_x(text, 16, 0x02000006ull);                // SYS_close
      z_aarch64_emit_svc(text, 0x80);
      z_aarch64_emit_mov_x(text, 0, 20);
      size_t end_ok = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, open_failed, text->len);
      z_aarch64_emit_movz_w(text, 0, 0);
      z_aarch64_patch_branch26(text, end_ok, text->len);
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
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
  if (!macho_emit_byte_view_ptr(text, fun, instr->value, 1, frame_size, ctx, diag)) return false;
  if (!macho_emit_byte_view_len(text, fun, instr->value, 2, frame_size, ctx, diag)) return false;
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

static bool macho_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return macho_emit_world_write(text, fun, instr, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) {
    if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O local store is out of range", instr->line, instr->column, "invalid local");
    if (fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_byte_view_ptr(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_ALLOC) {
      if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return macho_diag_at(diag, "direct AArch64 Mach-O FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
      if (!macho_emit_byte_view_ptr(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      z_aarch64_emit_movz_w(text, 8, 0);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 12, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return macho_diag_at(diag, "direct AArch64 Mach-O Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!macho_emit_byte_view_ptr(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
      z_aarch64_emit_movz_w(text, 8, 0);
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 8, frame_size);
      if (!macho_emit_byte_view_len(text, fun, instr->value->left, 8, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 8, instr->local_index, 12, frame_size);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (instr->value && instr->value->kind == IR_VALUE_ARGS_GET) {
        return macho_emit_args_get_to_local(text, fun, instr->value, &fun->locals[instr->local_index], frame_size, ctx, diag);
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
    if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_SCALAR) {
      if (!instr->value) return macho_diag_at(diag, "direct AArch64 Mach-O Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
      if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
        z_aarch64_emit_movz_w(text, 8, instr->value->data_len ? 1u : 0u);
        macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
        z_aarch64_emit_movz_x(text, 8, (uint64_t)instr->value->int_value);
        macho_emit_store_local_x(text, fun, 8, instr->local_index, 8, frame_size);
        return true;
      }
      if (instr->value->kind == IR_VALUE_JSON_PARSE_BYTES) {
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
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    if (macho_type_is_scalar64(fun->locals[instr->local_index].type)) macho_emit_store_local_x(text, fun, 8, instr->local_index, 0, frame_size);
    else macho_emit_store_local_w(text, fun, 8, instr->local_index, 0, frame_size);
    return true;
  }
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
    unsigned const_index = 0;
    if (local->is_array && local->element_type != IR_TYPE_U8 && macho_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
      macho_emit_store_local_w(text, fun, 10, instr->array_index, const_index * 4u, frame_size);
      return true;
    }
    if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
      if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
      if (!instr->index || !macho_emit_value_to_reg(text, fun, instr->index, 8, frame_size, ctx, diag)) return false;
      z_aarch64_emit_movz_w(text, 9, local->array_len);
      z_aarch64_emit_cmp_w(text, 8, 9);
      size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
      z_aarch64_emit_brk(text);
      z_aarch64_patch_cond19(text, ok_patch, text->len);
      z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
      z_aarch64_emit_add_x_reg_lsl(text, 9, 9, 8, 2);
      z_aarch64_emit_store_w_imm(text, 10, 9, 0);
      return true;
    }
    if (!local->is_array || local->element_type != IR_TYPE_U8) return macho_diag_at(diag, "direct AArch64 Mach-O indexed store requires [N]u8 or integer arrays", instr->line, instr->column, "unsupported array local");
    if (!macho_emit_value_to_reg(text, fun, instr->value, 10, frame_size, ctx, diag)) return false;
    if (!instr->index || !macho_emit_value_to_reg(text, fun, instr->index, 8, frame_size, ctx, diag)) return false;
    z_aarch64_emit_movz_w(text, 9, local->array_len);
    z_aarch64_emit_cmp_w(text, 8, 9);
    size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
    z_aarch64_emit_brk(text);
    z_aarch64_patch_cond19(text, ok_patch, text->len);
    z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, instr->array_index, 0, frame_size));
    z_aarch64_emit_add_x_reg(text, 9, 9, 8);
    z_aarch64_emit_store_b_imm(text, 10, 9, 0);
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    return !instr->value || macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_RETURN) {
    if (instr->value && !macho_emit_value_to_reg(text, fun, instr->value, 0, frame_size, ctx, diag)) return false;
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
  if (fun->param_count > 8) return macho_diag_at(diag, "direct AArch64 Mach-O object backend supports at most eight parameters", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar(fun->return_type)) {
    return macho_diag_at(diag, "direct AArch64 Mach-O object backend currently supports only Void and primitive integer returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      if (fun->locals[i].is_param) {
        return macho_diag_at(diag, "direct AArch64 Mach-O object backend does not yet support byte-view parameters", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
      }
      continue;
    }
    if (fun->locals[i].is_array && (fun->locals[i].element_type == IR_TYPE_U8 || fun->locals[i].element_type == IR_TYPE_U32 || fun->locals[i].element_type == IR_TYPE_I32 || fun->locals[i].element_type == IR_TYPE_USIZE)) continue;
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
  bool seed_process_args = ctx && ctx->seed_main_process_args && fun->is_exported && fun->name && strcmp(fun->name, "main") == 0;
  if (seed_process_args) z_aarch64_emit_stp_x20_x21_sp_pre16(text);
  z_aarch64_emit_stp_x29_x30_sp_pre16(text);
  z_aarch64_emit_mov_x29_sp(text);
  if (frame_size > 0) z_aarch64_emit_sub_sp_imm(text, frame_size);
  if (seed_process_args) {
    z_aarch64_emit_mov_x(text, 20, 0);
    z_aarch64_emit_mov_x(text, 21, 1);
  }
  for (size_t i = 0; i < fun->param_count; i++) {
    if (macho_type_is_scalar64(fun->locals[i].type)) macho_emit_store_local_x(text, fun, (unsigned)i, (unsigned)i, 0, frame_size);
    else macho_emit_store_local_w(text, fun, (unsigned)i, (unsigned)i, 0, frame_size);
  }
  if (!macho_emit_instrs(text, fun, fun->instrs, fun->instr_len, frame_size, seed_process_args, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) macho_emit_epilogue(text, frame_size, seed_process_args);
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

bool z_emit_macho64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return macho_diag(diag, "direct Mach-O backend received no program");
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

  ZBuf text;
  ZBuf rodata;
  ZBuf relocs;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&relocs);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = macho_rodata_base_offset(program);
  if (has_rodata) macho_append_rodata(&rodata, program, rodata_base_offset);
  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  uint32_t *string_offsets = z_checked_calloc(program->function_len, sizeof(uint32_t));
  if (!offsets) {
    free(string_offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O object");
  }
  if (!string_offsets) {
    free(offsets);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O symbols");
  }

  ZBuf strings;
  zbuf_init(&strings);
  append_u8(&strings, 0);
  MachOEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .pie_relative_data = true,
    .seed_main_process_args = true
  };
  for (size_t i = 0; i < program->function_len; i++) {
    const IrFunction *fun = &program->functions[i];
    macho_pad_to(&text, macho_align(text.len, 4));
    offsets[i] = text.len;
    if (!macho_emit_function_text(&text, fun, &ctx, diag)) {
      zbuf_free(&strings);
      free(string_offsets);
      free(offsets);
      z_macho_emit_context_free(&ctx);
      zbuf_free(&relocs);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
    string_offsets[i] = (uint32_t)strings.len;
    zbuf_append_char(&strings, '_');
    zbuf_append(&strings, fun->name ? fun->name : "zero_fn");
    append_u8(&strings, 0);
  }
  z_macho_append_call_relocations(&relocs, &ctx);
  if (has_rodata) {
    z_macho_append_data_relocations(&relocs, &ctx, (unsigned)program->function_len);
  }
  uint32_t next_runtime_symbol = (uint32_t)program->function_len + (has_rodata ? 1u : 0u);
  uint32_t runtime_symbol_indices[MACHO_RUNTIME_HELPER_COUNT] = {0};
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    runtime_symbol_indices[helper] = next_runtime_symbol++;
    z_macho_append_runtime_relocations(&relocs, &ctx, runtime_helper, runtime_symbol_indices[helper]);
  }

  const uint32_t const_addr = has_rodata ? (uint32_t)macho_align(text.len, 8) : 0;
  const uint32_t nsyms = next_runtime_symbol;
  uint32_t rodata_string_offset = 0;
  if (has_rodata) {
    rodata_string_offset = (uint32_t)strings.len;
    zbuf_append(&strings, "l_.zero_rodata");
    append_u8(&strings, 0);
  }
  uint32_t runtime_string_offsets[MACHO_RUNTIME_HELPER_COUNT] = {0};
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    runtime_string_offsets[helper] = (uint32_t)strings.len;
    zbuf_append(&strings, z_macho_runtime_helper_symbol(runtime_helper));
    append_u8(&strings, 0);
  }

  ZMachOSymbol *symbols = z_checked_calloc(nsyms, sizeof(ZMachOSymbol));
  if (!symbols) {
    zbuf_free(&strings);
    free(string_offsets);
    free(offsets);
    z_macho_emit_context_free(&ctx);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O symbols");
  }
  size_t symbol_len = 0;
  for (size_t i = 0; i < program->function_len; i++) {
    symbols[symbol_len++] = (ZMachOSymbol){
      .string_offset = string_offsets[i],
      .type = program->functions[i].is_exported ? 0x0f : 0x0e,
      .section = 1,
      .value = offsets[i]
    };
  }
  if (has_rodata) {
    symbols[symbol_len++] = (ZMachOSymbol){
      .string_offset = rodata_string_offset,
      .type = 0x0e,
      .section = 2,
      .value = const_addr
    };
  }
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    symbols[symbol_len++] = (ZMachOSymbol){ .string_offset = runtime_string_offsets[helper], .type = 0x01 };
  }

  const uint32_t text_reloc_count = (uint32_t)z_macho_text_relocation_count(&ctx);
  ZMachOObjectImage image = {
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .relocs = &relocs,
    .strings = &strings,
    .symbols = symbols,
    .symbol_len = symbol_len,
    .text_reloc_count = text_reloc_count
  };
  z_macho_write_object64(out, &image);
  free(symbols);

  z_macho_emit_context_free(&ctx);
  free(string_offsets);
  free(offsets);
  zbuf_free(&strings);
  zbuf_free(&relocs);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}

static const IrFunction *macho_find_executable_main(const IrProgram *program, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    if (program->functions[i].is_exported && strcmp(program->functions[i].name, "main") == 0) {
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
  if (fun->return_type != IR_TYPE_VOID && !macho_type_is_scalar32(fun->return_type)) {
    macho_diag_at(diag, "direct AArch64 Mach-O executable main must return Void, i32, or u32", fun->line, fun->column, fun->name);
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

bool z_emit_macho64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return macho_diag(diag, "direct Mach-O executable backend received no program");
  if (!program->mir_valid) {
    bool ok = macho_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  unsigned main_index = 0;
  if (!macho_find_executable_main(program, diag, &main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (!macho_validate_function(&program->functions[i], diag)) return false;
  }

  ZBuf text;
  ZBuf rodata;
  zbuf_init(&text);
  zbuf_init(&rodata);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = macho_rodata_base_offset(program);
  if (has_rodata) macho_append_rodata(&rodata, program, rodata_base_offset);

  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!offsets) {
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag(diag, "out of memory while emitting Mach-O executable");
  }

  MachOEmitContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .pie_relative_data = true
  };
  size_t start_call_patch = macho_emit_exe_start_stub(&text);
  macho_pad_to(&text, macho_align(text.len, 16));
  for (size_t i = 0; i < program->function_len; i++) {
    macho_pad_to(&text, macho_align(text.len, 4));
    offsets[i] = text.len;
    if (!macho_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      z_macho_emit_context_free(&ctx);
      free(offsets);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
  }

  if (z_macho_has_unsupported_exe_runtime_patches(&ctx)) {
    z_macho_emit_context_free(&ctx);
    free(offsets);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return macho_diag_at(diag, "direct AArch64 Mach-O executable runtime helpers require object emission and an explicit runtime link step", 1, 1, "use --emit obj and link zero_runtime.c");
  }

  size_t world_write_offset = 0;
  if (z_macho_runtime_patch_count(&ctx, MACHO_RUNTIME_WORLD_WRITE) > 0) {
    macho_pad_to(&text, macho_align(text.len, 4));
    world_write_offset = macho_emit_exe_world_write(&text);
  }
  z_aarch64_patch_branch26(&text, start_call_patch, offsets[main_index]);
  for (size_t i = 0; i < ctx.call_patch_len; i++) {
    const MachOCallPatch *patch = &ctx.call_patches[i];
    z_aarch64_patch_branch26(&text, patch->patch_offset, offsets[patch->callee_index]);
  }
  const MachOPatchList *world_write_patches = z_macho_runtime_patch_list(&ctx, MACHO_RUNTIME_WORLD_WRITE);
  for (size_t i = 0; world_write_patches && i < world_write_patches->len; i++) {
    z_aarch64_patch_branch26(&text, world_write_patches->items[i].patch_offset, world_write_offset);
  }

  const char *code_signature_id = "zero-direct";
  ZBuf rebase;
  zbuf_init(&rebase);
  ZMachOExecutableLayout layout;
  z_macho_compute_executable64_layout(&layout, &text, has_rodata ? &rodata : NULL, &rebase, code_signature_id);
  for (size_t i = 0; i < ctx.data_patch_len; i++) {
    const MachODataPatch *patch = &ctx.data_patches[i];
    uint64_t addr = layout.base_addr + layout.rodata_offset + (patch->data_offset - rodata_base_offset);
    if (ctx.pie_relative_data) z_aarch64_patch_adrp_add(&text, patch->patch_offset, layout.base_addr + layout.text_offset + patch->patch_offset, addr);
    else z_macho_patch_u64(&text, patch->patch_offset, addr);
  }
  if (ctx.data_patch_len > 0 && !ctx.pie_relative_data) {
    append_u8(&rebase, 0x11); // REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER
    for (size_t i = 0; i < ctx.data_patch_len; i++) {
      append_u8(&rebase, 0x21); // REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB, __TEXT segment
      z_macho_append_uleb128(&rebase, layout.text_offset + ctx.data_patches[i].patch_offset);
      append_u8(&rebase, 0x51); // REBASE_OPCODE_DO_REBASE_IMM_TIMES, once
    }
    append_u8(&rebase, 0x00);
  }
  z_macho_compute_executable64_layout(&layout, &text, has_rodata ? &rodata : NULL, &rebase, code_signature_id);
  ZMachOExecutableImage image = {
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .rebase = &rebase,
    .layout = layout,
    .code_signature_id = code_signature_id
  };
  z_macho_write_executable64(out, &image);

  z_macho_emit_context_free(&ctx);
  free(offsets);
  zbuf_free(&rebase);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}
