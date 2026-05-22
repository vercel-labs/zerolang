#include "zero.h"
#include "coff_emit_state.h"
#include "coff_format.h"
#include "x64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool coff_type_is_scalar32(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE;
}

static unsigned coff_local_offset(const IrFunction *fun, unsigned local_index) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0) return fun->locals[local_index].frame_offset;
  return (local_index + 1) * 8;
}

static unsigned coff_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  unsigned offset = coff_local_offset(fun, local_index);
  return offset >= slot_offset ? offset - slot_offset : offset;
}

static void coff_emit_load_local_eax(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_offset(fun, local_index), false);
}

static void coff_emit_load_local_slot_rax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), true);
}

static void coff_emit_load_local_slot_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), false);
}

static void coff_emit_load_local_slot_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned reg, bool wide) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide);
}

static void coff_emit_store_local_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg) {
  z_x64_emit_rbp_disp_reg(text, 0x89, reg, coff_local_offset(fun, local_index), false);
}

static void coff_emit_store_local_slot_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg, unsigned slot_offset, bool wide) {
  z_x64_emit_rbp_disp_reg(text, 0x89, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide);
}

static void coff_emit_load_field_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_append_u8(text, 0x0f);
    z_x64_emit_rbp_disp_reg(text, 0xb6, 0, offset, false);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x8b, 0, offset, false);
  }
}

static void coff_emit_store_field_from_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_emit_rbp_disp_reg(text, 0x88, 0, offset, false);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, false);
  }
}

static void coff_emit_array_base_rdx(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  z_x64_emit_rbp_disp_reg(text, 0x8d, 2, coff_local_offset(fun, local_index), true);
}

static void coff_emit_u8_array_bounds_check(ZBuf *text, const IrLocal *local) {
  z_x64_append_u8(text, 0x3d);
  z_x64_append_u32(text, local ? local->array_len : 0);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
}

static void coff_emit_epilogue(ZBuf *text) {
  z_x64_emit_epilogue(text);
}

static unsigned coff_setcc_opcode(IrCompareOp op) {
  switch (op) {
    case IR_CMP_EQ: return 0x94;
    case IR_CMP_NE: return 0x95;
    case IR_CMP_LT: return 0x9c;
    case IR_CMP_LE: return 0x9e;
    case IR_CMP_GT: return 0x9f;
    case IR_CMP_GE: return 0x9d;
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
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0xb8);
  size_t patch = text->len;
  uint64_t addend = data_offset - (ctx ? ctx->rodata_base_offset : 0);
  for (unsigned i = 0; i < 8; i++) z_x64_append_u8(text, (unsigned)((addend >> (i * 8)) & 0xffu));
  return z_coff_record_rodata_patch(ctx, patch, data_offset, value, diag);
}

static bool coff_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);
static bool coff_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);

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
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    coff_emit_load_local_slot_eax(text, fun, view->local_index, 16);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->index && view->right) {
    unsigned start = 0;
    unsigned end = 0;
    if (coff_const_u32_value(view->index, &start) && coff_const_u32_value(view->right, &end) && start <= end) {
      z_x64_emit_mov_eax_u32(text, end - start);
      return true;
    }
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
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    coff_emit_load_local_slot_rax(text, fun, view->local_index, 8);
    return true;
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return coff_diag_at(diag, "direct COFF byte-view array requires [N]u8", view->line, view->column, "unsupported array view");
    z_x64_emit_rbp_disp_reg(text, 0x8d, 0, coff_local_offset(fun, view->array_index), true);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    return coff_emit_rodata_ptr_rax(text, view->data_offset, ctx, view, diag);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned start = 0;
    if (!coff_const_u32_value(view->index, &start)) return coff_diag_at(diag, "direct COFF byte slice currently requires a constant start", view->line, view->column, "unsupported byte slice");
    if (!coff_emit_byte_view_ptr(text, fun, view->left, ctx, diag)) return false;
    if (start > 0) {
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x05);
      z_x64_append_u32(text, start);
    }
    return true;
  }
  return coff_diag_at(diag, "direct COFF value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool coff_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, CoffEmitContext *ctx, ZDiag *diag) {
  if (!value) return coff_diag_at(diag, "direct COFF expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      z_x64_emit_mov_eax_u32(text, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL:
      if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF local index is out of range", value->line, value->column, "invalid local");
      if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) {
        return coff_diag_at(diag, "direct COFF byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
      }
      coff_emit_load_local_eax(text, fun, value->local_index);
      return true;
    case IR_VALUE_BINARY:
      if (value->binary_op != IR_BIN_ADD && value->binary_op != IR_BIN_SUB && value->binary_op != IR_BIN_MUL) return coff_diag_at(diag, "direct COFF binary operator is unsupported", value->line, value->column, "unsupported operator");
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(text);
      if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(text, false);
      z_x64_emit_pop_rax(text);
      if (value->binary_op == IR_BIN_ADD) {
        z_x64_emit_add_rax_rcx(text, false);
      } else if (value->binary_op == IR_BIN_SUB) {
        z_x64_emit_sub_rax_rcx(text, false);
      } else {
        z_x64_emit_imul_rax_rcx(text, false);
      }
      return true;
    case IR_VALUE_COMPARE:
      if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF comparison requires two operands", value->line, value->column, "invalid comparison");
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(text);
      if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(text, false);
      z_x64_emit_pop_rax(text);
      z_x64_emit_cmp_rax_rcx_to_bool(text, coff_setcc_opcode(value->compare_op), false);
      return true;
    case IR_VALUE_CALL: {
      static const unsigned param_regs[] = {1, 2, 8, 9};
      if (value->arg_len > 4) return coff_diag_at(diag, "direct COFF call supports at most four integer arguments", value->line, value->column, "too many arguments");
      for (size_t i = 0; i < value->arg_len; i++) {
        if (!coff_emit_value(text, fun, value->args[i], ctx, diag)) return false;
        z_x64_append_u8(text, 0x50);
      }
      for (size_t i = value->arg_len; i > 0; i--) {
        z_x64_emit_pop_reg64(text, param_regs[i - 1]);
      }
      z_x64_emit_sub_rsp(text, 32);
      size_t patch = z_x64_emit_call32_placeholder(text);
      z_x64_emit_add_rsp(text, 32);
      return z_coff_record_call_patch(ctx, patch, value->callee_index, value, diag);
    }
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      coff_emit_load_local_slot_eax(text, fun, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12);
      return true;
    case IR_VALUE_VEC_PUSH: {
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return coff_diag_at(diag, "direct COFF Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
      coff_emit_load_local_slot_eax(text, fun, value->local_index, 8);
      coff_emit_load_local_slot_reg(text, fun, value->local_index, 12, 1, false);
      z_x64_emit_cmp_rax_rcx(text, false);
      size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
      z_x64_emit_mov_eax_u32(text, 0);
      size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
      z_x64_patch_rel32(text, ok_patch, text->len);
      z_x64_append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      z_x64_append_u8(text, 0x59);
      coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xca);
      z_x64_append_u8(text, 0x88);
      z_x64_append_u8(text, 0x02);
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0xc8);
      z_x64_append_u8(text, 0x83);
      z_x64_append_u8(text, 0xc0);
      z_x64_append_u8(text, 0x01);
      coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
      z_x64_emit_mov_eax_u32(text, 1);
      z_x64_patch_rel32(text, end_patch, text->len);
      return true;
    }
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW) return coff_diag_at(diag, "direct COFF maybe helper requires a Maybe<MutSpan<u8>> local", value->line, value->column, "invalid maybe local");
      coff_emit_load_local_slot_eax(text, fun, value->local_index, 0);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN:
      return coff_emit_byte_view_len(text, fun, value->left, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: {
      unsigned const_index = 0;
      unsigned char byte = 0;
      if (coff_const_u32_value(value->index, &const_index) &&
          coff_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
        z_x64_emit_mov_eax_u32(text, byte);
        return true;
      }
      if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
      z_x64_append_u8(text, 0x50);
      if (!coff_emit_byte_view_len(text, fun, value->left, ctx, diag)) return false;
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0xc1);
      z_x64_append_u8(text, 0x58);
      z_x64_emit_cmp_rax_rcx(text, false);
      size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
      z_x64_emit_ud2(text);
      z_x64_patch_rel32(text, ok_patch, text->len);
      z_x64_append_u8(text, 0x50);
      if (!coff_emit_byte_view_ptr(text, fun, value->left, ctx, diag)) return false;
      z_x64_append_u8(text, 0x59);
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xc8);
      z_x64_append_u8(text, 0x0f);
      z_x64_append_u8(text, 0xb6);
      z_x64_append_u8(text, 0x00);
      return true;
    }
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= fun->local_len) return coff_diag_at(diag, "direct COFF indexed load array is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &fun->locals[value->array_index];
      unsigned const_index = 0;
      if (local->is_array && local->element_type != IR_TYPE_U8 && coff_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
        coff_emit_load_local_slot_eax(text, fun, value->array_index, const_index * 4u);
        return true;
      }
      if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
        if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
        coff_emit_u8_array_bounds_check(text, local);
        z_x64_append_u8(text, 0x50);
        coff_emit_array_base_rdx(text, fun, value->array_index);
        z_x64_append_u8(text, 0x59);
        z_x64_append_u8(text, 0x48);
        z_x64_append_u8(text, 0xc1);
        z_x64_append_u8(text, 0xe1);
        z_x64_append_u8(text, 0x02);
        z_x64_append_u8(text, 0x48);
        z_x64_append_u8(text, 0x01);
        z_x64_append_u8(text, 0xca);
        z_x64_append_u8(text, 0x8b);
        z_x64_append_u8(text, 0x02);
        return true;
      }
      if (!local->is_array || local->element_type != IR_TYPE_U8) return coff_diag_at(diag, "direct COFF indexed load requires [N]u8 or integer arrays", value->line, value->column, "unsupported array local");
      if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
      coff_emit_u8_array_bounds_check(text, local);
      z_x64_append_u8(text, 0x50);
      coff_emit_array_base_rdx(text, fun, value->array_index);
      z_x64_append_u8(text, 0x59);
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xca);
      z_x64_append_u8(text, 0x0f);
      z_x64_append_u8(text, 0xb6);
      z_x64_append_u8(text, 0x02);
      return true;
    }
    case IR_VALUE_FIELD_LOAD:
      if (value->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF field load record is out of range", value->line, value->column, "invalid record local");
      if (!fun->locals[value->local_index].is_record) return coff_diag_at(diag, "direct COFF field load requires record local", value->line, value->column, "non-record local");
      coff_emit_load_field_eax(text, fun, value->local_index, value->field_offset, value->type);
      return true;
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
  if (!coff_emit_byte_view_ptr(text, fun, instr->value, ctx, diag)) return false;
  z_x64_append_u8(text, 0x50); // push rax, preserve ptr while computing len
  if (!coff_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
  z_x64_append_u8(text, 0x41);
  z_x64_append_u8(text, 0x89);
  z_x64_append_u8(text, 0xc0); // mov r8d, eax
  z_x64_append_u8(text, 0x5a); // pop rdx
  z_x64_append_u8(text, 0xb9);
  z_x64_append_u32(text, instr->field_offset == 2 ? 2u : 1u); // ecx = fd
  z_x64_emit_sub_rsp(text, 32);
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_add_rsp(text, 32);
  z_x64_emit_test_rax_rax(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_ud2(text); // ud2 on runtime write failure
  z_x64_patch_rel32(text, ok_patch, text->len);
  return z_coff_record_instr_runtime_patch(ctx, COFF_RUNTIME_WORLD_WRITE, patch, instr, diag);
}

static bool coff_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, CoffEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return coff_emit_world_write(text, fun, instr, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) {
    if (instr->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF local store is out of range", instr->line, instr->column, "invalid local");
    if (fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
      if (!coff_emit_byte_view_ptr(text, fun, instr->value, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
      if (!coff_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, false);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_ALLOC) {
      if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return coff_diag_at(diag, "direct COFF FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
      if (!coff_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
      if (!coff_emit_byte_view_len(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, false);
      z_x64_emit_mov_eax_u32(text, 0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 12, false);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return coff_diag_at(diag, "direct COFF Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!coff_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
      z_x64_emit_mov_eax_u32(text, 0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, false);
      if (!coff_emit_byte_view_len(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 12, false);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return coff_diag_at(diag, "direct COFF allocation source is invalid", instr->line, instr->column, "invalid allocation");
      if (!coff_emit_value(text, fun, instr->value->left, ctx, diag)) return false;
      z_x64_append_u8(text, 0x50);
      coff_emit_load_local_slot_eax(text, fun, instr->value->local_index, 12);
      coff_emit_load_local_slot_reg(text, fun, instr->value->local_index, 8, 1, false);
      z_x64_append_u8(text, 0x58);
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0xc2);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xc8);
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
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xd2);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0xd0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0xc8);
      coff_emit_store_local_slot_from_reg(text, fun, instr->value->local_index, 0, 12, false);
      z_x64_patch_rel32(text, end_patch, text->len);
      return true;
    }
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_local_from_reg(text, fun, instr->local_index, 0);
    return true;
  }
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    if (instr->local_index >= fun->local_len) return coff_diag_at(diag, "direct COFF field store record is out of range", instr->line, instr->column, "invalid record local");
    if (!fun->locals[instr->local_index].is_record) return coff_diag_at(diag, "direct COFF field store requires record local", instr->line, instr->column, "non-record local");
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_store_field_from_eax(text, fun, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32);
    return true;
  }
  if (instr->kind == IR_INSTR_INDEX_STORE) {
    if (instr->array_index >= fun->local_len) return coff_diag_at(diag, "direct COFF indexed store array is out of range", instr->line, instr->column, "invalid array local");
    const IrLocal *local = &fun->locals[instr->array_index];
    unsigned const_index = 0;
    if (local->is_array && local->element_type != IR_TYPE_U8 && coff_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
      if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->array_index, 0, const_index * 4u, false);
      return true;
    }
    if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
      if (!instr->index || !coff_emit_value(text, fun, instr->index, ctx, diag)) return false;
      coff_emit_u8_array_bounds_check(text, local);
      z_x64_append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
      z_x64_append_u8(text, 0x59);
      coff_emit_array_base_rdx(text, fun, instr->array_index);
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0xc1);
      z_x64_append_u8(text, 0xe1);
      z_x64_append_u8(text, 0x02);
      z_x64_append_u8(text, 0x48);
      z_x64_append_u8(text, 0x01);
      z_x64_append_u8(text, 0xca);
      z_x64_append_u8(text, 0x89);
      z_x64_append_u8(text, 0x02);
      return true;
    }
    if (!local->is_array || local->element_type != IR_TYPE_U8) return coff_diag_at(diag, "direct COFF indexed store requires [N]u8 or integer arrays", instr->line, instr->column, "unsupported array local");
    if (!instr->index || !coff_emit_value(text, fun, instr->index, ctx, diag)) return false;
    coff_emit_u8_array_bounds_check(text, local);
    z_x64_append_u8(text, 0x50);
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_append_u8(text, 0x59);
    coff_emit_array_base_rdx(text, fun, instr->array_index);
    z_x64_append_u8(text, 0x48);
    z_x64_append_u8(text, 0x01);
    z_x64_append_u8(text, 0xca);
    z_x64_append_u8(text, 0x88);
    z_x64_append_u8(text, 0x02);
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    return !instr->value || coff_emit_value(text, fun, instr->value, ctx, diag);
  }
  if (instr->kind == IR_INSTR_RETURN) {
    if (instr->value && !coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    coff_emit_epilogue(text);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
      z_x64_patch_rel32(text, false_patch, text->len);
      if (!coff_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
      z_x64_patch_rel32(text, end_patch, text->len);
    } else {
      z_x64_patch_rel32(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    size_t loop_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, loop_patch, loop_start);
    z_x64_patch_rel32(text, false_patch, text->len);
    return true;
  }
  char actual[64];
  snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
  return coff_diag_at(diag, "direct COFF instruction kind is unsupported", instr->line, instr->column, actual);
}

static bool coff_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, CoffEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!coff_emit_instr(text, fun, &instrs[i], ctx, diag)) return false;
  }
  return true;
}

static bool coff_validate_function(const IrFunction *fun, ZDiag *diag) {
  if (fun->param_count > 8) return coff_diag_at(diag, "direct COFF object backend supports at most eight integer parameters", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !coff_type_is_scalar32(fun->return_type)) {
    return coff_diag_at(diag, "direct COFF object backend currently supports only Void and 32-bit-or-smaller integer returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      if (fun->locals[i].is_param) {
        return coff_diag_at(diag, "direct COFF object backend does not yet support byte-view parameters", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
      }
      continue;
    }
    if (fun->locals[i].is_array && (fun->locals[i].element_type == IR_TYPE_U8 || fun->locals[i].element_type == IR_TYPE_U32 || fun->locals[i].element_type == IR_TYPE_I32 || fun->locals[i].element_type == IR_TYPE_USIZE)) continue;
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (fun->locals[i].type == IR_TYPE_VEC) continue;
    if (fun->locals[i].is_array || !coff_type_is_scalar32(fun->locals[i].type)) {
      return coff_diag_at(diag, "direct COFF object backend currently supports only primitive scalar locals", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
    }
  }
  return true;
}

static bool coff_emit_function_text(ZBuf *text, const IrFunction *fun, CoffEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {1, 2, 8, 9};
  unsigned frame_size = (unsigned)z_coff_align(fun ? fun->frame_bytes : 0, 16);
  z_x64_emit_prologue(text, frame_size);
  for (size_t i = 0; i < fun->param_count; i++) {
    if (i < 4) {
      coff_emit_store_local_from_reg(text, fun, (unsigned)i, param_regs[i]);
    } else {
      z_x64_emit_load_rbp_positive_reg(text, 0, 48u + (unsigned)(i - 4u) * 8u, false);
      coff_emit_store_local_from_reg(text, fun, (unsigned)i, 0);
    }
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
  if (has_rodata) coff_append_rodata(&rodata, program, rodata_base_offset);
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
    .rodata_base_offset = rodata_base_offset
  };
  for (size_t i = 0; i < program->function_len; i++) {
    while (text.len % 16 != 0) z_x64_append_u8(&text, 0x90);
    offsets[i] = text.len;
    if (!coff_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      z_coff_emit_context_free(&ctx);
      free(offsets);
      zbuf_free(&relocs);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
  }

  unsigned section_symbol_count = has_rodata ? 2u : 1u;
  bool has_world_write = z_coff_runtime_patch_count(&ctx, COFF_RUNTIME_WORLD_WRITE) > 0;
  size_t symbol_len = program->function_len + (has_world_write ? 1u : 0u);
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
  z_coff_append_call_relocations(&relocs, &ctx, section_symbol_count);
  z_coff_append_rodata_relocations(&relocs, &ctx, 1u);
  uint32_t world_write_symbol_index = section_symbol_count + (uint32_t)program->function_len;
  z_coff_append_runtime_relocations(&relocs, &ctx, COFF_RUNTIME_WORLD_WRITE, world_write_symbol_index);

  for (size_t i = 0; i < program->function_len; i++) {
    symbols[i] = (ZCoffSymbol){
      .name = program->functions[i].name ? program->functions[i].name : "zero_fn",
      .value = (uint32_t)offsets[i],
      .section_number = 1,
      .type = 0x20,
      .storage_class = program->functions[i].is_exported ? Z_COFF_SYMBOL_EXTERNAL : Z_COFF_SYMBOL_STATIC
    };
  }
  if (has_world_write) {
    symbols[program->function_len] = (ZCoffSymbol){
      .name = z_coff_runtime_helper_symbol(COFF_RUNTIME_WORLD_WRITE),
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
  if (fun->return_type != IR_TYPE_VOID && !coff_type_is_scalar32(fun->return_type)) {
    coff_diag_at(diag, "direct COFF x64 executable main must return Void, i32, or u32", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

static void coff_emit_import_call(ZBuf *text, ZCoffImportPatch *patches, size_t *patch_len, unsigned import_index) {
  z_x64_append_u8(text, 0xff);
  z_x64_append_u8(text, 0x15);
  size_t patch = text->len;
  z_x64_append_u32(text, 0);
  if (patches && patch_len && *patch_len < 8) {
    patches[*patch_len] = (ZCoffImportPatch){.patch_offset = patch, .import_index = import_index};
    *patch_len += 1;
  }
}

static size_t coff_emit_exe_start_stub(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  z_x64_emit_sub_rsp(text, 40);
  size_t main_patch = z_x64_emit_call32_placeholder(text);
  z_x64_append_u8(text, 0x89);
  z_x64_append_u8(text, 0xc1); // mov ecx, eax
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_EXIT_PROCESS);
  z_x64_append_u8(text, 0xcc);
  return main_patch;
}

static size_t coff_emit_exe_world_write(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  size_t offset = text->len;
  z_x64_emit_sub_rsp(text, 72);
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0x89);
  z_x64_append_u8(text, 0x54);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x28); // mov [rsp+40], rdx
  z_x64_append_u8(text, 0x4c);
  z_x64_append_u8(text, 0x89);
  z_x64_append_u8(text, 0x44);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x30); // mov [rsp+48], r8
  z_x64_append_u8(text, 0xc7);
  z_x64_append_u8(text, 0x44);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x38);
  z_x64_append_u32(text, 0); // DWORD bytes_written = 0
  z_x64_append_u8(text, 0x83);
  z_x64_append_u8(text, 0xf9);
  z_x64_append_u8(text, 0x02); // cmp ecx, 2
  z_x64_append_u8(text, 0xb9);
  z_x64_append_u32(text, 0xfffffff5u); // STD_OUTPUT_HANDLE
  z_x64_append_u8(text, 0x75);
  z_x64_append_u8(text, 0x05); // jne after stderr handle
  z_x64_append_u8(text, 0xb9);
  z_x64_append_u32(text, 0xfffffff4u); // STD_ERROR_HANDLE
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_GET_STD_HANDLE);
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0x89);
  z_x64_append_u8(text, 0xc1); // mov rcx, rax
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0x8b);
  z_x64_append_u8(text, 0x54);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x28); // mov rdx, [rsp+40]
  z_x64_append_u8(text, 0x4c);
  z_x64_append_u8(text, 0x8b);
  z_x64_append_u8(text, 0x44);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x30); // mov r8, [rsp+48]
  z_x64_append_u8(text, 0x4c);
  z_x64_append_u8(text, 0x8d);
  z_x64_append_u8(text, 0x4c);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x38); // lea r9, [rsp+56]
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0xc7);
  z_x64_append_u8(text, 0x44);
  z_x64_append_u8(text, 0x24);
  z_x64_append_u8(text, 0x20);
  z_x64_append_u32(text, 0); // lpOverlapped = NULL
  coff_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_WRITE_FILE);
  z_x64_append_u8(text, 0x31);
  z_x64_append_u8(text, 0xc0); // xor eax, eax
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
  unsigned main_index = 0;
  if (!coff_find_executable_main(program, diag, &main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (!coff_validate_function(&program->functions[i], diag)) return false;
  }

  ZBuf text;
  ZBuf rdata;
  zbuf_init(&text);
  zbuf_init(&rdata);
  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = coff_rodata_base_offset(program);
  if (has_rodata) coff_append_rodata(&rdata, program, rodata_base_offset);

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
    .rodata_base_offset = rodata_base_offset
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
