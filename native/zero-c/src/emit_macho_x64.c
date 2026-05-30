#include "zero.h"
#include "macho_emit_state.h"
#include "macho_format.h"
#include "x64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool machx64_diag(ZDiag *diag, const char *message) {
  if (diag) {
    diag->code = 4004;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct x86_64 Mach-O subset");
    snprintf(diag->actual, sizeof(diag->actual), "unsupported feature");
    snprintf(diag->help, sizeof(diag->help), "reduce the program to primitive direct-backend constructs or choose a supported direct target");
  }
  return false;
}

static bool machx64_diag_at(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct x86_64 Mach-O subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported feature");
    snprintf(diag->help, sizeof(diag->help), "reduce the program to primitive direct-backend constructs or choose a supported direct target");
  }
  return false;
}

static bool machx64_type_is_scalar32(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE;
}

static bool machx64_type_is_i64(IrTypeKind type) {
  return type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool machx64_type_is_supported_scalar(IrTypeKind type) {
  return machx64_type_is_scalar32(type) || machx64_type_is_i64(type);
}

static bool machx64_type_is_unsigned(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_U32 || type == IR_TYPE_USIZE || type == IR_TYPE_U64;
}

static bool machx64_is_main_function(const IrFunction *fun) {
  return fun && fun->is_exported && fun->name && fun->name[0] == 'm' && fun->name[1] == 'a' && fun->name[2] == 'i' && fun->name[3] == 'n' && fun->name[4] == '\0';
}

static bool machx64_function_propagates_to_process_exit(const IrFunction *fun) {
  return fun && (fun->raises || (machx64_is_main_function(fun) && fun->return_type == IR_TYPE_I32 && fun->value_return_type == IR_TYPE_VOID));
}

static size_t machx64_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static void machx64_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) z_x64_append_u8(buf, 0x90);
}

static void machx64_append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

static void machx64_append_bytes(ZBuf *buf, const char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) machx64_append_u8(buf, (unsigned char)bytes[i]);
}

static unsigned machx64_local_offset(const IrFunction *fun, unsigned local_index) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0) return fun->locals[local_index].frame_offset;
  return (local_index + 1) * 8;
}

static unsigned machx64_local_slot_offset(const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  unsigned offset = machx64_local_offset(fun, local_index);
  return offset >= slot_offset ? offset - slot_offset : offset;
}

static void machx64_emit_load_local_eax(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  bool wide = fun && local_index < fun->local_len && machx64_type_is_i64(fun->locals[local_index].type);
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, machx64_local_offset(fun, local_index), wide);
}

static void machx64_emit_load_local_slot_rax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, machx64_local_slot_offset(fun, local_index, slot_offset), true);
}

static void machx64_emit_load_local_slot_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  z_x64_emit_rbp_disp_reg(text, 0x8b, 0, machx64_local_slot_offset(fun, local_index, slot_offset), false);
}

static void machx64_emit_store_local_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg) {
  bool wide = fun && local_index < fun->local_len && machx64_type_is_i64(fun->locals[local_index].type);
  z_x64_emit_rbp_disp_reg(text, 0x89, reg, machx64_local_offset(fun, local_index), wide);
}

static void machx64_emit_store_local_slot_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg, unsigned slot_offset, bool wide) {
  z_x64_emit_rbp_disp_reg(text, 0x89, reg, machx64_local_slot_offset(fun, local_index, slot_offset), wide);
}

static void machx64_emit_load_field_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = machx64_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_append_u8(text, 0x0f);
    z_x64_emit_rbp_disp_reg(text, 0xb6, 0, offset, false);
  } else if (machx64_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(text, 0x8b, 0, offset, true);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x8b, 0, offset, false);
  }
}

static void machx64_emit_store_field_from_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = machx64_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_emit_rbp_disp_reg(text, 0x88, 0, offset, false);
  } else if (machx64_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, true);
  } else {
    z_x64_emit_rbp_disp_reg(text, 0x89, 0, offset, false);
  }
}

static void machx64_emit_array_base_rdx(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  z_x64_emit_rbp_disp_reg(text, 0x8d, 2, machx64_local_offset(fun, local_index), true);
}

static void machx64_emit_bounds_check(ZBuf *text, const IrLocal *local) {
  z_x64_append_u8(text, 0x3d);
  z_x64_append_u32(text, local ? local->array_len : 0);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
}

static unsigned machx64_setcc_opcode(IrCompareOp op, bool uns) {
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

static bool machx64_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT || value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static bool machx64_readonly_data_byte(const IrProgram *program, unsigned offset, unsigned char *out) {
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

static void machx64_emit_error_condition_from_rax(ZBuf *text) {
  z_x64_emit_mov_rcx_from_rax(text, true);
  z_x64_emit_shr_rcx_imm8(text, 32);
  z_x64_emit_test_ecx_ecx(text);
}

static void machx64_emit_packed_error_rax(ZBuf *text, unsigned code_value) {
  z_x64_emit_mov_rax_u64(text, ((uint64_t)code_value) << 32);
}

static bool machx64_byte_view_const_len(const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!machx64_byte_view_const_len(view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !machx64_const_u32_value(view->index, &start)) return false;
    if (view->right && !machx64_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  return false;
}

static bool machx64_byte_view_const_byte(const IrProgram *program, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    return machx64_readonly_data_byte(program, view->data_offset + index, out);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned len = 0;
    unsigned start = 0;
    if (!machx64_byte_view_const_len(view, &len) || index >= len) return false;
    if (view->index && !machx64_const_u32_value(view->index, &start)) return false;
    return machx64_byte_view_const_byte(program, view->left, start + index, out);
  }
  return false;
}

static size_t machx64_emit_lea_rax_rip_placeholder(ZBuf *text) {
  z_x64_append_u8(text, 0x48);
  z_x64_append_u8(text, 0x8d);
  z_x64_append_u8(text, 0x05);
  size_t patch = text->len;
  z_x64_append_u32(text, 0);
  return patch;
}

static bool machx64_emit_rodata_ptr_rax(ZBuf *text, unsigned data_offset, MachOEmitContext *ctx, const IrValue *value, ZDiag *diag) {
  size_t patch = machx64_emit_lea_rax_rip_placeholder(text);
  return z_macho_record_data_patch(ctx, patch, data_offset, value, diag);
}

static bool machx64_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, MachOEmitContext *ctx, ZDiag *diag);
static bool machx64_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, MachOEmitContext *ctx, ZDiag *diag);
static bool machx64_emit_byte_view_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, MachOEmitContext *ctx, ZDiag *diag);
static bool machx64_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag);

static bool machx64_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, MachOEmitContext *ctx, ZDiag *diag) {
  unsigned len = 0;
  if (machx64_byte_view_const_len(view, &len)) {
    z_x64_emit_mov_eax_u32(text, len);
    return true;
  }
  if (view && view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    machx64_emit_load_local_slot_eax(text, fun, view->local_index, 8);
    return true;
  }
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    machx64_emit_load_local_slot_eax(text, fun, view->local_index, 16);
    return true;
  }
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!machx64_emit_value(text, fun, view, ctx, diag)) return false;
    z_x64_emit_mov_reg_from_reg(text, 0, 2, true);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && !view->right) {
    if (!machx64_emit_byte_view_len(text, fun, view->left, ctx, diag)) return false;
    if (!view->index) return true;
    unsigned start = 0;
    if (machx64_const_u32_value(view->index, &start)) {
      if (start > 0) z_x64_emit_sub_rax_u32(text, start, true);
      return true;
    }
    z_x64_emit_push_rax(text);
    if (!machx64_emit_value(text, fun, view->index, ctx, diag)) return false;
    z_x64_emit_mov_rcx_from_rax(text, true);
    z_x64_emit_pop_rax(text);
    z_x64_emit_sub_rax_rcx(text, true);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->right) {
    unsigned start = 0;
    if (!view->index || machx64_const_u32_value(view->index, &start)) {
      unsigned end = 0;
      if (machx64_const_u32_value(view->right, &end) && start <= end) {
        z_x64_emit_mov_eax_u32(text, end - start);
        return true;
      }
      if (!machx64_emit_value(text, fun, view->right, ctx, diag)) return false;
      if (start > 0) z_x64_emit_sub_rax_u32(text, start, true);
      return true;
    }
    if (!machx64_emit_value(text, fun, view->index, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (!machx64_emit_value(text, fun, view->right, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_sub_reg_reg(text, 0, 1, true);
    return true;
  }
  (void)ctx;
  return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view ? view->line : 1, view ? view->column : 1, "unsupported byte view length");
}

static bool machx64_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return machx64_diag_at(diag, "direct x86_64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    machx64_emit_load_local_slot_rax(text, fun, view->local_index, 0);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    machx64_emit_load_local_slot_rax(text, fun, view->local_index, 8);
    return true;
  }
  if (view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) return machx64_emit_value(text, fun, view, ctx, diag);
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view array requires [N]u8", view->line, view->column, "unsupported array view");
    z_x64_emit_rbp_disp_reg(text, 0x8d, 0, machx64_local_offset(fun, view->array_index), true);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) return machx64_emit_rodata_ptr_rax(text, view->data_offset, ctx, view, diag);
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    if (!machx64_emit_byte_view_ptr(text, fun, view->left, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (view->index) {
      if (!machx64_emit_value(text, fun, view->index, ctx, diag)) return false;
    } else {
      z_x64_emit_mov_eax_u32(text, 0);
    }
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_add_rax_rcx(text, true);
    return true;
  }
  return machx64_diag_at(diag, "direct x86_64 Mach-O value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static void machx64_emit_move_byte_view_pair(ZBuf *text, unsigned ptr_reg, unsigned len_reg, unsigned src_ptr_reg, unsigned src_len_reg) {
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

static bool machx64_emit_byte_view_pair(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, MachOEmitContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && view->kind == IR_VALUE_CALL && view->type == IR_TYPE_BYTE_VIEW) {
    if (!machx64_emit_value(text, fun, view, ctx, diag)) return false;
    machx64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 2);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE) {
    if (!view->index && !view->right) return machx64_emit_byte_view_pair(text, fun, view->left, ptr_reg, len_reg, ctx, diag);
    if (!machx64_emit_byte_view_pair(text, fun, view->left, 8, 10, ctx, diag)) return false;
    z_x64_emit_push_reg64(text, 8);
    z_x64_emit_push_reg64(text, 10);
    if (view->index) {
      if (!machx64_emit_value(text, fun, view->index, ctx, diag)) return false;
    } else {
      z_x64_emit_mov_eax_u32(text, 0);
    }
    z_x64_emit_mov_rcx_from_rax(text, true);
    if (view->right) {
      z_x64_emit_push_reg64(text, 1);
      if (!machx64_emit_value(text, fun, view->right, ctx, diag)) return false;
      z_x64_emit_pop_reg64(text, 1);
      z_x64_emit_sub_reg_reg(text, 0, 1, true);
      z_x64_emit_pop_reg64(text, 10);
    } else {
      z_x64_emit_pop_rax(text);
      z_x64_emit_sub_reg_reg(text, 0, 1, true);
    }
    z_x64_emit_pop_reg64(text, 8);
    z_x64_emit_add_reg_reg(text, 8, 1, true);
    machx64_emit_move_byte_view_pair(text, ptr_reg, len_reg, 8, 0);
    return true;
  }
  if (!machx64_emit_byte_view_ptr(text, fun, view, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!machx64_emit_byte_view_len(text, fun, view, ctx, diag)) return false;
  if (len_reg != 0) z_x64_emit_mov_reg_from_reg(text, len_reg, 0, true);
  z_x64_emit_pop_reg64(text, ptr_reg);
  return true;
}

static void machx64_emit_cast_normalize_rax(ZBuf *text, IrTypeKind source, IrTypeKind target) {
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
    case IR_TYPE_USIZE:
      z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
      return;
    case IR_TYPE_I64:
      if (source == IR_TYPE_I32) z_x64_emit_cdqe(text);
      return;
    case IR_TYPE_U64:
      return;
    default:
      return;
  }
}

static bool machx64_emit_local_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O local index is out of range", value->line, value->column, "invalid local");
  if (fun->locals[value->local_index].type == IR_TYPE_BYTE_VIEW) return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view local cannot be used as a scalar", value->line, value->column, "byte-view local");
  machx64_emit_load_local_eax(text, fun, value->local_index);
  return true;
}

static bool machx64_emit_binary_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->binary_op == IR_BIN_AND) {
    if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t left_false = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!machx64_emit_value(text, fun, value->right, ctx, diag)) return false;
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
    if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t eval_right = z_x64_emit_jcc32_placeholder(text, 0x84);
    z_x64_emit_mov_eax_u32(text, 1);
    size_t left_true_end = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, eval_right, text->len);
    if (!machx64_emit_value(text, fun, value->right, ctx, diag)) return false;
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
    return machx64_diag_at(diag, "direct x86_64 Mach-O binary operator is unsupported", value->line, value->column, "unsupported operator");
  }
  if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!machx64_emit_value(text, fun, value->right, ctx, diag)) return false;
  bool wide = machx64_type_is_i64(value->type);
  z_x64_emit_mov_rcx_from_rax(text, wide);
  z_x64_emit_pop_rax(text);
  if (value->binary_op == IR_BIN_ADD) z_x64_emit_add_rax_rcx(text, wide);
  else if (value->binary_op == IR_BIN_SUB) z_x64_emit_sub_rax_rcx(text, wide);
  else if (value->binary_op == IR_BIN_MUL) z_x64_emit_imul_rax_rcx(text, wide);
  else z_x64_emit_div_rax_rcx(text, wide, machx64_type_is_unsigned(value->type), value->binary_op == IR_BIN_MOD);
  return true;
}

static bool machx64_emit_compare_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return machx64_diag_at(diag, "direct x86_64 Mach-O comparison requires two operands", value->line, value->column, "invalid comparison");
  if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!machx64_emit_value(text, fun, value->right, ctx, diag)) return false;
  bool wide = machx64_type_is_i64(value->left ? value->left->type : value->type);
  z_x64_emit_mov_rcx_from_rax(text, wide);
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx_to_bool(text, machx64_setcc_opcode(value->compare_op, machx64_type_is_unsigned(value->left ? value->left->type : value->type)), wide);
  return true;
}

static bool machx64_emit_check_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || value->left->type != IR_TYPE_I64) {
    return machx64_diag_at(diag, "direct x86_64 Mach-O check requires a packed fallible call result", value->line, value->column, "non-fallible value");
  }
  if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
  machx64_emit_error_condition_from_rax(text);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!machx64_function_propagates_to_process_exit(fun)) z_x64_emit_mov_eax_u32(text, 1);
  z_x64_emit_epilogue(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
  if (!machx64_type_is_i64(value->type)) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
  return true;
}

static bool machx64_emit_call_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {7, 6, 2, 1, 8, 9};
  size_t abi_slots = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    const IrValue *arg = value->args[i];
    bool byte_view = arg && arg->type == IR_TYPE_BYTE_VIEW;
    abi_slots += byte_view ? 2u : 1u;
    if (abi_slots > 6) return machx64_diag_at(diag, "direct x86_64 Mach-O call supports at most six ABI argument slots", value->line, value->column, "too many arguments");
    if (byte_view) {
      if (!machx64_emit_byte_view_pair(text, fun, arg, 0, 2, ctx, diag)) return false;
      z_x64_emit_push_rax(text);
      z_x64_emit_push_reg64(text, 2);
      continue;
    }
    if (!machx64_emit_value(text, fun, arg, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
  }
  for (size_t i = abi_slots; i > 0; i--) z_x64_emit_pop_reg64(text, param_regs[i - 1]);
  size_t patch = z_x64_emit_call32_placeholder(text);
  return z_macho_record_call_patch(ctx, patch, value->callee_index, value, diag);
}

static bool machx64_emit_byte_view_index_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  unsigned const_index = 0;
  unsigned char byte = 0;
  if (machx64_const_u32_value(value->index, &const_index) && machx64_byte_view_const_byte(ctx ? ctx->program : NULL, value->left, const_index, &byte)) {
    z_x64_emit_mov_eax_u32(text, byte);
    return true;
  }
  if (!value->index || !machx64_emit_value(text, fun, value->index, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!machx64_emit_byte_view_pair(text, fun, value->left, 8, 1, ctx, diag)) return false;
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_mov_reg_from_reg(text, 0, 8, true);
  z_x64_emit_add_rax_rcx(text, true);
  z_x64_emit_movzx_reg32_ptr_reg_u8(text, 0, 0);
  return true;
}

static bool machx64_emit_byte_copy_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return machx64_diag_at(diag, "direct x86_64 Mach-O byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
  if (!machx64_emit_byte_view_pair(text, fun, value->left, 0, 2, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  z_x64_emit_push_reg64(text, 2);
  if (!machx64_emit_byte_view_pair(text, fun, value->right, 7, 0, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  z_x64_emit_pop_reg64(text, 6);
  z_x64_emit_byte_copy_min_loop(text);
  return true;
}

static bool machx64_emit_byte_fill_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return machx64_diag_at(diag, "direct x86_64 Mach-O byte fill requires a fill byte and destination byte view", value->line, value->column, "missing byte fill input");
  if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!machx64_emit_byte_view_pair(text, fun, value->right, 7, 2, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 9);
  z_x64_emit_byte_fill_loop(text);
  return true;
}

static bool machx64_emit_byte_view_eq_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view equality requires two byte views", value->line, value->column, "missing byte view");
  if (!machx64_emit_byte_view_pair(text, fun, value->left, 8, 10, ctx, diag)) return false;
  z_x64_emit_push_reg64(text, 8);
  z_x64_emit_push_reg64(text, 10);
  if (!machx64_emit_byte_view_pair(text, fun, value->right, 9, 0, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 10);
  z_x64_emit_cmp_reg_reg(text, 10, 0, false);
  size_t same_len = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_pop_reg64(text, 8);
  z_x64_emit_mov_eax_u32(text, 0);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, same_len, text->len);
  z_x64_emit_pop_reg64(text, 8);
  z_x64_emit_byte_eq_loop(text);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool machx64_emit_index_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->array_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O indexed load array is out of range", value->line, value->column, "invalid array local");
  const IrLocal *local = &fun->locals[value->array_index];
  bool byte_array = local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_BOOL;
  unsigned const_index = 0;
  if (local->is_array && !byte_array && machx64_const_u32_value(value->index, &const_index) && const_index < local->array_len) {
    machx64_emit_load_local_slot_eax(text, fun, value->array_index, const_index * 4u);
    return true;
  }
  if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
    if (!value->index || !machx64_emit_value(text, fun, value->index, ctx, diag)) return false;
    machx64_emit_bounds_check(text, local);
    z_x64_emit_push_rax(text);
    machx64_emit_array_base_rdx(text, fun, value->array_index);
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_shl_rcx_imm8(text, 2);
    z_x64_emit_add_rdx_rcx(text, true);
    z_x64_emit_load_reg_ptr_reg(text, 0, 2, false);
    return true;
  }
  if (!local->is_array || !byte_array) return machx64_diag_at(diag, "direct x86_64 Mach-O indexed load requires [N]u8, [N]Bool, or integer arrays", value->line, value->column, "unsupported array local");
  if (!value->index || !machx64_emit_value(text, fun, value->index, ctx, diag)) return false;
  machx64_emit_bounds_check(text, local);
  z_x64_emit_push_rax(text);
  machx64_emit_array_base_rdx(text, fun, value->array_index);
  z_x64_emit_pop_reg64(text, 1);
  z_x64_emit_add_rdx_rcx(text, true);
  z_x64_emit_movzx_reg32_ptr_reg_u8(text, 0, 2);
  return true;
}

static bool machx64_emit_field_load_value(ZBuf *text, const IrFunction *fun, const IrValue *value, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O field load record is out of range", value->line, value->column, "invalid record local");
  if (!fun->locals[value->local_index].is_record) return machx64_diag_at(diag, "direct x86_64 Mach-O field load requires record local", value->line, value->column, "non-record local");
  machx64_emit_load_field_eax(text, fun, value->local_index, value->field_offset, value->type);
  return true;
}

static bool machx64_emit_value(ZBuf *text, const IrFunction *fun, const IrValue *value, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value) return machx64_diag_at(diag, "direct x86_64 Mach-O expression is missing", 1, 1, "missing expression");
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      if (machx64_type_is_i64(value->type)) z_x64_emit_mov_rax_u64(text, (uint64_t)value->int_value);
      else z_x64_emit_mov_eax_u32(text, (uint32_t)value->int_value);
      return true;
    case IR_VALUE_LOCAL: return machx64_emit_local_value(text, fun, value, diag);
    case IR_VALUE_CAST:
      if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
      machx64_emit_cast_normalize_rax(text, value->left ? value->left->type : IR_TYPE_UNSUPPORTED, value->type);
      return true;
    case IR_VALUE_BINARY: return machx64_emit_binary_value(text, fun, value, ctx, diag);
    case IR_VALUE_COMPARE: return machx64_emit_compare_value(text, fun, value, ctx, diag);
    case IR_VALUE_CHECK: return machx64_emit_check_value(text, fun, value, ctx, diag);
    case IR_VALUE_CALL: return machx64_emit_call_value(text, fun, value, ctx, diag);
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW) return machx64_diag_at(diag, "direct x86_64 Mach-O maybe helper requires a Maybe byte-view local", value->line, value->column, "invalid maybe local");
      machx64_emit_load_local_slot_eax(text, fun, value->local_index, 0);
      return true;
    case IR_VALUE_BYTE_VIEW_LEN: return machx64_emit_byte_view_len(text, fun, value->left, ctx, diag);
    case IR_VALUE_BYTE_COPY: return machx64_emit_byte_copy_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_FILL: return machx64_emit_byte_fill_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ: return machx64_emit_byte_view_eq_value(text, fun, value, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: return machx64_emit_byte_view_index_load_value(text, fun, value, ctx, diag);
    case IR_VALUE_INDEX_LOAD: return machx64_emit_index_load_value(text, fun, value, ctx, diag);
    case IR_VALUE_FIELD_LOAD: return machx64_emit_field_load_value(text, fun, value, diag);
    case IR_VALUE_PTR_FROM_INT:
      if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
      return true;
    case IR_VALUE_PTR_LOAD: {
      if (!machx64_emit_value(text, fun, value->left, ctx, diag)) return false;
      IrTypeKind elem = value->element_type;
      if (elem == IR_TYPE_U8) {
        z_x64_emit_movzx_reg32_ptr_reg_u8(text, 0, 0);
      } else if (elem == IR_TYPE_U16) {
        z_x64_emit_movzx_reg32_ptr_reg_disp_u16(text, 0, 0, 0);
      } else {
        z_x64_emit_load_reg_ptr_reg(text, 0, 0, elem == IR_TYPE_U64 || elem == IR_TYPE_I64);
      }
      return true;
    }
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported value kind %d", (int)value->kind);
      return machx64_diag_at(diag, "direct x86_64 Mach-O value kind is unsupported", value->line, value->column, actual);
    }
  }
}

static bool machx64_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, MachOEmitContext *ctx, ZDiag *diag);

static bool machx64_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return machx64_diag_at(diag, "direct x86_64 Mach-O World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!machx64_emit_byte_view_pair(text, fun, instr->value, 6, 2, ctx, diag)) return false;
  z_x64_emit_mov_reg_u32(text, 7, instr->field_offset == 2 ? 2u : 1u);
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_test_rax_rax(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
  return z_macho_record_instr_runtime_patch(ctx, MACHO_RUNTIME_WORLD_WRITE, patch, instr, diag);
}

static bool machx64_emit_local_set_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (!machx64_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
  machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
  machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, false);
  return true;
}

static bool machx64_emit_local_set_maybe_byte_view(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value) return machx64_diag_at(diag, "direct x86_64 Mach-O Maybe byte-view initializer is missing", instr->line, instr->column, "missing maybe value");
  if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
    z_x64_emit_mov_eax_u32(text, instr->value->data_len ? 1u : 0u);
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    if (!instr->value->data_len) {
      z_x64_emit_xor_eax_eax(text);
      machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
      machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
      return true;
    }
    if (!machx64_emit_byte_view_pair(text, fun, instr->value->left, 0, 2, ctx, diag)) return false;
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 16, false);
    return true;
  }
  if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
    if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
    machx64_emit_store_local_slot_from_reg(text, fun, instr->local_index, 1, 16, false);
    return true;
  }
  return machx64_diag_at(diag, "direct x86_64 Mach-O Maybe byte-view initializer is unsupported", instr->line, instr->column, "unsupported Maybe byte-view initializer");
}

static bool machx64_emit_local_set_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O local store is out of range", instr->line, instr->column, "invalid local");
  if (fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) return machx64_emit_local_set_byte_view(text, fun, instr, ctx, diag);
  if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) return machx64_emit_local_set_maybe_byte_view(text, fun, instr, ctx, diag);
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  machx64_emit_store_local_from_reg(text, fun, instr->local_index, 0);
  return true;
}

static bool machx64_emit_field_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O field store record is out of range", instr->line, instr->column, "invalid record local");
  if (!fun->locals[instr->local_index].is_record) return machx64_diag_at(diag, "direct x86_64 Mach-O field store requires record local", instr->line, instr->column, "non-record local");
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  machx64_emit_store_field_from_eax(text, fun, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32);
  return true;
}

static bool machx64_emit_byte_view_index_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (!instr->value || instr->value->type != IR_TYPE_U8) return machx64_diag_at(diag, "direct x86_64 Mach-O byte-view indexed store requires u8 value", instr->line, instr->column, "unsupported byte-view store value");
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!instr->index || !machx64_emit_value(text, fun, instr->index, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  machx64_emit_load_local_slot_eax(text, fun, instr->array_index, 8);
  z_x64_emit_mov_rcx_from_rax(text, false);
  z_x64_emit_pop_rax(text);
  z_x64_emit_cmp_rax_rcx(text, false);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x82);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
  z_x64_emit_push_rax(text);
  machx64_emit_load_local_slot_rax(text, fun, instr->array_index, 0);
  z_x64_emit_pop_reg64(text, 1);
  z_x64_emit_add_rax_rcx(text, true);
  z_x64_emit_mov_reg_from_reg(text, 2, 0, true);
  z_x64_emit_pop_reg64(text, 0);
  z_x64_emit_store_ptr_reg8_from_reg(text, 2, 0);
  return true;
}

static bool machx64_emit_index_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->array_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O indexed store array is out of range", instr->line, instr->column, "invalid array local");
  const IrLocal *local = &fun->locals[instr->array_index];
  if (local->type == IR_TYPE_BYTE_VIEW) return machx64_emit_byte_view_index_store_instr(text, fun, instr, ctx, diag);
  bool byte_array = local->element_type == IR_TYPE_U8 || local->element_type == IR_TYPE_BOOL;
  unsigned const_index = 0;
  if (local->is_array && !byte_array && machx64_const_u32_value(instr->index, &const_index) && const_index < local->array_len) {
    if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
    machx64_emit_store_local_slot_from_reg(text, fun, instr->array_index, 0, const_index * 4u, false);
    return true;
  }
  if (local->is_array && (local->element_type == IR_TYPE_U32 || local->element_type == IR_TYPE_I32 || local->element_type == IR_TYPE_USIZE)) {
    if (!instr->index || !machx64_emit_value(text, fun, instr->index, ctx, diag)) return false;
    machx64_emit_bounds_check(text, local);
    z_x64_emit_push_rax(text);
    if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    machx64_emit_array_base_rdx(text, fun, instr->array_index);
    z_x64_emit_shl_rcx_imm8(text, 2);
    z_x64_emit_add_rdx_rcx(text, true);
    z_x64_emit_store_ptr_reg_from_reg(text, 2, 0, false);
    return true;
  }
  if (!local->is_array || !byte_array) return machx64_diag_at(diag, "direct x86_64 Mach-O indexed store requires [N]u8, [N]Bool, or integer arrays", instr->line, instr->column, "unsupported array local");
  if (!instr->index || !machx64_emit_value(text, fun, instr->index, ctx, diag)) return false;
  machx64_emit_bounds_check(text, local);
  z_x64_emit_push_rax(text);
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_pop_reg64(text, 1);
  machx64_emit_array_base_rdx(text, fun, instr->array_index);
  z_x64_emit_add_rdx_rcx(text, true);
  z_x64_emit_store_ptr_reg8_from_reg(text, 2, 0);
  return true;
}

static bool machx64_emit_if_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_test_rax_rax(text, false);
  size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!machx64_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
  if (instr->else_len > 0) {
    size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, false_patch, text->len);
    if (!machx64_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
    z_x64_patch_rel32(text, end_patch, text->len);
  } else {
    z_x64_patch_rel32(text, false_patch, text->len);
  }
  return true;
}

static bool machx64_emit_while_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  size_t loop_start = text->len;
  if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_test_rax_rax(text, false);
  size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  if (!machx64_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
  size_t loop_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, loop_patch, loop_start);
  z_x64_patch_rel32(text, false_patch, text->len);
  return true;
}

static bool machx64_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, MachOEmitContext *ctx, ZDiag *diag) {
  switch (instr->kind) {
    case IR_INSTR_PTR_STORE: {
      if (instr->local_index >= fun->local_len) return machx64_diag_at(diag, "direct x86_64 Mach-O pointer store local is out of range", instr->line, instr->column, "invalid local");
      const IrLocal *ptr_local = &fun->locals[instr->local_index];
      if (ptr_local->type != IR_TYPE_PTR) return machx64_diag_at(diag, "direct x86_64 Mach-O pointer store requires a Ptr local", instr->line, instr->column, "non-pointer local");
      IrTypeKind elem = ptr_local->element_type;
      machx64_emit_load_local_eax(text, fun, instr->local_index);
      z_x64_emit_push_rax(text);
      if (!instr->value || !machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
      z_x64_emit_pop_reg64(text, 1);
      if (elem == IR_TYPE_U8) z_x64_emit_store_ptr_reg8_from_reg(text, 1, 0);
      else z_x64_emit_store_ptr_reg_from_reg(text, 1, 0, elem == IR_TYPE_U64 || elem == IR_TYPE_I64);
      return true;
    }
    case IR_INSTR_WORLD_WRITE: return machx64_emit_world_write(text, fun, instr, ctx, diag);
    case IR_INSTR_LOCAL_SET: return machx64_emit_local_set_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_FIELD_STORE: return machx64_emit_field_store_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_INDEX_STORE: return machx64_emit_index_store_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_EXPR: return !instr->value || machx64_emit_value(text, fun, instr->value, ctx, diag);
    case IR_INSTR_RETURN:
      if (fun->return_type == IR_TYPE_BYTE_VIEW && instr->value) {
        if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_BYTE_VIEW) {
          if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
        } else {
          if (!machx64_emit_byte_view_pair(text, fun, instr->value, 0, 2, ctx, diag)) return false;
        }
        z_x64_emit_epilogue(text);
        return true;
      }
      if (fun->return_type == IR_TYPE_MAYBE_BYTE_VIEW && instr->value) {
        if (instr->value->kind == IR_VALUE_CALL && instr->value->type == IR_TYPE_MAYBE_BYTE_VIEW) {
          if (!machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
        } else if (instr->value->kind == IR_VALUE_MAYBE_BYTE_VIEW_LITERAL) {
          if (!instr->value->data_len) {
            z_x64_emit_xor_eax_eax(text);
            z_x64_emit_xor_reg_reg(text, 2, true);
            z_x64_emit_xor_ecx_ecx(text);
          } else {
            if (!machx64_emit_byte_view_pair(text, fun, instr->value->left, 2, 1, ctx, diag)) return false;
            z_x64_emit_mov_eax_u32(text, 1);
          }
        } else {
          return machx64_diag_at(diag, "direct x86_64 Mach-O Maybe byte-view return requires a Maybe byte-view value", instr->line, instr->column, "unsupported Maybe byte-view return");
        }
        z_x64_emit_epilogue(text);
        return true;
      }
      if (instr->value && !machx64_emit_value(text, fun, instr->value, ctx, diag)) return false;
      if (fun->raises && !instr->value) z_x64_emit_xor_rax_rax(text);
      else if (fun->raises && instr->value && !machx64_type_is_i64(instr->value->type)) z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
      z_x64_emit_epilogue(text);
      return true;
    case IR_INSTR_RAISE:
      if (!machx64_function_propagates_to_process_exit(fun)) return machx64_diag_at(diag, "direct x86_64 Mach-O raise requires a fallible function context", instr->line, instr->column, "non-fallible context");
      machx64_emit_packed_error_rax(text, instr->error_code ? instr->error_code : IR_ERROR_UNKNOWN);
      z_x64_emit_epilogue(text);
      return true;
    case IR_INSTR_IF: return machx64_emit_if_instr(text, fun, instr, ctx, diag);
    case IR_INSTR_WHILE: return machx64_emit_while_instr(text, fun, instr, ctx, diag);
    default: {
      char actual[64];
      snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
      return machx64_diag_at(diag, "direct x86_64 Mach-O instruction kind is unsupported", instr ? instr->line : 1, instr ? instr->column : 1, actual);
    }
  }
}

static bool machx64_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, MachOEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!machx64_emit_instr(text, fun, &instrs[i], ctx, diag)) return false;
  }
  return true;
}

static bool machx64_validate_function(const IrFunction *fun, ZDiag *diag) {
  size_t abi_slots = 0;
  for (size_t i = 0; i < fun->param_count; i++) abi_slots += fun->locals[i].type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
  if (abi_slots > 6) return machx64_diag_at(diag, "direct x86_64 Mach-O object backend supports at most six ABI parameter slots", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !machx64_type_is_supported_scalar(fun->return_type) &&
      fun->return_type != IR_TYPE_BYTE_VIEW && fun->return_type != IR_TYPE_MAYBE_BYTE_VIEW) {
    return machx64_diag_at(diag, "direct x86_64 Mach-O object backend currently supports Void, integer, byte-view, and Maybe byte-view returns", fun->line, fun->column, fun->name);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW || fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW) continue;
    if (fun->locals[i].type == IR_TYPE_PTR) continue;
    if (fun->locals[i].is_array && (fun->locals[i].element_type == IR_TYPE_BOOL || fun->locals[i].element_type == IR_TYPE_U8 || fun->locals[i].element_type == IR_TYPE_U32 || fun->locals[i].element_type == IR_TYPE_I32 || fun->locals[i].element_type == IR_TYPE_USIZE)) continue;
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].is_array || !machx64_type_is_supported_scalar(fun->locals[i].type)) {
      return machx64_diag_at(diag, "direct x86_64 Mach-O object backend currently supports only primitive scalar locals", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
    }
  }
  return true;
}

static bool machx64_emit_function_text(ZBuf *text, const IrFunction *fun, MachOEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {7, 6, 2, 1, 8, 9};
  unsigned frame_size = (unsigned)machx64_align(fun ? fun->frame_bytes : 0, 16);
  z_x64_emit_prologue(text, frame_size);
  size_t abi_slot = 0;
  for (size_t i = 0; i < fun->param_count; i++) {
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      machx64_emit_store_local_slot_from_reg(text, fun, (unsigned)i, param_regs[abi_slot++], 0, true);
      machx64_emit_store_local_slot_from_reg(text, fun, (unsigned)i, param_regs[abi_slot++], 8, false);
      continue;
    }
    machx64_emit_store_local_from_reg(text, fun, (unsigned)i, param_regs[abi_slot++]);
  }
  if (!machx64_emit_instrs(text, fun, fun->instrs, fun->instr_len, ctx, diag)) return false;
  if (fun->instr_len == 0 || (fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN && fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RAISE)) z_x64_emit_epilogue(text);
  return true;
}

static unsigned machx64_rodata_base_offset(const IrProgram *program) {
  if (!program || program->data_segment_len == 0) return 0;
  unsigned base = program->data_segments[0].offset;
  for (size_t i = 1; i < program->data_segment_len; i++) {
    if (program->data_segments[i].offset < base) base = program->data_segments[i].offset;
  }
  return base;
}

static void machx64_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    while (rodata->len < segment->offset - base_offset) machx64_append_u8(rodata, 0);
    machx64_append_bytes(rodata, (const char *)segment->bytes, segment->len);
  }
}

static void machx64_patch_object_data_refs(ZBuf *text, const MachOEmitContext *ctx) {
  uint32_t const_addr = (uint32_t)machx64_align(text ? text->len : 0, 8);
  for (size_t i = 0; ctx && i < ctx->data_patch_len; i++) {
    const MachODataPatch *patch = &ctx->data_patches[i];
    int64_t displacement = (int64_t)const_addr + (int64_t)(patch->data_offset - ctx->rodata_base_offset) - (int64_t)(patch->patch_offset + 4u);
    z_x64_patch_u32(text, patch->patch_offset, (uint32_t)(int32_t)displacement);
  }
}

static void machx64_append_reloc(ZBuf *relocs, uint32_t address, uint32_t symbol_or_section, bool pcrel, unsigned length, bool external, unsigned type) {
  uint32_t reloc_info = (symbol_or_section & 0x00ffffffu) |
                        ((pcrel ? 1u : 0u) << 24) |
                        ((length & 3u) << 25) |
                        ((external ? 1u : 0u) << 27) |
                        ((type & 15u) << 28);
  z_macho_append_u32(relocs, address);
  z_macho_append_u32(relocs, reloc_info);
}

static void machx64_append_data_relocations(ZBuf *relocs, const MachOEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->data_patch_len; i++) {
    machx64_append_reloc(relocs, (uint32_t)ctx->data_patches[i].patch_offset, 2u, true, 2, false, 1);
  }
}

static size_t machx64_text_relocation_count(const MachOEmitContext *ctx) {
  if (!ctx) return 0;
  size_t count = ctx->call_patch_len + ctx->data_patch_len;
  for (unsigned i = 0; i < MACHO_RUNTIME_HELPER_COUNT; i++) count += ctx->runtime_patches[i].len;
  return count;
}

typedef struct {
  ZBuf text;
  ZBuf rodata;
  ZBuf relocs;
  ZBuf strings;
  size_t *offsets;
  uint32_t *function_string_offsets;
  uint32_t runtime_string_offsets[MACHO_RUNTIME_HELPER_COUNT];
  uint32_t rodata_string_offset;
  ZMachOSymbol *symbols;
  size_t symbol_len;
  uint32_t symbol_count;
  MachOEmitContext ctx;
  unsigned rodata_base_offset;
  bool has_rodata;
} MachX64ObjectBuild;

static void machx64_object_build_free(MachX64ObjectBuild *build) {
  if (!build) return;
  z_macho_emit_context_free(&build->ctx);
  free(build->symbols);
  free(build->function_string_offsets);
  free(build->offsets);
  zbuf_free(&build->strings);
  zbuf_free(&build->relocs);
  zbuf_free(&build->rodata);
  zbuf_free(&build->text);
}

static bool machx64_validate_object_program(const IrProgram *program, ZDiag *diag) {
  if (!program) return machx64_diag(diag, "direct x86_64 Mach-O backend received no program");
  if (!program->mir_valid) {
    bool ok = machx64_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (program->function_len == 0) return machx64_diag_at(diag, "direct x86_64 Mach-O object backend requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < program->function_len; i++) {
    if (program->functions[i].is_exported) has_export = true;
    if (!machx64_validate_function(&program->functions[i], diag)) return false;
  }
  if (!has_export) return machx64_diag_at(diag, "direct x86_64 Mach-O object backend requires at least one exported function", 1, 1, "no exported function");
  return true;
}

static bool machx64_object_build_init(MachX64ObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  memset(build, 0, sizeof(*build));
  zbuf_init(&build->text);
  zbuf_init(&build->rodata);
  zbuf_init(&build->relocs);
  zbuf_init(&build->strings);
  machx64_append_u8(&build->strings, 0);
  build->has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  build->rodata_base_offset = machx64_rodata_base_offset(program);
  if (build->has_rodata) machx64_append_rodata(&build->rodata, program, build->rodata_base_offset);
  build->offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  build->function_string_offsets = z_checked_calloc(program->function_len, sizeof(uint32_t));
  if (!build->offsets || !build->function_string_offsets) {
    machx64_object_build_free(build);
    return machx64_diag(diag, "out of memory while emitting x86_64 Mach-O object");
  }
  build->ctx = (MachOEmitContext){
    .program = program,
    .function_offsets = build->offsets,
    .function_count = program->function_len,
    .rodata_base_offset = build->rodata_base_offset
  };
  return true;
}

static bool machx64_object_emit_functions(MachX64ObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  for (size_t i = 0; i < program->function_len; i++) {
    machx64_pad_to(&build->text, machx64_align(build->text.len, 16));
    build->offsets[i] = build->text.len;
    if (!machx64_emit_function_text(&build->text, &program->functions[i], &build->ctx, diag)) return false;
    build->function_string_offsets[i] = (uint32_t)build->strings.len;
    zbuf_append_char(&build->strings, '_');
    zbuf_append(&build->strings, program->functions[i].name ? program->functions[i].name : "zero_fn");
    machx64_append_u8(&build->strings, 0);
  }
  return true;
}

static void machx64_object_append_relocations(MachX64ObjectBuild *build, const IrProgram *program) {
  machx64_patch_object_data_refs(&build->text, &build->ctx);
  z_macho_append_call_relocations(&build->relocs, &build->ctx);
  if (build->has_rodata) machx64_append_data_relocations(&build->relocs, &build->ctx);
  uint32_t next_symbol = (uint32_t)program->function_len + (build->has_rodata ? 1u : 0u);
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    z_macho_append_runtime_relocations(&build->relocs, &build->ctx, runtime_helper, next_symbol++);
  }
  build->symbol_count = next_symbol;
}

static void machx64_object_append_symbol_strings(MachX64ObjectBuild *build) {
  if (build->has_rodata) {
    build->rodata_string_offset = (uint32_t)build->strings.len;
    zbuf_append(&build->strings, "l_.zero_rodata");
    machx64_append_u8(&build->strings, 0);
  }
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    build->runtime_string_offsets[helper] = (uint32_t)build->strings.len;
    zbuf_append(&build->strings, z_macho_runtime_helper_symbol(runtime_helper));
    machx64_append_u8(&build->strings, 0);
  }
}

static bool machx64_object_build_symbols(MachX64ObjectBuild *build, const IrProgram *program, ZDiag *diag) {
  build->symbols = z_checked_calloc(build->symbol_count, sizeof(ZMachOSymbol));
  if (!build->symbols) return machx64_diag(diag, "out of memory while emitting x86_64 Mach-O symbols");
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
      .value = (uint32_t)machx64_align(build->text.len, 8)
    };
  }
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    build->symbols[build->symbol_len++] = (ZMachOSymbol){.string_offset = build->runtime_string_offsets[helper], .type = 0x01};
  }
  return true;
}

static void machx64_object_write(const MachX64ObjectBuild *build, ZBuf *out) {
  ZMachOObjectImage image = {
    .cpu = Z_MACHO_CPU_X86_64,
    .text = &build->text,
    .rodata = build->has_rodata ? &build->rodata : NULL,
    .relocs = &build->relocs,
    .strings = &build->strings,
    .symbols = build->symbols,
    .symbol_len = build->symbol_len,
    .text_reloc_count = (uint32_t)machx64_text_relocation_count(&build->ctx)
  };
  z_macho_write_object64(out, &image);
}

bool z_emit_macho_x64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!out) return machx64_diag(diag, "direct x86_64 Mach-O backend received no output buffer");
  if (!machx64_validate_object_program(program, diag)) return false;
  MachX64ObjectBuild build;
  if (!machx64_object_build_init(&build, program, diag)) return false;
  bool ok = machx64_object_emit_functions(&build, program, diag);
  if (ok) {
    machx64_object_append_relocations(&build, program);
    machx64_object_append_symbol_strings(&build);
    ok = machx64_object_build_symbols(&build, program, diag);
  }
  if (ok) machx64_object_write(&build, out);
  machx64_object_build_free(&build);
  return ok;
}

static const IrFunction *machx64_find_executable_main(const IrProgram *program, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; program && i < program->function_len; i++) {
    if (program->functions[i].is_exported && program->functions[i].name && strcmp(program->functions[i].name, "main") == 0) {
      if (fun) {
        machx64_diag_at(diag, "direct x86_64 Mach-O executable backend requires exactly one exported main function", program->functions[i].line, program->functions[i].column, program->functions[i].name);
        return NULL;
      }
      fun = &program->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    machx64_diag_at(diag, "direct x86_64 Mach-O executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (fun->param_count != 0) {
    machx64_diag_at(diag, "direct x86_64 Mach-O executable main must not take parameters", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (fun->return_type != IR_TYPE_VOID && !machx64_type_is_scalar32(fun->return_type)) {
    machx64_diag_at(diag, "direct x86_64 Mach-O executable main must return Void or a 32-bit-or-smaller scalar", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (out_index) *out_index = index;
  return fun;
}

typedef struct {
  ZBuf text;
  ZBuf rodata;
  ZBuf rebase;
  size_t *offsets;
  size_t start_call_patch;
  ZMachOExecutableLayout layout;
  MachOEmitContext ctx;
  unsigned main_index;
  unsigned rodata_base_offset;
  bool has_rodata;
} MachX64ExeBuild;

static size_t machx64_emit_exe_start_stub(ZBuf *text) {
  z_x64_emit_sub_rsp(text, 8);
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_add_rsp(text, 8);
  machx64_emit_error_condition_from_rax(text);
  size_t success_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_append_u8(text, 0xc3);
  z_x64_patch_rel32(text, success_patch, text->len);
  z_x64_append_u8(text, 0xc3);
  return patch;
}

static size_t machx64_emit_exe_world_write(ZBuf *text) {
  size_t offset = text->len;
  z_x64_emit_mov_eax_u32(text, 0x02000004u);
  z_x64_emit_syscall(text);
  z_x64_emit_xor_eax_eax(text);
  z_x64_append_u8(text, 0xc3);
  return offset;
}

static bool machx64_validate_exe_program(const IrProgram *program, unsigned *main_index, ZDiag *diag) {
  if (!program) return machx64_diag(diag, "direct x86_64 Mach-O executable backend received no program");
  if (!program->mir_valid) {
    bool ok = machx64_diag_at(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (!machx64_find_executable_main(program, diag, main_index)) return false;
  for (size_t i = 0; i < program->function_len; i++) if (!machx64_validate_function(&program->functions[i], diag)) return false;
  return true;
}

static void machx64_exe_build_free(MachX64ExeBuild *build) {
  if (!build) return;
  z_macho_emit_context_free(&build->ctx);
  free(build->offsets);
  zbuf_free(&build->rebase);
  zbuf_free(&build->rodata);
  zbuf_free(&build->text);
}

static bool machx64_exe_build_init(MachX64ExeBuild *build, const IrProgram *program, unsigned main_index, ZDiag *diag) {
  memset(build, 0, sizeof(*build));
  zbuf_init(&build->text);
  zbuf_init(&build->rodata);
  zbuf_init(&build->rebase);
  build->main_index = main_index;
  build->has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  build->rodata_base_offset = machx64_rodata_base_offset(program);
  if (build->has_rodata) machx64_append_rodata(&build->rodata, program, build->rodata_base_offset);
  build->offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!build->offsets) {
    machx64_exe_build_free(build);
    return machx64_diag(diag, "out of memory while emitting x86_64 Mach-O executable");
  }
  build->ctx = (MachOEmitContext){
    .program = program,
    .function_offsets = build->offsets,
    .function_count = program->function_len,
    .rodata_base_offset = build->rodata_base_offset
  };
  build->start_call_patch = machx64_emit_exe_start_stub(&build->text);
  machx64_pad_to(&build->text, machx64_align(build->text.len, 16));
  return true;
}

static bool machx64_exe_emit_functions(MachX64ExeBuild *build, const IrProgram *program, ZDiag *diag) {
  for (size_t i = 0; i < program->function_len; i++) {
    machx64_pad_to(&build->text, machx64_align(build->text.len, 16));
    build->offsets[i] = build->text.len;
    if (!machx64_emit_function_text(&build->text, &program->functions[i], &build->ctx, diag)) return false;
  }
  return true;
}

static bool machx64_exe_validate_runtime(const MachX64ExeBuild *build, ZDiag *diag) {
  return !z_macho_has_unsupported_exe_runtime_patches(&build->ctx) || machx64_diag_at(diag, "direct x86_64 Mach-O executable runtime helpers require object emission and an explicit runtime link step", 1, 1, "use --emit obj and link zero_runtime.c");
}

static void machx64_exe_patch_branches(MachX64ExeBuild *build) {
  size_t world_write_offset = 0;
  if (z_macho_runtime_patch_count(&build->ctx, MACHO_RUNTIME_WORLD_WRITE) > 0) {
    machx64_pad_to(&build->text, machx64_align(build->text.len, 16));
    world_write_offset = machx64_emit_exe_world_write(&build->text);
  }
  z_x64_patch_rel32(&build->text, build->start_call_patch, build->offsets[build->main_index]);
  for (size_t i = 0; i < build->ctx.call_patch_len; i++) {
    const MachOCallPatch *patch = &build->ctx.call_patches[i];
    z_x64_patch_rel32(&build->text, patch->patch_offset, build->offsets[patch->callee_index]);
  }
  const MachOPatchList *world_write_patches = z_macho_runtime_patch_list(&build->ctx, MACHO_RUNTIME_WORLD_WRITE);
  for (size_t i = 0; world_write_patches && i < world_write_patches->len; i++) {
    z_x64_patch_rel32(&build->text, world_write_patches->items[i].patch_offset, world_write_offset);
  }
}

static void machx64_exe_patch_data(MachX64ExeBuild *build, const char *code_signature_id) {
  z_macho_compute_executable64_layout(&build->layout, &build->text, build->has_rodata ? &build->rodata : NULL, &build->rebase, code_signature_id);
  for (size_t i = 0; i < build->ctx.data_patch_len; i++) {
    const MachODataPatch *patch = &build->ctx.data_patches[i];
    uint64_t target = build->layout.base_addr + build->layout.rodata_offset + (patch->data_offset - build->rodata_base_offset);
    uint64_t source_next = build->layout.base_addr + build->layout.text_offset + patch->patch_offset + 4u;
    int64_t displacement = (int64_t)target - (int64_t)source_next;
    z_x64_patch_u32(&build->text, patch->patch_offset, (uint32_t)(int32_t)displacement);
  }
  z_macho_compute_executable64_layout(&build->layout, &build->text, build->has_rodata ? &build->rodata : NULL, &build->rebase, code_signature_id);
}

static void machx64_exe_write(const MachX64ExeBuild *build, ZBuf *out, const char *code_signature_id) {
  ZMachOExecutableImage image = {
    .cpu = Z_MACHO_CPU_X86_64,
    .text = &build->text,
    .rodata = build->has_rodata ? &build->rodata : NULL,
    .rebase = &build->rebase,
    .layout = build->layout,
    .code_signature_id = code_signature_id
  };
  z_macho_write_executable64(out, &image);
}

bool z_emit_macho_x64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return machx64_diag(diag, "direct x86_64 Mach-O executable backend received no program");
  unsigned main_index = 0;
  if (!machx64_validate_exe_program(program, &main_index, diag)) return false;
  MachX64ExeBuild build;
  if (!machx64_exe_build_init(&build, program, main_index, diag)) return false;
  bool ok = machx64_exe_emit_functions(&build, program, diag) && machx64_exe_validate_runtime(&build, diag);
  if (ok) {
    const char *code_signature_id = "zero-direct-x64";
    machx64_exe_patch_branches(&build);
    machx64_exe_patch_data(&build, code_signature_id);
    machx64_exe_write(&build, out, code_signature_id);
  }
  machx64_exe_build_free(&build);
  return ok;
}
