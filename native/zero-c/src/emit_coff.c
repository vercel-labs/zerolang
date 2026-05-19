#include "zero.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xffu));
}

static void append_u16le(ZBuf *buf, uint16_t value) {
  append_u8(buf, value);
  append_u8(buf, value >> 8);
}

static void append_u32le(ZBuf *buf, uint32_t value) {
  append_u8(buf, value);
  append_u8(buf, value >> 8);
  append_u8(buf, value >> 16);
  append_u8(buf, value >> 24);
}

static void append_u64le(ZBuf *buf, uint64_t value) {
  append_u32le(buf, (uint32_t)(value & 0xffffffffu));
  append_u32le(buf, (uint32_t)(value >> 32));
}

static void patch_u32le(ZBuf *buf, size_t offset, uint32_t value) {
  buf->data[offset + 0] = (char)(value & 0xff);
  buf->data[offset + 1] = (char)((value >> 8) & 0xff);
  buf->data[offset + 2] = (char)((value >> 16) & 0xff);
  buf->data[offset + 3] = (char)((value >> 24) & 0xff);
}

static void patch_u64le(ZBuf *buf, size_t offset, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) buf->data[offset + i] = (char)((value >> (i * 8)) & 0xffu);
}

static void append_bytes(ZBuf *buf, const char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) append_u8(buf, (unsigned char)bytes[i]);
}

static void append_zeros(ZBuf *buf, size_t len) {
  for (size_t i = 0; i < len; i++) append_u8(buf, 0);
}

static void append_coff_name(ZBuf *buf, const char *name) {
  size_t len = name ? strlen(name) : 0;
  if (len > 8) len = 8;
  for (size_t i = 0; i < len; i++) append_u8(buf, (unsigned char)name[i]);
  for (size_t i = len; i < 8; i++) append_u8(buf, 0);
}

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

static void append_symbol_name(ZBuf *buf, const char *name, ZBuf *strings) {
  size_t len = name ? strlen(name) : 0;
  if (len <= 8) {
    append_coff_name(buf, name);
    return;
  }
  append_u32le(buf, 0);
  append_u32le(buf, (uint32_t)strings->len + 4);
  zbuf_append(strings, name);
  append_u8(strings, 0);
}

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
} CoffCallPatch;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} CoffRodataPatch;

typedef struct {
  size_t patch_offset;
} CoffWorldWritePatch;

typedef struct {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  CoffCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  CoffRodataPatch *rodata_patches;
  size_t rodata_patch_len;
  size_t rodata_patch_cap;
  CoffWorldWritePatch *world_write_patches;
  size_t world_write_patch_len;
  size_t world_write_patch_cap;
  unsigned rodata_base_offset;
} CoffEmitContext;

static size_t coff_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
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

static void coff_emit_rbp_disp_reg(ZBuf *text, unsigned opcode, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    append_u8(text, rex);
  }
  append_u8(text, opcode);
  unsigned reg_low = reg & 7u;
  if (offset <= 127) {
    append_u8(text, 0x40 | (reg_low << 3) | 0x05);
    append_u8(text, (unsigned char)(-(int)offset));
  } else {
    append_u8(text, 0x80 | (reg_low << 3) | 0x05);
    append_u32le(text, (uint32_t)(-(int32_t)offset));
  }
}

static void coff_emit_load_rbp_positive_reg(ZBuf *text, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    append_u8(text, rex);
  }
  append_u8(text, 0x8b);
  unsigned reg_low = reg & 7u;
  if (offset <= 127) {
    append_u8(text, 0x40 | (reg_low << 3) | 0x05);
    append_u8(text, (unsigned char)offset);
  } else {
    append_u8(text, 0x80 | (reg_low << 3) | 0x05);
    append_u32le(text, offset);
  }
}

static void coff_emit_load_local_eax(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  coff_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_offset(fun, local_index), false);
}

static void coff_emit_load_local_slot_rax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  coff_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), true);
}

static void coff_emit_load_local_slot_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset) {
  coff_emit_rbp_disp_reg(text, 0x8b, 0, coff_local_slot_offset(fun, local_index, slot_offset), false);
}

static void coff_emit_load_local_slot_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned slot_offset, unsigned reg, bool wide) {
  coff_emit_rbp_disp_reg(text, 0x8b, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide);
}

static void coff_emit_store_local_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg) {
  coff_emit_rbp_disp_reg(text, 0x89, reg, coff_local_offset(fun, local_index), false);
}

static void coff_emit_store_local_slot_from_reg(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned reg, unsigned slot_offset, bool wide) {
  coff_emit_rbp_disp_reg(text, 0x89, reg, coff_local_slot_offset(fun, local_index, slot_offset), wide);
}

static void coff_emit_load_field_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    append_u8(text, 0x0f);
    coff_emit_rbp_disp_reg(text, 0xb6, 0, offset, false);
  } else {
    coff_emit_rbp_disp_reg(text, 0x8b, 0, offset, false);
  }
}

static void coff_emit_store_field_from_eax(ZBuf *text, const IrFunction *fun, unsigned local_index, unsigned field_offset, IrTypeKind type) {
  unsigned offset = coff_local_slot_offset(fun, local_index, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    coff_emit_rbp_disp_reg(text, 0x88, 0, offset, false);
  } else {
    coff_emit_rbp_disp_reg(text, 0x89, 0, offset, false);
  }
}

static size_t coff_emit_jcc32_placeholder(ZBuf *text, unsigned opcode);
static void coff_patch_rel32(ZBuf *buf, size_t patch_offset, size_t target_offset);

static void coff_emit_array_base_rdx(ZBuf *text, const IrFunction *fun, unsigned local_index) {
  coff_emit_rbp_disp_reg(text, 0x8d, 2, coff_local_offset(fun, local_index), true);
}

static void coff_emit_u8_array_bounds_check(ZBuf *text, const IrLocal *local) {
  append_u8(text, 0x3d);
  append_u32le(text, local ? local->array_len : 0);
  size_t ok_patch = coff_emit_jcc32_placeholder(text, 0x82);
  append_u8(text, 0x0f);
  append_u8(text, 0x0b);
  coff_patch_rel32(text, ok_patch, text->len);
}

static void coff_emit_pop_reg64(ZBuf *text, unsigned reg) {
  if (reg >= 8) append_u8(text, 0x41);
  append_u8(text, 0x58 + (reg & 7u));
}

static void coff_emit_sub_rsp(ZBuf *text, unsigned amount) {
  if (amount == 0) return;
  append_u8(text, 0x48);
  if (amount <= 127) {
    append_u8(text, 0x83);
    append_u8(text, 0xec);
    append_u8(text, amount);
  } else {
    append_u8(text, 0x81);
    append_u8(text, 0xec);
    append_u32le(text, amount);
  }
}

static void coff_emit_add_rsp(ZBuf *text, unsigned amount) {
  if (amount == 0) return;
  append_u8(text, 0x48);
  if (amount <= 127) {
    append_u8(text, 0x83);
    append_u8(text, 0xc4);
    append_u8(text, amount);
  } else {
    append_u8(text, 0x81);
    append_u8(text, 0xc4);
    append_u32le(text, amount);
  }
}

static void coff_emit_epilogue(ZBuf *text) {
  append_u8(text, 0xc9);
  append_u8(text, 0xc3);
}

static size_t coff_emit_jmp32_placeholder(ZBuf *text, unsigned opcode) {
  append_u8(text, opcode);
  size_t patch = text->len;
  append_u32le(text, 0);
  return patch;
}

static size_t coff_emit_jcc32_placeholder(ZBuf *text, unsigned opcode) {
  append_u8(text, 0x0f);
  append_u8(text, opcode);
  size_t patch = text->len;
  append_u32le(text, 0);
  return patch;
}

static void coff_patch_rel32(ZBuf *text, size_t patch_offset, size_t target_offset) {
  int64_t rel = (int64_t)target_offset - (int64_t)(patch_offset + 4);
  patch_u32le(text, patch_offset, (uint32_t)(int32_t)rel);
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

static bool coff_record_call_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  if (!ctx || callee_index >= ctx->function_count) {
    return coff_diag_at(diag, "direct COFF call target index is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len == ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(CoffCallPatch));
  }
  ctx->call_patches[ctx->call_patch_len++] = (CoffCallPatch){.patch_offset = patch_offset, .callee_index = callee_index};
  return true;
}

static bool coff_record_rodata_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  if (!ctx) return coff_diag_at(diag, "direct COFF readonly data relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->rodata_patch_len == ctx->rodata_patch_cap) {
    ctx->rodata_patch_cap = z_grow_capacity(ctx->rodata_patch_cap, ctx->rodata_patch_len + 1, 8);
    ctx->rodata_patches = z_checked_reallocarray(ctx->rodata_patches, ctx->rodata_patch_cap, sizeof(CoffRodataPatch));
  }
  ctx->rodata_patches[ctx->rodata_patch_len++] = (CoffRodataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static bool coff_record_world_write_patch(CoffEmitContext *ctx, size_t patch_offset, const IrInstr *instr, ZDiag *diag) {
  if (!ctx) return coff_diag_at(diag, "direct COFF World write relocation requires an emit context", instr ? instr->line : 1, instr ? instr->column : 1, "missing context");
  if (ctx->world_write_patch_len == ctx->world_write_patch_cap) {
    ctx->world_write_patch_cap = z_grow_capacity(ctx->world_write_patch_cap, ctx->world_write_patch_len + 1, 4);
    ctx->world_write_patches = z_checked_reallocarray(ctx->world_write_patches, ctx->world_write_patch_cap, sizeof(CoffWorldWritePatch));
  }
  ctx->world_write_patches[ctx->world_write_patch_len++] = (CoffWorldWritePatch){.patch_offset = patch_offset};
  return true;
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
  append_u8(text, 0x48);
  append_u8(text, 0xb8);
  size_t patch = text->len;
  uint64_t addend = data_offset - (ctx ? ctx->rodata_base_offset : 0);
  for (unsigned i = 0; i < 8; i++) append_u8(text, (unsigned)((addend >> (i * 8)) & 0xffu));
  return coff_record_rodata_patch(ctx, patch, data_offset, value, diag);
}

static bool coff_emit_byte_view_ptr(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);
static bool coff_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag);

static bool coff_emit_byte_view_len(ZBuf *text, const IrFunction *fun, const IrValue *view, CoffEmitContext *ctx, ZDiag *diag) {
  unsigned len = 0;
  if (coff_byte_view_const_len(view, &len)) {
    append_u8(text, 0xb8);
    append_u32le(text, len);
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
      append_u8(text, 0xb8);
      append_u32le(text, end - start);
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
    coff_emit_rbp_disp_reg(text, 0x8d, 0, coff_local_offset(fun, view->array_index), true);
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
      append_u8(text, 0x48);
      append_u8(text, 0x05);
      append_u32le(text, start);
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
      append_u8(text, 0xb8);
      append_u32le(text, (uint32_t)value->int_value);
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
      append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
      append_u8(text, 0x89);
      append_u8(text, 0xc1);
      append_u8(text, 0x58);
      if (value->binary_op == IR_BIN_MUL) {
        append_u8(text, 0x0f);
        append_u8(text, 0xaf);
        append_u8(text, 0xc1);
      } else {
        append_u8(text, value->binary_op == IR_BIN_ADD ? 0x01 : 0x29);
        append_u8(text, 0xc8);
      }
      return true;
    case IR_VALUE_COMPARE:
      if (!value->left || !value->right) return coff_diag_at(diag, "direct COFF comparison requires two operands", value->line, value->column, "invalid comparison");
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
      append_u8(text, 0x89);
      append_u8(text, 0xc1);
      append_u8(text, 0x58);
      append_u8(text, 0x39);
      append_u8(text, 0xc8);
      append_u8(text, 0x0f);
      append_u8(text, coff_setcc_opcode(value->compare_op));
      append_u8(text, 0xc0);
      append_u8(text, 0x0f);
      append_u8(text, 0xb6);
      append_u8(text, 0xc0);
      return true;
    case IR_VALUE_CALL: {
      static const unsigned param_regs[] = {1, 2, 8, 9};
      if (value->arg_len > 4) return coff_diag_at(diag, "direct COFF call supports at most four integer arguments", value->line, value->column, "too many arguments");
      for (size_t i = 0; i < value->arg_len; i++) {
        if (!coff_emit_value(text, fun, value->args[i], ctx, diag)) return false;
        append_u8(text, 0x50);
      }
      for (size_t i = value->arg_len; i > 0; i--) {
        coff_emit_pop_reg64(text, param_regs[i - 1]);
      }
      coff_emit_sub_rsp(text, 32);
      append_u8(text, 0xe8);
      size_t patch = text->len;
      append_u32le(text, 0);
      coff_emit_add_rsp(text, 32);
      return coff_record_call_patch(ctx, patch, value->callee_index, value, diag);
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
      append_u8(text, 0x39);
      append_u8(text, 0xc8);
      size_t ok_patch = coff_emit_jcc32_placeholder(text, 0x82);
      append_u8(text, 0xb8);
      append_u32le(text, 0);
      size_t end_patch = coff_emit_jmp32_placeholder(text, 0xe9);
      coff_patch_rel32(text, ok_patch, text->len);
      append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x59);
      coff_emit_load_local_slot_reg(text, fun, value->local_index, 0, 2, true);
      append_u8(text, 0x48);
      append_u8(text, 0x01);
      append_u8(text, 0xca);
      append_u8(text, 0x88);
      append_u8(text, 0x02);
      append_u8(text, 0x89);
      append_u8(text, 0xc8);
      append_u8(text, 0x83);
      append_u8(text, 0xc0);
      append_u8(text, 0x01);
      coff_emit_store_local_slot_from_reg(text, fun, value->local_index, 0, 8, false);
      append_u8(text, 0xb8);
      append_u32le(text, 1);
      coff_patch_rel32(text, end_patch, text->len);
      return true;
    }
    case IR_VALUE_MEMORY_PEEK_U8:
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x0f);
      append_u8(text, 0xb6);
      append_u8(text, 0x00);
      return true;
    case IR_VALUE_MEMORY_POKE_U8:
      if (!coff_emit_value(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, value->right, ctx, diag)) return false;
      append_u8(text, 0x59);
      append_u8(text, 0x88);
      append_u8(text, 0x01);
      append_u8(text, 0xb8);
      append_u32le(text, 1);
      return true;
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
        append_u8(text, 0xb8);
        append_u32le(text, byte);
        return true;
      }
      if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
      append_u8(text, 0x50);
      if (!coff_emit_byte_view_len(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x89);
      append_u8(text, 0xc1);
      append_u8(text, 0x58);
      append_u8(text, 0x39);
      append_u8(text, 0xc8);
      size_t ok_patch = coff_emit_jcc32_placeholder(text, 0x82);
      append_u8(text, 0x0f);
      append_u8(text, 0x0b);
      coff_patch_rel32(text, ok_patch, text->len);
      append_u8(text, 0x50);
      if (!coff_emit_byte_view_ptr(text, fun, value->left, ctx, diag)) return false;
      append_u8(text, 0x59);
      append_u8(text, 0x48);
      append_u8(text, 0x01);
      append_u8(text, 0xc8);
      append_u8(text, 0x0f);
      append_u8(text, 0xb6);
      append_u8(text, 0x00);
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
        append_u8(text, 0x50);
        coff_emit_array_base_rdx(text, fun, value->array_index);
        append_u8(text, 0x59);
        append_u8(text, 0x48);
        append_u8(text, 0xc1);
        append_u8(text, 0xe1);
        append_u8(text, 0x02);
        append_u8(text, 0x48);
        append_u8(text, 0x01);
        append_u8(text, 0xca);
        append_u8(text, 0x8b);
        append_u8(text, 0x02);
        return true;
      }
      if (!local->is_array || local->element_type != IR_TYPE_U8) return coff_diag_at(diag, "direct COFF indexed load requires [N]u8 or integer arrays", value->line, value->column, "unsupported array local");
      if (!value->index || !coff_emit_value(text, fun, value->index, ctx, diag)) return false;
      coff_emit_u8_array_bounds_check(text, local);
      append_u8(text, 0x50);
      coff_emit_array_base_rdx(text, fun, value->array_index);
      append_u8(text, 0x59);
      append_u8(text, 0x48);
      append_u8(text, 0x01);
      append_u8(text, 0xca);
      append_u8(text, 0x0f);
      append_u8(text, 0xb6);
      append_u8(text, 0x02);
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
  append_u8(text, 0x50); // push rax, preserve ptr while computing len
  if (!coff_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
  append_u8(text, 0x41);
  append_u8(text, 0x89);
  append_u8(text, 0xc0); // mov r8d, eax
  append_u8(text, 0x5a); // pop rdx
  append_u8(text, 0xb9);
  append_u32le(text, instr->field_offset == 2 ? 2u : 1u); // ecx = fd
  coff_emit_sub_rsp(text, 32);
  append_u8(text, 0xe8);
  size_t patch = text->len;
  append_u32le(text, 0);
  coff_emit_add_rsp(text, 32);
  append_u8(text, 0x85);
  append_u8(text, 0xc0); // test eax, eax
  size_t ok_patch = coff_emit_jcc32_placeholder(text, 0x84);
  append_u8(text, 0x0f);
  append_u8(text, 0x0b); // ud2 on runtime write failure
  coff_patch_rel32(text, ok_patch, text->len);
  return coff_record_world_write_patch(ctx, patch, instr, diag);
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
      append_u8(text, 0xb8);
      append_u32le(text, 0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 12, false);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return coff_diag_at(diag, "direct COFF Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!coff_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, true);
      append_u8(text, 0xb8);
      append_u32le(text, 0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, false);
      if (!coff_emit_byte_view_len(text, fun, instr->value->left, ctx, diag)) return false;
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 12, false);
      return true;
    }
    if (fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return coff_diag_at(diag, "direct COFF allocation source is invalid", instr->line, instr->column, "invalid allocation");
      if (!coff_emit_value(text, fun, instr->value->left, ctx, diag)) return false;
      append_u8(text, 0x50);
      coff_emit_load_local_slot_eax(text, fun, instr->value->local_index, 12);
      coff_emit_load_local_slot_reg(text, fun, instr->value->local_index, 8, 1, false);
      append_u8(text, 0x58);
      append_u8(text, 0x89);
      append_u8(text, 0xc2);
      append_u8(text, 0x01);
      append_u8(text, 0xc8);
      append_u8(text, 0x39);
      append_u8(text, 0xc8);
      size_t ok_patch = coff_emit_jcc32_placeholder(text, 0x86);
      append_u8(text, 0xb8);
      append_u32le(text, 0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 8, true);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
      size_t end_patch = coff_emit_jmp32_placeholder(text, 0xe9);
      coff_patch_rel32(text, ok_patch, text->len);
      append_u8(text, 0xb8);
      append_u32le(text, 1);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 0, false);
      coff_emit_load_local_slot_reg(text, fun, instr->value->local_index, 0, 2, true);
      append_u8(text, 0x48);
      append_u8(text, 0x01);
      append_u8(text, 0xd2);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 2, 8, true);
      append_u8(text, 0x89);
      append_u8(text, 0xd0);
      coff_emit_store_local_slot_from_reg(text, fun, instr->local_index, 0, 16, false);
      append_u8(text, 0x89);
      append_u8(text, 0xc8);
      coff_emit_store_local_slot_from_reg(text, fun, instr->value->local_index, 0, 12, false);
      coff_patch_rel32(text, end_patch, text->len);
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
      append_u8(text, 0x50);
      if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
      append_u8(text, 0x59);
      coff_emit_array_base_rdx(text, fun, instr->array_index);
      append_u8(text, 0x48);
      append_u8(text, 0xc1);
      append_u8(text, 0xe1);
      append_u8(text, 0x02);
      append_u8(text, 0x48);
      append_u8(text, 0x01);
      append_u8(text, 0xca);
      append_u8(text, 0x89);
      append_u8(text, 0x02);
      return true;
    }
    if (!local->is_array || local->element_type != IR_TYPE_U8) return coff_diag_at(diag, "direct COFF indexed store requires [N]u8 or integer arrays", instr->line, instr->column, "unsupported array local");
    if (!instr->index || !coff_emit_value(text, fun, instr->index, ctx, diag)) return false;
    coff_emit_u8_array_bounds_check(text, local);
    append_u8(text, 0x50);
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    append_u8(text, 0x59);
    coff_emit_array_base_rdx(text, fun, instr->array_index);
    append_u8(text, 0x48);
    append_u8(text, 0x01);
    append_u8(text, 0xca);
    append_u8(text, 0x88);
    append_u8(text, 0x02);
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
    append_u8(text, 0x85);
    append_u8(text, 0xc0);
    size_t false_patch = coff_emit_jcc32_placeholder(text, 0x84);
    if (!coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = coff_emit_jmp32_placeholder(text, 0xe9);
      coff_patch_rel32(text, false_patch, text->len);
      if (!coff_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
      coff_patch_rel32(text, end_patch, text->len);
    } else {
      coff_patch_rel32(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!coff_emit_value(text, fun, instr->value, ctx, diag)) return false;
    append_u8(text, 0x85);
    append_u8(text, 0xc0);
    size_t false_patch = coff_emit_jcc32_placeholder(text, 0x84);
    if (!coff_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    size_t loop_patch = coff_emit_jmp32_placeholder(text, 0xe9);
    coff_patch_rel32(text, loop_patch, loop_start);
    coff_patch_rel32(text, false_patch, text->len);
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
  unsigned frame_size = (unsigned)coff_align(fun ? fun->frame_bytes : 0, 16);
  append_u8(text, 0x55);
  append_u8(text, 0x48);
  append_u8(text, 0x89);
  append_u8(text, 0xe5);
  coff_emit_sub_rsp(text, frame_size);
  for (size_t i = 0; i < fun->param_count; i++) {
    if (i < 4) {
      coff_emit_store_local_from_reg(text, fun, (unsigned)i, param_regs[i]);
    } else {
      coff_emit_load_rbp_positive_reg(text, 0, 48u + (unsigned)(i - 4u) * 8u, false);
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
    while (rodata->len < segment->offset - base_offset) append_u8(rodata, 0);
    append_bytes(rodata, (const char *)segment->bytes, segment->len);
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

  zbuf_init(out);

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
    while (text.len % 16 != 0) append_u8(&text, 0x90);
    offsets[i] = text.len;
    if (!coff_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      free(ctx.world_write_patches);
      free(ctx.rodata_patches);
      free(ctx.call_patches);
      free(offsets);
      zbuf_free(&relocs);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
  }

  unsigned section_symbol_count = has_rodata ? 2u : 1u;
  for (size_t i = 0; i < ctx.call_patch_len; i++) {
    const CoffCallPatch *patch = &ctx.call_patches[i];
    int64_t rel = (int64_t)offsets[patch->callee_index] - (int64_t)(patch->patch_offset + 4);
    patch_u32le(&text, patch->patch_offset, (uint32_t)(int32_t)rel);
    append_u32le(&relocs, (uint32_t)patch->patch_offset);
    append_u32le(&relocs, section_symbol_count + patch->callee_index);
    append_u16le(&relocs, 0x0004); // IMAGE_REL_AMD64_REL32
  }
  for (size_t i = 0; i < ctx.rodata_patch_len; i++) {
    const CoffRodataPatch *patch = &ctx.rodata_patches[i];
    append_u32le(&relocs, (uint32_t)patch->patch_offset);
    append_u32le(&relocs, 1u); // .rdata section symbol
    append_u16le(&relocs, 0x0001); // IMAGE_REL_AMD64_ADDR64
  }
  uint32_t world_write_symbol_index = section_symbol_count + (uint32_t)program->function_len;
  for (size_t i = 0; i < ctx.world_write_patch_len; i++) {
    const CoffWorldWritePatch *patch = &ctx.world_write_patches[i];
    append_u32le(&relocs, (uint32_t)patch->patch_offset);
    append_u32le(&relocs, world_write_symbol_index);
    append_u16le(&relocs, 0x0004); // IMAGE_REL_AMD64_REL32
  }

  ZBuf strings;
  zbuf_init(&strings);
  const uint16_t section_count = has_rodata ? 2 : 1;
  const uint32_t header_size = 20 + 40 * section_count;
  const uint32_t raw_text_offset = header_size;
  const uint32_t raw_rodata_offset = has_rodata ? raw_text_offset + (uint32_t)text.len : 0;
  const uint32_t reloc_offset = raw_text_offset + (uint32_t)text.len + (uint32_t)rodata.len;
  const uint32_t symbol_offset = reloc_offset + (uint32_t)relocs.len;
  const uint32_t symbol_count = section_symbol_count + (uint32_t)program->function_len + (ctx.world_write_patch_len > 0 ? 1u : 0u);

  append_u16le(out, 0x8664);           // IMAGE_FILE_MACHINE_AMD64
  append_u16le(out, section_count);
  append_u32le(out, 0);
  append_u32le(out, symbol_offset);
  append_u32le(out, symbol_count);
  append_u16le(out, 0);
  append_u16le(out, 0);

  append_coff_name(out, ".text");
  append_u32le(out, (uint32_t)text.len);
  append_u32le(out, 0);
  append_u32le(out, (uint32_t)text.len);
  append_u32le(out, raw_text_offset);
  append_u32le(out, relocs.len > 0 ? reloc_offset : 0);
  append_u32le(out, 0);
  append_u16le(out, (uint16_t)(ctx.call_patch_len + ctx.rodata_patch_len + ctx.world_write_patch_len));
  append_u16le(out, 0);
  append_u32le(out, 0x60000020u);      // code | execute | read

  if (has_rodata) {
    append_coff_name(out, ".rdata");
    append_u32le(out, (uint32_t)rodata.len);
    append_u32le(out, 0);
    append_u32le(out, (uint32_t)rodata.len);
    append_u32le(out, raw_rodata_offset);
    append_u32le(out, 0);
    append_u32le(out, 0);
    append_u16le(out, 0);
    append_u16le(out, 0);
    append_u32le(out, 0x40000040u);    // initialized data | read
  }

  if (text.data) append_bytes(out, text.data, text.len);
  if (rodata.data) append_bytes(out, rodata.data, rodata.len);
  if (relocs.data) append_bytes(out, relocs.data, relocs.len);

  append_coff_name(out, ".text");
  append_u32le(out, 0);
  append_u16le(out, 1);
  append_u16le(out, 0);
  append_u8(out, 3);                   // IMAGE_SYM_CLASS_STATIC
  append_u8(out, 0);

  if (has_rodata) {
    append_coff_name(out, ".rdata");
    append_u32le(out, 0);
    append_u16le(out, 2);
    append_u16le(out, 0);
    append_u8(out, 3);
    append_u8(out, 0);
  }

  for (size_t i = 0; i < program->function_len; i++) {
    append_symbol_name(out, program->functions[i].name ? program->functions[i].name : "zero_fn", &strings);
    append_u32le(out, (uint32_t)offsets[i]);
    append_u16le(out, 1);
    append_u16le(out, 0x20);           // function
    append_u8(out, program->functions[i].is_exported ? 2 : 3);
    append_u8(out, 0);
  }

  if (ctx.world_write_patch_len > 0) {
    append_symbol_name(out, "zero_world_write", &strings);
    append_u32le(out, 0);
    append_u16le(out, 0);              // undefined external
    append_u16le(out, 0x20);           // function
    append_u8(out, 2);                 // IMAGE_SYM_CLASS_EXTERNAL
    append_u8(out, 0);
  }

  append_u32le(out, (uint32_t)strings.len + 4);
  if (strings.data) append_bytes(out, strings.data, strings.len);
  if (strings.len == 0) append_zeros(out, 0);

  free(ctx.world_write_patches);
  free(ctx.rodata_patches);
  free(ctx.call_patches);
  free(offsets);
  zbuf_free(&strings);
  zbuf_free(&relocs);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}

typedef struct {
  size_t patch_offset;
  unsigned import_index;
} CoffImportPatch;

typedef struct {
  uint32_t import_directory_rva;
  uint32_t import_directory_size;
  uint32_t iat_rva;
  uint32_t iat_size;
  uint32_t iat_offsets[3];
} CoffImportLayout;

enum {
  COFF_IMPORT_EXIT_PROCESS = 0,
  COFF_IMPORT_GET_STD_HANDLE = 1,
  COFF_IMPORT_WRITE_FILE = 2,
  COFF_IMPORT_COUNT = 3
};

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

static void coff_patch_rel32_to_va(ZBuf *text, size_t patch_offset, uint64_t text_base_va, uint64_t target_va) {
  int64_t rel = (int64_t)target_va - (int64_t)(text_base_va + patch_offset + 4);
  patch_u32le(text, patch_offset, (uint32_t)(int32_t)rel);
}

static void coff_emit_import_call(ZBuf *text, CoffImportPatch *patches, size_t *patch_len, unsigned import_index) {
  append_u8(text, 0xff);
  append_u8(text, 0x15);
  size_t patch = text->len;
  append_u32le(text, 0);
  if (patches && patch_len && *patch_len < 8) {
    patches[*patch_len] = (CoffImportPatch){.patch_offset = patch, .import_index = import_index};
    *patch_len += 1;
  }
}

static size_t coff_emit_exe_start_stub(ZBuf *text, CoffImportPatch *import_patches, size_t *import_patch_len) {
  coff_emit_sub_rsp(text, 40);
  append_u8(text, 0xe8);
  size_t main_patch = text->len;
  append_u32le(text, 0);
  append_u8(text, 0x89);
  append_u8(text, 0xc1); // mov ecx, eax
  coff_emit_import_call(text, import_patches, import_patch_len, COFF_IMPORT_EXIT_PROCESS);
  append_u8(text, 0xcc);
  return main_patch;
}

static size_t coff_emit_exe_world_write(ZBuf *text, CoffImportPatch *import_patches, size_t *import_patch_len) {
  size_t offset = text->len;
  coff_emit_sub_rsp(text, 72);
  append_u8(text, 0x48);
  append_u8(text, 0x89);
  append_u8(text, 0x54);
  append_u8(text, 0x24);
  append_u8(text, 0x28); // mov [rsp+40], rdx
  append_u8(text, 0x4c);
  append_u8(text, 0x89);
  append_u8(text, 0x44);
  append_u8(text, 0x24);
  append_u8(text, 0x30); // mov [rsp+48], r8
  append_u8(text, 0xc7);
  append_u8(text, 0x44);
  append_u8(text, 0x24);
  append_u8(text, 0x38);
  append_u32le(text, 0); // DWORD bytes_written = 0
  append_u8(text, 0x83);
  append_u8(text, 0xf9);
  append_u8(text, 0x02); // cmp ecx, 2
  append_u8(text, 0xb9);
  append_u32le(text, 0xfffffff5u); // STD_OUTPUT_HANDLE
  append_u8(text, 0x75);
  append_u8(text, 0x05); // jne after stderr handle
  append_u8(text, 0xb9);
  append_u32le(text, 0xfffffff4u); // STD_ERROR_HANDLE
  coff_emit_import_call(text, import_patches, import_patch_len, COFF_IMPORT_GET_STD_HANDLE);
  append_u8(text, 0x48);
  append_u8(text, 0x89);
  append_u8(text, 0xc1); // mov rcx, rax
  append_u8(text, 0x48);
  append_u8(text, 0x8b);
  append_u8(text, 0x54);
  append_u8(text, 0x24);
  append_u8(text, 0x28); // mov rdx, [rsp+40]
  append_u8(text, 0x4c);
  append_u8(text, 0x8b);
  append_u8(text, 0x44);
  append_u8(text, 0x24);
  append_u8(text, 0x30); // mov r8, [rsp+48]
  append_u8(text, 0x4c);
  append_u8(text, 0x8d);
  append_u8(text, 0x4c);
  append_u8(text, 0x24);
  append_u8(text, 0x38); // lea r9, [rsp+56]
  append_u8(text, 0x48);
  append_u8(text, 0xc7);
  append_u8(text, 0x44);
  append_u8(text, 0x24);
  append_u8(text, 0x20);
  append_u32le(text, 0); // lpOverlapped = NULL
  coff_emit_import_call(text, import_patches, import_patch_len, COFF_IMPORT_WRITE_FILE);
  append_u8(text, 0x31);
  append_u8(text, 0xc0); // xor eax, eax
  coff_emit_add_rsp(text, 72);
  append_u8(text, 0xc3);
  return offset;
}

static void coff_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) append_u8(buf, 0);
}

static void coff_append_import_table(ZBuf *rdata, uint32_t rdata_rva, CoffImportLayout *layout) {
  static const char *names[COFF_IMPORT_COUNT] = {"ExitProcess", "GetStdHandle", "WriteFile"};
  coff_pad_to(rdata, coff_align(rdata->len, 4));
  uint32_t descriptor_offset = (uint32_t)rdata->len;
  append_zeros(rdata, 40); // one IMAGE_IMPORT_DESCRIPTOR plus terminator
  coff_pad_to(rdata, coff_align(rdata->len, 8));
  uint32_t int_offset = (uint32_t)rdata->len;
  append_zeros(rdata, (COFF_IMPORT_COUNT + 1) * 8);
  coff_pad_to(rdata, coff_align(rdata->len, 8));
  uint32_t iat_offset = (uint32_t)rdata->len;
  append_zeros(rdata, (COFF_IMPORT_COUNT + 1) * 8);
  uint32_t dll_name_offset = (uint32_t)rdata->len;
  append_bytes(rdata, "KERNEL32.dll", strlen("KERNEL32.dll") + 1);

  uint32_t hint_name_offsets[COFF_IMPORT_COUNT];
  for (unsigned i = 0; i < COFF_IMPORT_COUNT; i++) {
    coff_pad_to(rdata, coff_align(rdata->len, 2));
    hint_name_offsets[i] = (uint32_t)rdata->len;
    append_u16le(rdata, 0);
    append_bytes(rdata, names[i], strlen(names[i]) + 1);
  }

  patch_u32le(rdata, descriptor_offset + 0, rdata_rva + int_offset);
  patch_u32le(rdata, descriptor_offset + 12, rdata_rva + dll_name_offset);
  patch_u32le(rdata, descriptor_offset + 16, rdata_rva + iat_offset);
  for (unsigned i = 0; i < COFF_IMPORT_COUNT; i++) {
    uint64_t thunk = rdata_rva + hint_name_offsets[i];
    patch_u64le(rdata, int_offset + i * 8, thunk);
    patch_u64le(rdata, iat_offset + i * 8, thunk);
    if (layout) layout->iat_offsets[i] = iat_offset + i * 8;
  }
  if (layout) {
    layout->import_directory_rva = rdata_rva + descriptor_offset;
    layout->import_directory_size = 40;
    layout->iat_rva = rdata_rva + iat_offset;
    layout->iat_size = (COFF_IMPORT_COUNT + 1) * 8;
  }
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
  CoffImportPatch import_patches[8];
  size_t import_patch_len = 0;
  size_t start_main_patch = coff_emit_exe_start_stub(&text, import_patches, &import_patch_len);
  while (text.len % 16 != 0) append_u8(&text, 0x90);
  for (size_t i = 0; i < program->function_len; i++) {
    while (text.len % 16 != 0) append_u8(&text, 0x90);
    offsets[i] = text.len;
    if (!coff_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      free(ctx.world_write_patches);
      free(ctx.rodata_patches);
      free(ctx.call_patches);
      free(offsets);
      zbuf_free(&rdata);
      zbuf_free(&text);
      return false;
    }
  }
  size_t world_write_offset = 0;
  if (ctx.world_write_patch_len > 0) {
    while (text.len % 16 != 0) append_u8(&text, 0x90);
    world_write_offset = coff_emit_exe_world_write(&text, import_patches, &import_patch_len);
  }

  coff_patch_rel32(&text, start_main_patch, offsets[main_index]);
  for (size_t i = 0; i < ctx.call_patch_len; i++) {
    const CoffCallPatch *patch = &ctx.call_patches[i];
    coff_patch_rel32(&text, patch->patch_offset, offsets[patch->callee_index]);
  }
  for (size_t i = 0; i < ctx.world_write_patch_len; i++) {
    coff_patch_rel32(&text, ctx.world_write_patches[i].patch_offset, world_write_offset);
  }

  const uint64_t image_base = 0x140000000ull;
  const uint32_t section_alignment = 0x1000;
  const uint32_t file_alignment = 0x200;
  const uint32_t dos_header_size = 0x80;
  const uint16_t section_count = 2;
  const uint32_t optional_header_size = 240;
  const uint32_t pe_header_offset = dos_header_size;
  const uint32_t headers_unaligned = pe_header_offset + 4 + 20 + optional_header_size + section_count * 40;
  const uint32_t headers_size = (uint32_t)coff_align(headers_unaligned, file_alignment);
  const uint32_t text_rva = section_alignment;
  const uint32_t text_raw_offset = headers_size;
  const uint32_t text_raw_size = (uint32_t)coff_align(text.len, file_alignment);
  const uint32_t rdata_rva = (uint32_t)coff_align(text_rva + text.len, section_alignment);
  CoffImportLayout imports = {0};
  coff_append_import_table(&rdata, rdata_rva, &imports);
  const uint32_t rdata_raw_offset = text_raw_offset + text_raw_size;
  const uint32_t rdata_raw_size = (uint32_t)coff_align(rdata.len, file_alignment);
  const uint32_t size_of_image = (uint32_t)coff_align(rdata_rva + rdata.len, section_alignment);

  for (size_t i = 0; i < ctx.rodata_patch_len; i++) {
    const CoffRodataPatch *patch = &ctx.rodata_patches[i];
    uint64_t addr = image_base + rdata_rva + (patch->data_offset - rodata_base_offset);
    patch_u64le(&text, patch->patch_offset, addr);
  }
  for (size_t i = 0; i < import_patch_len; i++) {
    const CoffImportPatch *patch = &import_patches[i];
    uint64_t target = image_base + rdata_rva + imports.iat_offsets[patch->import_index];
    coff_patch_rel32_to_va(&text, patch->patch_offset, image_base + text_rva, target);
  }

  zbuf_init(out);
  append_u8(out, 'M');
  append_u8(out, 'Z');
  append_zeros(out, 0x3a);
  append_u32le(out, pe_header_offset);
  coff_pad_to(out, dos_header_size);

  append_u8(out, 'P');
  append_u8(out, 'E');
  append_u8(out, 0);
  append_u8(out, 0);
  append_u16le(out, 0x8664);           // IMAGE_FILE_MACHINE_AMD64
  append_u16le(out, section_count);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u16le(out, optional_header_size);
  append_u16le(out, 0x0022);           // executable | large-address-aware

  append_u16le(out, 0x20b);            // PE32+
  append_u8(out, 0);
  append_u8(out, 1);
  append_u32le(out, text_raw_size);
  append_u32le(out, rdata_raw_size);
  append_u32le(out, 0);
  append_u32le(out, text_rva);
  append_u32le(out, text_rva);
  append_u64le(out, image_base);
  append_u32le(out, section_alignment);
  append_u32le(out, file_alignment);
  append_u16le(out, 6);
  append_u16le(out, 0);
  append_u16le(out, 0);
  append_u16le(out, 0);
  append_u16le(out, 6);
  append_u16le(out, 0);
  append_u32le(out, 0);
  append_u32le(out, size_of_image);
  append_u32le(out, headers_size);
  append_u32le(out, 0);
  append_u16le(out, 3);                // Windows CUI subsystem
  append_u16le(out, 0x0100);           // NX compatible; fixed image has no reloc section
  append_u64le(out, 0x100000);
  append_u64le(out, 0x1000);
  append_u64le(out, 0x100000);
  append_u64le(out, 0x1000);
  append_u32le(out, 0);
  append_u32le(out, 16);
  append_u32le(out, 0);
  append_u32le(out, 0);                // export table
  append_u32le(out, imports.import_directory_rva);
  append_u32le(out, imports.import_directory_size);
  append_u32le(out, 0);
  append_u32le(out, 0);                // resource
  append_u32le(out, 0);
  append_u32le(out, 0);                // exception
  append_u32le(out, 0);
  append_u32le(out, 0);                // cert
  append_u32le(out, 0);
  append_u32le(out, 0);                // reloc
  append_u32le(out, 0);
  append_u32le(out, 0);                // debug
  append_u32le(out, 0);
  append_u32le(out, 0);                // architecture
  append_u32le(out, 0);
  append_u32le(out, 0);                // global ptr
  append_u32le(out, 0);
  append_u32le(out, 0);                // TLS
  append_u32le(out, 0);
  append_u32le(out, 0);                // load config
  append_u32le(out, 0);
  append_u32le(out, 0);                // bound import
  append_u32le(out, imports.iat_rva);
  append_u32le(out, imports.iat_size);
  append_u32le(out, 0);
  append_u32le(out, 0);                // delay import
  append_u32le(out, 0);
  append_u32le(out, 0);                // CLR
  append_u32le(out, 0);
  append_u32le(out, 0);                // reserved

  append_coff_name(out, ".text");
  append_u32le(out, (uint32_t)text.len);
  append_u32le(out, text_rva);
  append_u32le(out, text_raw_size);
  append_u32le(out, text_raw_offset);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u16le(out, 0);
  append_u16le(out, 0);
  append_u32le(out, 0x60000020u);

  append_coff_name(out, ".rdata");
  append_u32le(out, (uint32_t)rdata.len);
  append_u32le(out, rdata_rva);
  append_u32le(out, rdata_raw_size);
  append_u32le(out, rdata_raw_offset);
  append_u32le(out, 0);
  append_u32le(out, 0);
  append_u16le(out, 0);
  append_u16le(out, 0);
  append_u32le(out, 0x40000040u);

  coff_pad_to(out, headers_size);
  if (text.data) append_bytes(out, text.data, text.len);
  coff_pad_to(out, text_raw_offset + text_raw_size);
  if (rdata.data) append_bytes(out, rdata.data, rdata.len);
  coff_pad_to(out, rdata_raw_offset + rdata_raw_size);

  free(ctx.world_write_patches);
  free(ctx.rodata_patches);
  free(ctx.call_patches);
  free(offsets);
  zbuf_free(&rdata);
  zbuf_free(&text);
  return true;
}
