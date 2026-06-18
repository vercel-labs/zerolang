#include "zero.h"
#include "aarch64_emit.h"
#include "direct_emit.h"
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
// Minimum constant-fill run length that justifies a fill loop over unrolled
// per-element stores. Below this the unrolled form is smaller and faster.
#define MACHO_FILL_RUN_MIN 8u

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

static void macho_emit_load_field_indirect(ZBuf *text, unsigned reg, unsigned base, unsigned field_offset, IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_aarch64_emit_load_b_imm(text, reg, base, field_offset);
  } else if (type == IR_TYPE_U16) {
    z_aarch64_emit_load_h_imm(text, reg, base, field_offset);
  } else if (macho_type_is_scalar64(type)) {
    z_aarch64_emit_load_x_imm(text, reg, base, field_offset);
  } else {
    z_aarch64_emit_load_w_imm(text, reg, base, field_offset);
  }
}

static void macho_emit_store_field_indirect(ZBuf *text, unsigned reg, unsigned base, unsigned field_offset, IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_aarch64_emit_store_b_imm(text, reg, base, field_offset);
  } else if (type == IR_TYPE_U16) {
    z_aarch64_emit_store_h_imm(text, reg, base, field_offset);
  } else if (macho_type_is_scalar64(type)) {
    z_aarch64_emit_store_x_imm(text, reg, base, field_offset);
  } else {
    z_aarch64_emit_store_w_imm(text, reg, base, field_offset);
  }
}

static bool macho_emit_field_load_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field load record is out of range", value->line, value->column, "invalid record local");
  const IrLocal *record_local = &fun->locals[value->local_index];
  if (record_local->is_record_ref) {
    unsigned base = reg == 10 ? 11 : 10;
    macho_emit_load_local_x(text, fun, base, value->local_index, 0, frame_size);
    macho_emit_load_field_indirect(text, reg, base, value->field_offset, value->type);
    return true;
  }
  if (!record_local->is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field load requires record local", value->line, value->column, "non-record local");
  macho_emit_load_field(text, fun, reg, value->local_index, value->field_offset, value->type, frame_size);
  return true;
}

static bool macho_emit_record_addr_to_reg(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O record address local is out of range", value->line, value->column, "invalid record local");
  if (!fun->locals[value->local_index].is_record) return macho_diag_at(diag, "direct AArch64 Mach-O record address requires record local", value->line, value->column, "non-record local");
  z_aarch64_emit_add_x_sp_imm(text, reg, macho_local_slot_offset(fun, value->local_index, 0, frame_size));
  return true;
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

static bool macho_emit_trap(ZBuf *text, MachOEmitContext *ctx, ZDiag *diag, ZDirectTrapKind kind);

static bool macho_emit_u32_bounds_check(ZBuf *text, unsigned index_reg, unsigned len_reg, MachOEmitContext *ctx, ZDiag *diag) {
  z_aarch64_emit_cmp_w(text, index_reg, len_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  if (!macho_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_INDEX_BOUNDS)) return false;
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
}

static bool macho_emit_u32_upper_bound_check(ZBuf *text, unsigned value_reg, unsigned max_reg, MachOEmitContext *ctx, ZDiag *diag) {
  z_aarch64_emit_cmp_w(text, value_reg, max_reg);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 9);
  if (!macho_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_VALUE_BOUNDS)) return false;
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
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
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE: return MACHO_RUNTIME_HTTP_WRITE_JSON_RESPONSE;
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: return MACHO_RUNTIME_HTTP_REQUEST_METHOD_NAME;
    case IR_VALUE_HTTP_REQUEST_PATH: return MACHO_RUNTIME_HTTP_REQUEST_PATH;
    case IR_VALUE_HTTP_REQUEST_MATCHES: return MACHO_RUNTIME_HTTP_REQUEST_MATCHES;
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN: return MACHO_RUNTIME_HTTP_REQUEST_BODY_WITHIN;
    case IR_VALUE_JSON_FIELD: return MACHO_RUNTIME_JSON_FIELD;
    case IR_VALUE_JSON_LOOKUP_SCALAR: return MACHO_RUNTIME_JSON_LOOKUP_SCALAR;
    case IR_VALUE_JSON_STRING_DECODE: return MACHO_RUNTIME_JSON_STRING_DECODE;
    case IR_VALUE_JSON_STRING_FIELD: return MACHO_RUNTIME_JSON_STRING_FIELD;
    case IR_VALUE_STR_CONTAINS: return MACHO_RUNTIME_STR_CONTAINS;
    case IR_VALUE_FS_READ_BYTES_PATH: return MACHO_RUNTIME_FS_READ_BYTES;
    case IR_VALUE_FS_READ_BYTES_AT_PATH: return MACHO_RUNTIME_FS_READ_BYTES_AT;
    case IR_VALUE_FS_WRITE_BYTES_PATH: return MACHO_RUNTIME_FS_WRITE_BYTES;
    case IR_VALUE_FS_APPEND_BYTES_PATH: return MACHO_RUNTIME_FS_APPEND_BYTES;
    case IR_VALUE_FS_ATOMIC_WRITE: return MACHO_RUNTIME_FS_ATOMIC_WRITE;
    case IR_VALUE_PARSE_RUNTIME: return MACHO_RUNTIME_PARSE_OP;
    case IR_VALUE_TIME_RUNTIME: return MACHO_RUNTIME_TIME_OP;
    case IR_VALUE_TERM_RUNTIME: return MACHO_RUNTIME_TERM_OP;
    case IR_VALUE_MATH_RUNTIME: return MACHO_RUNTIME_MATH_OP;
    case IR_VALUE_SEARCH_RUNTIME: return MACHO_RUNTIME_SEARCH_OP;
    case IR_VALUE_PROC_CAPTURE: return MACHO_RUNTIME_PROC_CAPTURE;
    case IR_VALUE_PROC_CAPTURE_FILES: return MACHO_RUNTIME_PROC_CAPTURE_FILES;
    case IR_VALUE_PROC_CHILD_SPAWN: return MACHO_RUNTIME_PROC_SPAWN_CHILD;
    case IR_VALUE_PROC_CHILD_OP: return MACHO_RUNTIME_PROC_CHILD_OP;
    case IR_VALUE_PROC_CHILD_IO: return MACHO_RUNTIME_PROC_CHILD_IO;
    case IR_VALUE_PROC_PTY_RESIZE: return MACHO_RUNTIME_PTY_RESIZE;
    case IR_VALUE_ARGS_FIND:
    case IR_VALUE_ARGS_CONTAINS:
    case IR_VALUE_ARGS_VALUE_AFTER:
    case IR_VALUE_ARGS_VALUE_AFTER_OR:
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32: return MACHO_RUNTIME_ARGS_FIND;
    case IR_VALUE_PARSE_I32: return MACHO_RUNTIME_PARSE_I32;
    case IR_VALUE_PARSE_U32:
    case IR_VALUE_ARGS_PARSE_U32: return MACHO_RUNTIME_PARSE_U32;
    case IR_VALUE_FMT_BOOL: return MACHO_RUNTIME_FMT_BOOL;
    case IR_VALUE_FMT_HEX_U32: return MACHO_RUNTIME_FMT_HEX_LOWER_U32;
    case IR_VALUE_FMT_I32: return MACHO_RUNTIME_FMT_I32;
    case IR_VALUE_FMT_U32: return MACHO_RUNTIME_FMT_U32;
    case IR_VALUE_FMT_USIZE: return MACHO_RUNTIME_FMT_USIZE;
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

static bool macho_emit_trap(ZBuf *text, MachOEmitContext *ctx, ZDiag *diag, ZDirectTrapKind kind) {
  /* Cold path: branch to the shared per-binary trap stub that prints a diagnostic. */
  if (ctx && ctx->trap_messages.lens[kind] > 0) {
    size_t patch = z_aarch64_emit_b_placeholder(text);
    if (z_direct_trap_branches_record(&ctx->trap_branches[kind], patch)) return true;
    return macho_diag_at(diag, "direct AArch64 Mach-O backend ran out of memory while recording a trap branch", 1, 1, "allocation failed");
  }
  z_aarch64_emit_brk(text);
  return true;
}

static bool macho_emit_trap_stubs(ZBuf *text, MachOEmitContext *ctx, ZDiag *diag) {
  for (unsigned kind = 0; ctx && kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    ZDirectTrapBranchList *branches = &ctx->trap_branches[kind];
    if (branches->len == 0) continue;
    macho_pad_to(text, macho_align(text->len, 4));
    size_t stub_offset = text->len;
    if (!macho_emit_rodata_ptr_literal(text, 1, ctx->trap_messages.offsets[kind], ctx, NULL, diag)) return false;
    z_aarch64_emit_movz_w(text, 2, ctx->trap_messages.lens[kind]);
    z_aarch64_emit_movz_w(text, 0, 2u);
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    if (!z_macho_record_instr_runtime_patch(ctx, MACHO_RUNTIME_WORLD_WRITE, patch, NULL, diag)) return false;
    z_aarch64_emit_brk(text);
    for (size_t i = 0; i < branches->len; i++) z_aarch64_patch_branch26(text, branches->items[i], stub_offset);
    branches->len = 0;
  }
  return true;
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

static void macho_emit_parse_u32_result_to_maybe_regs(ZBuf *text);

static bool macho_emit_json_diagnostic_call_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_DIAGNOSTIC, patch, value, diag);
}

static bool macho_emit_json_field_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O JSON field helper requires bytes and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON field input");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_FIELD, patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool macho_emit_json_lookup_scalar_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O JSON scalar helper requires bytes and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON scalar input");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 4, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_LOOKUP_SCALAR, patch, value, diag)) return false;
  macho_emit_parse_u32_result_to_maybe_regs(text);
  return true;
}

static bool macho_emit_json_string_decode_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O JSON string decode helper requires a buffer and string", value ? value->line : 1, value ? value->column : 1, "invalid JSON string decode input");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  MachORuntimeHelper helper = value->kind == IR_VALUE_JSON_WRITE_STRING ? MACHO_RUNTIME_JSON_WRITE_STRING : MACHO_RUNTIME_JSON_STRING_DECODE;
  if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 2, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool macho_emit_json_string_field_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) return macho_diag_at(diag, "direct AArch64 Mach-O JSON string field helper requires a buffer, bytes, and key", value ? value->line : 1, value ? value->column : 1, "invalid JSON string field input");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_store_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->index, 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_JSON_STRING_FIELD, patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 4, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 4, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static MachORuntimeHelper macho_json_write_runtime_helper(IrJsonWriteOp op) {
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW: return MACHO_RUNTIME_JSON_WRITE_FIELD_RAW;
    case IR_JSON_WRITE_FIELD_STRING: return MACHO_RUNTIME_JSON_WRITE_FIELD_STRING;
    case IR_JSON_WRITE_FIELD_U32: return MACHO_RUNTIME_JSON_WRITE_FIELD_U32;
    case IR_JSON_WRITE_FIELD_BOOL: return MACHO_RUNTIME_JSON_WRITE_FIELD_BOOL;
    case IR_JSON_WRITE_OBJECT1_STRING: return MACHO_RUNTIME_JSON_WRITE_OBJECT1_STRING;
    case IR_JSON_WRITE_OBJECT1_U32: return MACHO_RUNTIME_JSON_WRITE_OBJECT1_U32;
    case IR_JSON_WRITE_OBJECT1_BOOL: return MACHO_RUNTIME_JSON_WRITE_OBJECT1_BOOL;
    case IR_JSON_WRITE_OBJECT2_FIELDS: return MACHO_RUNTIME_JSON_WRITE_OBJECT2_FIELDS;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD: return MACHO_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD: return MACHO_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD;
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD: return MACHO_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD;
    case IR_JSON_WRITE_ARRAY2_STRINGS: return MACHO_RUNTIME_JSON_WRITE_ARRAY2_STRINGS;
    case IR_JSON_WRITE_ARRAY2_U32: return MACHO_RUNTIME_JSON_WRITE_ARRAY2_U32;
    case IR_JSON_WRITE_ARRAY2_BOOLS: return MACHO_RUNTIME_JSON_WRITE_ARRAY2_BOOLS;
  }
  return MACHO_RUNTIME_HELPER_COUNT;
}

static bool macho_emit_json_writer_store_view_arg(ZBuf *text, const IrFunction *fun, const IrValue *value, size_t arg_index, unsigned pair_slot, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || arg_index >= value->arg_len) return macho_diag_at(diag, "direct AArch64 Mach-O JSON writer is missing a span argument", value ? value->line : 1, value ? value->column : 1, "missing JSON writer argument");
  if (!macho_emit_byte_view_pair_at(text, fun, value->args[arg_index], 8, 9, frame_size, scratch_slot + pair_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, IR_TYPE_U64, scratch_slot + pair_slot, value->args[arg_index], diag)) return false;
  return macho_emit_store_scratch(text, 9, IR_TYPE_U32, scratch_slot + pair_slot + 1, value->args[arg_index], diag);
}

static bool macho_emit_json_writer_store_scalar_arg(ZBuf *text, const IrFunction *fun, const IrValue *value, size_t arg_index, unsigned slot, IrTypeKind type, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || arg_index >= value->arg_len) return macho_diag_at(diag, "direct AArch64 Mach-O JSON writer is missing a scalar argument", value ? value->line : 1, value ? value->column : 1, "missing JSON writer argument");
  if (!macho_emit_value_to_reg_at(text, fun, value->args[arg_index], 8, frame_size, scratch_slot + slot, ctx, diag)) return false;
  return macho_emit_store_scratch(text, 8, type, scratch_slot + slot, value->args[arg_index], diag);
}

static bool macho_emit_json_writer_load_view_arg(ZBuf *text, const IrValue *value, size_t arg_index, unsigned pair_slot, unsigned ptr_reg, unsigned len_reg, unsigned scratch_slot, ZDiag *diag) {
  if (!macho_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, scratch_slot + pair_slot, value->args[arg_index], diag)) return false;
  return macho_emit_load_scratch(text, len_reg, IR_TYPE_U32, scratch_slot + pair_slot + 1, value->args[arg_index], diag);
}

static bool macho_emit_json_write_runtime_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 3) return macho_diag_at(diag, "direct AArch64 Mach-O JSON writer requires arguments", value ? value->line : 1, value ? value->column : 1, "invalid JSON writer");
  IrJsonWriteOp op = (IrJsonWriteOp)value->int_value;
  MachORuntimeHelper helper = macho_json_write_runtime_helper(op);
  if (helper == MACHO_RUNTIME_HELPER_COUNT) return macho_diag_at(diag, "direct AArch64 Mach-O JSON writer op is invalid", value->line, value->column, "invalid JSON writer");
  if (!macho_emit_json_writer_store_view_arg(text, fun, value, 0, 0, frame_size, scratch_slot, ctx, diag)) return false;
  switch (op) {
    case IR_JSON_WRITE_FIELD_RAW:
    case IR_JSON_WRITE_FIELD_STRING:
    case IR_JSON_WRITE_OBJECT1_STRING:
    case IR_JSON_WRITE_OBJECT2_FIELDS:
    case IR_JSON_WRITE_ARRAY2_STRINGS:
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 2, 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 2, 4, 4, 5, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_FIELD_U32:
    case IR_JSON_WRITE_FIELD_BOOL:
    case IR_JSON_WRITE_OBJECT1_U32:
    case IR_JSON_WRITE_OBJECT1_BOOL:
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_scalar_arg(text, fun, value, 2, 4, op == IR_JSON_WRITE_FIELD_BOOL || op == IR_JSON_WRITE_OBJECT1_BOOL ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!macho_emit_load_scratch(text, 4, op == IR_JSON_WRITE_FIELD_BOOL || op == IR_JSON_WRITE_OBJECT1_BOOL ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      break;
    case IR_JSON_WRITE_OBJECT2_STRING_FIELD:
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 2, 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 3, 6, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 2, 4, 4, 5, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 3, 6, 6, 7, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_OBJECT2_U32_FIELD:
    case IR_JSON_WRITE_OBJECT2_BOOL_FIELD:
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 1, 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_scalar_arg(text, fun, value, 2, 4, op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_view_arg(text, fun, value, 3, 5, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 1, 2, 2, 3, scratch_slot, diag)) return false;
      if (!macho_emit_load_scratch(text, 4, op == IR_JSON_WRITE_OBJECT2_BOOL_FIELD ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 3, 5, 5, 6, scratch_slot, diag)) return false;
      break;
    case IR_JSON_WRITE_ARRAY2_U32:
    case IR_JSON_WRITE_ARRAY2_BOOLS:
      if (!macho_emit_json_writer_store_scalar_arg(text, fun, value, 1, 2, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_store_scalar_arg(text, fun, value, 2, 3, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_json_writer_load_view_arg(text, value, 0, 0, 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_scratch(text, 2, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      if (!macho_emit_load_scratch(text, 3, op == IR_JSON_WRITE_ARRAY2_BOOLS ? IR_TYPE_BOOL : IR_TYPE_U32, scratch_slot + 3, value->args[2], diag)) return false;
      break;
  }
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 7, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 7, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool macho_emit_args_get_or_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view || !view->left || !view->right) return macho_diag_at(diag, "direct AArch64 Mach-O std.args.getOr requires an index and fallback", view ? view->line : 1, view ? view->column : 1, "missing getOr input");
  if (!macho_emit_value_to_reg_at(text, fun, view->left, 10, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_cmp_w(text, 10, 20);
  size_t in_range = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  if (!macho_emit_byte_view_pair_at(text, fun, view->right, ptr_reg, len_reg, frame_size, scratch_slot + 1, ctx, diag)) return false;
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, in_range, text->len);

  z_aarch64_emit_add_x_reg_lsl(text, 12, 21, 10, 3);
  z_aarch64_emit_load_x_imm(text, 12, 12, 0);
  z_aarch64_emit_movz_w(text, 10, 0);
  size_t loop_start = text->len;
  z_aarch64_emit_add_x_reg(text, 13, 12, 10);
  z_aarch64_emit_load_b_imm(text, 14, 13, 0);
  size_t done_len = z_aarch64_emit_cbz_w_placeholder(text, 14);
  z_aarch64_emit_add_w_imm(text, 10, 10, 1);
  size_t loop_back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, loop_back, loop_start);
  z_aarch64_patch_cond19(text, done_len, text->len);
  macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 12, 10);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_args_find_call_at(ZBuf *text, const IrFunction *fun, const IrValue *site, const IrValue *name, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!name) return macho_diag_at(diag, "direct AArch64 Mach-O args find helper requires a name", site ? site->line : 1, site ? site->column : 1, "missing args name");
  if (!macho_emit_byte_view_pair_at(text, fun, name, 2, 3, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_mov_x(text, 0, 20);
  z_aarch64_emit_mov_x(text, 1, 21);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_ARGS_FIND, patch, site, diag);
}

static bool macho_emit_args_find_next_index_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag, size_t *none_patch, size_t *found_patch) {
  if (!value || !value->left) return macho_diag_at(diag, "direct AArch64 Mach-O args option helper requires a name", value ? value->line : 1, value ? value->column : 1, "missing option name");
  if (!macho_emit_args_find_call_at(text, fun, value, value->left, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_mov_w(text, 8, 0);
  z_aarch64_emit_lsr_x_imm(text, 0, 0, 32);
  *none_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
  z_aarch64_emit_add_w_imm(text, 8, 8, 1);
  z_aarch64_emit_cmp_w(text, 8, 20);
  *found_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  return true;
}

static void macho_emit_args_value_after_load_argv_value(ZBuf *text) {
  z_aarch64_emit_add_x_reg_lsl(text, 12, 21, 8, 3);
  z_aarch64_emit_load_x_imm(text, 12, 12, 0);
}

static void macho_emit_strlen_x12_to_w10(ZBuf *text) {
  z_aarch64_emit_movz_w(text, 10, 0);
  size_t loop_start = text->len;
  z_aarch64_emit_add_x_reg(text, 13, 12, 10);
  z_aarch64_emit_load_b_imm(text, 14, 13, 0);
  size_t done_len = z_aarch64_emit_cbz_w_placeholder(text, 14);
  z_aarch64_emit_add_w_imm(text, 10, 10, 1);
  size_t loop_back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, loop_back, loop_start);
  z_aarch64_patch_cond19(text, done_len, text->len);
}

static bool macho_emit_args_value_after_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O args option fallback helper requires a fallback", value ? value->line : 1, value ? value->column : 1, "missing fallback");
  size_t none = 0;
  size_t found = 0;
  if (!macho_emit_args_find_next_index_at(text, fun, value, frame_size, scratch_slot, ctx, diag, &none, &found)) return false;
  z_aarch64_patch_cond19(text, none, text->len);
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, ptr_reg, len_reg, frame_size, scratch_slot + 1, ctx, diag)) return false;
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, found, text->len);
  macho_emit_args_value_after_load_argv_value(text);
  macho_emit_strlen_x12_to_w10(text);
  macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 12, 10);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_byte_view_len_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  unsigned const_len = 0;
  if (view->kind == IR_VALUE_BYTE_SLICE && macho_byte_view_const_len(view, &const_len)) {
    if (const_len > Z_DIRECT_FRAME_LOCAL_LIMIT_BYTES) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length is too large for the current MVP", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, const_len);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (view->data_len > Z_DIRECT_FRAME_LOCAL_LIMIT_BYTES) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length is too large for the current MVP", view->line, view->column, "large byte view");
    z_aarch64_emit_movz_w(text, reg, view->data_len);
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_VEC_BYTES && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_VEC) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_w(text, fun, reg, view->local_index, 16, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *record_local = &fun->locals[view->local_index];
    if (record_local->is_record_ref) {
      unsigned base = reg == 10 ? 11 : 10;
      macho_emit_load_local_x(text, fun, base, view->local_index, 0, frame_size);
      macho_emit_load_field_indirect(text, reg, base, view->field_offset + 8, IR_TYPE_U32);
      return true;
    }
    if (!record_local->is_record) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view field load requires record local", view->line, view->column, "non-record local");
    macho_emit_load_field(text, fun, reg, view->local_index, view->field_offset + 8, IR_TYPE_U32, frame_size);
    return true;
  }
  if ((view->kind == IR_VALUE_CALL || view->kind == IR_VALUE_STR_RUNTIME) && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 1) z_aarch64_emit_mov_w(text, reg, 1);
    return true;
  }
  if (view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    unsigned ptr_tmp = reg == 11 ? 12 : 11;
    return macho_emit_byte_view_pair_at(text, fun, view, ptr_tmp, reg, frame_size, scratch_slot, ctx, diag);
  }
  if (view->kind == IR_VALUE_ARGS_GET_OR || view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR) {
    if (view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR) return macho_emit_args_value_after_pair_at(text, fun, view, 11, reg, frame_size, scratch_slot, ctx, diag);
    return macho_emit_args_get_or_pair_at(text, fun, view, 11, reg, frame_size, scratch_slot, ctx, diag);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned ptr_tmp = reg == 11 ? 12 : 11;
    return macho_emit_byte_view_pair_at(text, fun, view, ptr_tmp, reg, frame_size, scratch_slot, ctx, diag);
  }
  (void)ctx;
  return macho_diag_at(diag, "direct AArch64 Mach-O byte-view length currently requires a literal, constant slice, or byte-view local", view->line, view->column, "unsupported byte view length");
}

static bool macho_emit_byte_view_remaining_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->index) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.io.remaining requires a byte view and offset", value ? value->line : 1, value ? value->column : 1, "missing remaining operand");
  }
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->index->type, scratch_slot, value->index, diag)) return false;
  if (!macho_emit_byte_view_len_at(text, fun, value->left, 9, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, value->index->type, scratch_slot, value->index, diag)) return false;
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t keep = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t done = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, keep, text->len);
  z_aarch64_emit_sub_w_reg(text, reg, 9, 8);
  z_aarch64_patch_branch26(text, done, text->len);
  return true;
}

static bool macho_emit_byte_view_ptr_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view) return macho_diag_at(diag, "direct AArch64 Mach-O byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 0, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_VEC_BYTES && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_VEC) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 0, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    macho_emit_load_local_x(text, fun, reg, view->local_index, 8, frame_size);
    return true;
  }
  if (view->kind == IR_VALUE_FIELD_LOAD && view->type == IR_TYPE_BYTE_VIEW && view->local_index < fun->local_len) {
    const IrLocal *record_local = &fun->locals[view->local_index];
    if (record_local->is_record_ref) {
      unsigned base = reg == 10 ? 11 : 10;
      macho_emit_load_local_x(text, fun, base, view->local_index, 0, frame_size);
      macho_emit_load_field_indirect(text, reg, base, view->field_offset, IR_TYPE_U64);
      return true;
    }
    if (!record_local->is_record) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view field load requires record local", view->line, view->column, "non-record local");
    macho_emit_load_field(text, fun, reg, view->local_index, view->field_offset, IR_TYPE_U64, frame_size);
    return true;
  }
  if ((view->kind == IR_VALUE_CALL || view->kind == IR_VALUE_STR_RUNTIME) && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
    return true;
  }
  if (view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    unsigned len_tmp = reg == 10 ? 11 : 10;
    return macho_emit_byte_view_pair_at(text, fun, view, reg, len_tmp, frame_size, scratch_slot, ctx, diag);
  }
  if (view->kind == IR_VALUE_ARGS_GET_OR || view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR) {
    if (view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR) return macho_emit_args_value_after_pair_at(text, fun, view, reg, 10, frame_size, scratch_slot, ctx, diag);
    return macho_emit_args_get_or_pair_at(text, fun, view, reg, 10, frame_size, scratch_slot, ctx, diag);
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (local->is_record_ref) {
      macho_emit_load_local_x(text, fun, reg, view->array_index, 0, frame_size);
      if (view->field_offset > 0) z_aarch64_emit_add_x_imm(text, reg, reg, view->field_offset);
      return true;
    }
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

static bool macho_emit_json_error_label_arm(ZBuf *text, const IrValue *view, unsigned index, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view || index >= view->arg_len || !view->args[index] || view->args[index]->kind != IR_VALUE_STRING_LITERAL) {
    return macho_diag_at(diag, "direct AArch64 Mach-O JSON error label requires string literal arms", view ? view->line : 1, view ? view->column : 1, "invalid JSON error label");
  }
  const IrValue *label = view->args[index];
  if (!macho_emit_rodata_ptr_literal(text, 0, label->data_offset, ctx, label, diag)) return false;
  z_aarch64_emit_movz_w(text, 1, label->data_len);
  return true;
}

static bool macho_emit_json_error_label_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!view || !view->left) return macho_diag_at(diag, "direct AArch64 Mach-O JSON error label requires a status code", view ? view->line : 1, view ? view->column : 1, "missing JSON status");
  if (view->arg_len != 4) return macho_diag_at(diag, "direct AArch64 Mach-O JSON error label requires four labels", view->line, view->column, "invalid JSON error label");
  if (!macho_emit_value_to_reg_at(text, fun, view->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, 0);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code0 = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, 9, 1);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code1 = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, 9, 2);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t code2 = z_aarch64_emit_b_cond_placeholder(text, 0);
  if (!macho_emit_json_error_label_arm(text, view, 3, ctx, diag)) return false;
  size_t done3 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code0, text->len);
  if (!macho_emit_json_error_label_arm(text, view, 0, ctx, diag)) return false;
  size_t done0 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code1, text->len);
  if (!macho_emit_json_error_label_arm(text, view, 1, ctx, diag)) return false;
  size_t done1 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, code2, text->len);
  if (!macho_emit_json_error_label_arm(text, view, 2, ctx, diag)) return false;
  size_t done2 = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, done3, text->len);
  z_aarch64_patch_branch26(text, done0, text->len);
  z_aarch64_patch_branch26(text, done1, text->len);
  z_aarch64_patch_branch26(text, done2, text->len);
  macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
  return true;
}

static bool macho_emit_byte_view_pair_at(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (ptr_reg == len_reg) return macho_diag_at(diag, "direct AArch64 Mach-O byte-view pair requires distinct destination registers", view ? view->line : 1, view ? view->column : 1, "invalid byte-view registers");
  if (view && (view->kind == IR_VALUE_CALL || view->kind == IR_VALUE_STR_RUNTIME) && view->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_value_to_reg_at(text, fun, view, 0, frame_size, scratch_slot, ctx, diag)) return false;
    macho_emit_move_byte_view_pair(text, ptr_reg, len_reg, 0, 1);
    return true;
  }
  if (view && view->kind == IR_VALUE_JSON_ERROR_LABEL && view->type == IR_TYPE_BYTE_VIEW) {
    return macho_emit_json_error_label_pair_at(text, fun, view, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
  }
  if (view && (view->kind == IR_VALUE_ARGS_GET_OR || view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR)) {
    if (view->kind == IR_VALUE_ARGS_VALUE_AFTER_OR) return macho_emit_args_value_after_pair_at(text, fun, view, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
    return macho_emit_args_get_or_pair_at(text, fun, view, ptr_reg, len_reg, frame_size, scratch_slot, ctx, diag);
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
      if (!macho_emit_u32_upper_bound_check(text, 8, 9, ctx, diag)) return false;
      if (!macho_emit_u32_upper_bound_check(text, 9, 12, ctx, diag)) return false;
      macho_emit_binary_reg(text, IR_BIN_SUB, 10, 9, 8, false);
    } else {
      if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, view->left, diag)) return false;
      if (!macho_emit_load_scratch(text, 8, view->index ? view->index->type : IR_TYPE_U32, scratch_slot + 2, view->index, diag)) return false;
      if (!macho_emit_u32_upper_bound_check(text, 8, 10, ctx, diag)) return false;
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
  const IrFunction *callee = ctx && ctx->program && !value->external_call && value->callee_index < ctx->program->function_len ? &ctx->program->functions[value->callee_index] : NULL;
  const IrExternalFunction *external = ctx && ctx->program && value->external_call && value->external_index < ctx->program->external_function_len ? &ctx->program->external_functions[value->external_index] : NULL;
  if (!callee && !external) return macho_diag_at(diag, "direct AArch64 Mach-O call target is unavailable", value->line, value->column, "invalid callee");
  size_t param_count = external ? external->param_len : callee->param_count;
  unsigned abi_slots = 0;
  for (size_t i = 0; i < value->arg_len; i++) {
    if (i >= param_count) return macho_diag_at(diag, "direct AArch64 Mach-O call parameter metadata is unavailable", value->line, value->column, "invalid callee parameter");
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    unsigned slots = param_type == IR_TYPE_BYTE_VIEW ? 2u : 1u;
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
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    if (param_type == IR_TYPE_BYTE_VIEW) {
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
    IrTypeKind param_type = external ? external->param_types[i] : callee->locals[i].type;
    if (param_type == IR_TYPE_BYTE_VIEW) {
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
  if (!z_macho_record_call_patch(ctx, patch, value->external_call ? 0 : value->callee_index, value, diag)) return false;
  if (value->external_call) macho_emit_cast_normalize_reg(text, 0, value->type, value->type);
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

static bool macho_emit_rand_bounded_from_w8(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, bool add_low) {
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 8);

  z_aarch64_emit_movz_w(text, 9, 0);
  z_aarch64_emit_sub_w_reg(text, 9, 9, 8);
  z_aarch64_emit_div_reg(text, 10, 9, 8, true, false);
  z_aarch64_emit_msub_reg(text, 9, 10, 8, 9, false);

  size_t loop = text->len;
  macho_emit_load_local_w(text, fun, 11, value->local_index, 0, frame_size);
  z_aarch64_emit_movz_w(text, 12, 1664525u);
  z_aarch64_emit_mul_w_reg(text, 11, 11, 12);
  z_aarch64_emit_movz_w(text, 12, 1013904223u);
  z_aarch64_emit_add_w_reg(text, 11, 11, 12);
  macho_emit_store_local_w(text, fun, 11, value->local_index, 0, frame_size);
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

static bool macho_emit_rand_maybe_to_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O std.rand bounded local is out of range", value ? value->line : 1, value ? value->column : 1, "invalid RandSource");
  if (value->kind == IR_VALUE_RAND_NEXT_BELOW) {
    if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
    return macho_emit_rand_bounded_from_w8(text, fun, value, frame_size, false);
  }
  if (value->kind == IR_VALUE_RAND_RANGE_U32) {
    if (!macho_emit_value_to_reg_at(text, fun, value->left, 13, frame_size, scratch_slot, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 13, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
    if (!macho_emit_value_to_reg_at(text, fun, value->right, 8, frame_size, scratch_slot + 1, ctx, diag)) return false;
    if (!macho_emit_load_scratch(text, 13, IR_TYPE_U32, scratch_slot, value->left, diag)) return false;
    z_aarch64_emit_cmp_w(text, 8, 13);
    size_t empty = z_aarch64_emit_b_cond_placeholder(text, 9);
    z_aarch64_emit_sub_w_reg(text, 8, 8, 13);
    bool ok = macho_emit_rand_bounded_from_w8(text, fun, value, frame_size, true);
    size_t done = z_aarch64_emit_b_placeholder(text);
    z_aarch64_patch_cond19(text, empty, text->len);
    z_aarch64_emit_movz_w(text, 0, 0);
    z_aarch64_emit_movz_w(text, 1, 0);
    z_aarch64_patch_branch26(text, done, text->len);
    return ok;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O std.rand bounded helper is invalid", value->line, value->column, "invalid rand helper");
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
  if (!macho_emit_u32_bounds_check(text, 8, 10, ctx, diag)) return false;
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

static void macho_emit_item_copy_loop(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  // Inputs: x11=source ptr, w10=source len, x12=destination ptr, w13=destination len. Output: reg=copied item count.
  z_aarch64_emit_mov_w(text, 14, 13);
  z_aarch64_emit_cmp_w(text, 13, 10);
  size_t keep_dst_len = z_aarch64_emit_b_cond_placeholder(text, 9);
  z_aarch64_emit_mov_w(text, 14, 10);
  z_aarch64_patch_cond19(text, keep_dst_len, text->len);
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 14);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  macho_emit_add_scaled_index(text, 15, 11, 9, element_type);
  macho_emit_load_ptr_element(text, 15, 15, element_type);
  macho_emit_add_scaled_index(text, 13, 12, 9, element_type);
  macho_emit_store_ptr_element(text, 15, 13, element_type);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, reg, 14);
}

static bool macho_emit_item_copy_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O item copy requires source and destination views", value->line, value->column, "missing item view");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 12, 13, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  macho_emit_item_copy_loop(text, reg, value->element_type == IR_TYPE_UNSUPPORTED ? macho_view_element_type(value->left) : value->element_type);
  return true;
}

static void macho_emit_item_fill_loop(ZBuf *text, unsigned reg, IrTypeKind element_type) {
  // Inputs: x11=destination ptr, w10=len, x8=item value. Output: reg=filled item count.
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t done = z_aarch64_emit_b_cond_placeholder(text, 2);
  macho_emit_add_scaled_index(text, 12, 11, 9, element_type);
  macho_emit_store_ptr_element(text, 8, 12, element_type);
  z_aarch64_emit_add_w_imm(text, 9, 9, 1);
  size_t back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, back, loop);
  z_aarch64_patch_cond19(text, done, text->len);
  z_aarch64_emit_mov_w(text, reg, 10);
}

static bool macho_emit_item_fill_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O item fill requires a value and destination view", value->line, value->column, "missing item fill input");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->element_type == IR_TYPE_UNSUPPORTED ? value->left->type : value->element_type, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 11, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, value->element_type == IR_TYPE_UNSUPPORTED ? value->left->type : value->element_type, scratch_slot, value->left, diag)) return false;
  macho_emit_item_fill_loop(text, reg, value->element_type == IR_TYPE_UNSUPPORTED ? macho_view_element_type(value->right) : value->element_type);
  return true;
}

static bool macho_emit_item_contains_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O item contains requires an input view and needle", value->line, value->column, "missing item contains input");
  IrTypeKind element_type = value->element_type == IR_TYPE_UNSUPPORTED ? macho_view_element_type(value->left) : value->element_type;
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 12, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, 0);
  size_t loop = text->len;
  z_aarch64_emit_cmp_w(text, 9, 10);
  size_t done_without_match = z_aarch64_emit_b_cond_placeholder(text, 2);
  macho_emit_add_scaled_index(text, 13, 11, 9, element_type);
  macho_emit_load_ptr_element(text, 15, 13, element_type);
  if (macho_type_is_scalar64(element_type)) z_aarch64_emit_cmp_x(text, 15, 12);
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

static bool macho_emit_args_eq_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O std.cli.argEquals requires an index and expected text", value ? value->line : 1, value ? value->column : 1, "missing argEquals input");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 10, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_cmp_w(text, 10, 20);
  size_t in_range = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, in_range, text->len);

  z_aarch64_emit_add_x_reg_lsl(text, 11, 21, 10, 3);
  z_aarch64_emit_load_x_imm(text, 11, 11, 0);
  z_aarch64_emit_movz_w(text, 10, 0);
  size_t loop_start = text->len;
  z_aarch64_emit_add_x_reg(text, 13, 11, 10);
  z_aarch64_emit_load_b_imm(text, 14, 13, 0);
  size_t done_len = z_aarch64_emit_cbz_w_placeholder(text, 14);
  z_aarch64_emit_add_w_imm(text, 10, 10, 1);
  size_t loop_back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, loop_back, loop_start);
  z_aarch64_patch_cond19(text, done_len, text->len);

  if (!macho_emit_store_scratch(text, 11, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 12, 9, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 10, IR_TYPE_U32, scratch_slot + 1, value, diag)) return false;
  z_aarch64_emit_cmp_w(text, 10, 9);
  size_t same_len = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t done_compare = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, same_len, text->len);
  if (!macho_emit_load_scratch(text, 11, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  z_aarch64_emit_byte_eq_loop(text, reg);
  z_aarch64_patch_branch26(text, done_compare, text->len);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_str_contains_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O std.str.contains requires two byte views", value->line, value->column, "missing byte view");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_STR_CONTAINS, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_byte_view_to_scratch(ZBuf *text, const IrFunction *fun, const IrValue *view, unsigned slot, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, view, 8, 9, frame_size, scratch_slot + 8, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, IR_TYPE_U64, slot, view, diag)) return false;
  return macho_emit_store_scratch(text, 9, IR_TYPE_U32, slot + 1, view, diag);
}

static bool macho_emit_load_byte_view_from_scratch(ZBuf *text, const IrValue *view, unsigned ptr_reg, unsigned len_reg, unsigned slot, ZDiag *diag) {
  if (!macho_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, slot, view, diag)) return false;
  return macho_emit_load_scratch(text, len_reg, IR_TYPE_U32, slot + 1, view, diag);
}

static MachORuntimeHelper macho_str_runtime_helper(IrStrOp op) {
  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
      return MACHO_RUNTIME_STR_BUFFER_OP;
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      return MACHO_RUNTIME_CRYPTO_DIGEST;
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
      return MACHO_RUNTIME_CRYPTO_HMAC_SHA256;
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      return MACHO_RUNTIME_CRYPTO_HMAC_SHA256_HEX;
    case IR_STR_OP_CONCAT:
      return MACHO_RUNTIME_STR_CONCAT;
    case IR_STR_OP_REPEAT:
      return MACHO_RUNTIME_STR_REPEAT;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      return MACHO_RUNTIME_STR_TRIM_OP;
    case IR_STR_OP_COUNT_BYTE:
      return MACHO_RUNTIME_STR_COUNT_BYTE;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      return MACHO_RUNTIME_STR_PAIR_OP;
    case IR_STR_OP_WORD_COUNT_ASCII:
      return MACHO_RUNTIME_STR_WORD_COUNT_ASCII;
  }
  return MACHO_RUNTIME_HELPER_COUNT;
}

static uint32_t macho_crypto_digest_op(IrStrOp op) {
  return op == IR_STR_OP_CRYPTO_SHA256_HEX ? 1u : 0u;
}

static void macho_emit_encoded_len_to_maybe_byte_view_regs(ZBuf *text) {
  z_aarch64_emit_mov_w(text, 2, 0);
  z_aarch64_emit_movz_w(text, 0, 0);
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
  z_aarch64_emit_sub_w_imm(text, 2, 2, 1);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_cond19(text, none, text->len);
}

static bool macho_emit_str_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  IrStrOp op = (IrStrOp)value->int_value;
  MachORuntimeHelper helper = macho_str_runtime_helper(op);
  if (helper == MACHO_RUNTIME_HELPER_COUNT) return macho_diag_at(diag, "direct AArch64 Mach-O std.str helper is unknown", value->line, value->column, "unknown std.str op");

  switch (op) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      if (value->arg_len != 2) return macho_diag_at(diag, "direct AArch64 Mach-O std.str buffer helper requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      z_aarch64_emit_movz_w(text, 4, helper == MACHO_RUNTIME_CRYPTO_DIGEST ? macho_crypto_digest_op(op) : (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      macho_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_CONCAT:
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      if (value->arg_len != 3) return macho_diag_at(diag, "direct AArch64 Mach-O std.str three-view helper requires three arguments", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[2], scratch_slot + 4, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[2], 4, 5, scratch_slot + 4, diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      macho_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_REPEAT:
      if (value->arg_len != 3) return macho_diag_at(diag, "direct AArch64 Mach-O std.str.repeat requires three arguments", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_value_to_reg_at(text, fun, value->args[2], 8, frame_size, scratch_slot + 8, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      if (!macho_emit_load_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->args[2], diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      z_aarch64_emit_mov_w(text, 2, 0);
      if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      macho_emit_encoded_len_to_maybe_byte_view_regs(text);
      return true;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      if (value->arg_len != 1) return macho_diag_at(diag, "direct AArch64 Mach-O std.str trim helper requires one argument", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      z_aarch64_emit_movz_w(text, 2, (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      z_aarch64_emit_mov_x(text, 8, 0);
      z_aarch64_emit_lsr_x_imm(text, 9, 8, 32);
      z_aarch64_emit_mov_w(text, 1, 8);
      if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->args[0], diag)) return false;
      z_aarch64_emit_add_x_reg(text, 0, 0, 9);
      if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
      return true;
    case IR_STR_OP_COUNT_BYTE:
      if (value->arg_len != 2) return macho_diag_at(diag, "direct AArch64 Mach-O std.str.countByte requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_value_to_reg_at(text, fun, value->args[1], 8, frame_size, scratch_slot + 8, ctx, diag)) return false;
      if (!macho_emit_store_scratch(text, 8, IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_scratch(text, 2, IR_TYPE_U32, scratch_slot + 2, value->args[1], diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
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
      if (value->arg_len != 2) return macho_diag_at(diag, "direct AArch64 Mach-O std.str pair helper requires two arguments", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[1], scratch_slot + 2, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[1], 2, 3, scratch_slot + 2, diag)) return false;
      z_aarch64_emit_movz_w(text, 4, (uint32_t)op);
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    case IR_STR_OP_WORD_COUNT_ASCII:
      if (value->arg_len != 1) return macho_diag_at(diag, "direct AArch64 Mach-O std.str.wordCountAscii requires one argument", value->line, value->column, "invalid std.str arity");
      if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
      if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
      {
        size_t patch = z_aarch64_emit_bl_placeholder(text);
        if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
      }
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
  }
  return macho_diag_at(diag, "direct AArch64 Mach-O std.str helper is unsupported", value->line, value->column, "unsupported std.str op");
}

static bool macho_emit_ascii_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return macho_diag_at(diag, "direct AArch64 Mach-O std.ascii helper requires one byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.ascii arity");
  if (!macho_emit_value_to_reg_at(text, fun, value->args[0], 0, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 1, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_ASCII_OP, patch, value, diag)) return false;
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

static bool macho_emit_text_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len != 1) return macho_diag_at(diag, "direct AArch64 Mach-O std.text helper requires one byte-view argument", value ? value->line : 1, value ? value->column : 1, "invalid std.text arity");
  if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_TEXT_OP, patch, value, diag)) return false;
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

static void macho_emit_parse_u32_result_to_maybe_regs(ZBuf *text);

static bool macho_value_returns_maybe_usize(const IrValue *value) {
  return value && value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE;
}

static bool macho_emit_parse_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len < 1 || value->arg_len > 2) return macho_diag_at(diag, "direct AArch64 Mach-O std.parse helper requires one byte-view argument and optional byte argument", value ? value->line : 1, value ? value->column : 1, "invalid std.parse arity");
  if (!macho_emit_byte_view_to_scratch(text, fun, value->args[0], scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (value->arg_len == 2) {
    if (!macho_emit_value_to_reg_at(text, fun, value->args[1], 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  } else {
    z_aarch64_emit_movz_x(text, 2, 0);
  }
  if (!macho_emit_load_byte_view_from_scratch(text, value->args[0], 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_value_returns_maybe_usize(value) ? MACHO_RUNTIME_PARSE_USIZE : MACHO_RUNTIME_PARSE_OP, patch, value, diag)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!macho_value_returns_maybe_usize(value)) macho_emit_parse_u32_result_to_maybe_regs(text);
  } else if (reg != 0) {
    if (macho_type_is_scalar64(value->type)) z_aarch64_emit_mov_x(text, reg, 0);
    else z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool macho_emit_time_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) return macho_diag_at(diag, "direct AArch64 Mach-O std.time helper supports at most three Duration arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.time arity");
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!macho_emit_value_to_reg_at(text, fun, value->args[i], 8, frame_size, scratch_slot + 3 + (unsigned)i, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 8, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
  }
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!macho_emit_load_scratch(text, (unsigned)i, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
    } else {
      z_aarch64_emit_movz_x(text, (unsigned)i, 0);
    }
  }
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_TIME_OP, patch, value, diag)) return false;
  if (value->type == IR_TYPE_I32) {
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  } else {
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  }
  return true;
}

static bool macho_emit_term_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value && (IrTermOp)value->int_value == IR_TERM_OP_READ_INPUT) {
    if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O std.term.readInput requires a caller buffer", value->line, value->column, "missing terminal input buffer");
    if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_TERM_READ_INPUT, patch, value, diag);
  }
  if (!value || value->arg_len > 1) return macho_diag_at(diag, "direct AArch64 Mach-O std.term helper supports at most one fallback argument", value ? value->line : 1, value ? value->column : 1, "invalid std.term arity");
  if (value->arg_len == 1) {
    if (!macho_emit_value_to_reg_at(text, fun, value->args[0], 0, frame_size, scratch_slot, ctx, diag)) return false;
  } else {
    z_aarch64_emit_movz_x(text, 0, 0);
  }
  z_aarch64_emit_movz_w(text, 1, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_TERM_OP, patch, value, diag)) return false;
  if (value->type == IR_TYPE_BOOL) {
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  } else {
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  }
  return true;
}

static bool macho_emit_math_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || value->arg_len > 3) return macho_diag_at(diag, "direct AArch64 Mach-O std.math helper supports at most three scalar arguments", value ? value->line : 1, value ? value->column : 1, "invalid std.math arity");
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!macho_emit_value_to_reg_at(text, fun, value->args[i], 8, frame_size, scratch_slot + 3 + (unsigned)i, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, 8, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
  }
  for (size_t i = 0; i < 3; i++) {
    if (i < value->arg_len) {
      if (!macho_emit_load_scratch(text, (unsigned)i, IR_TYPE_I64, scratch_slot + (unsigned)i, value->args[i], diag)) return false;
    } else {
      z_aarch64_emit_movz_x(text, (unsigned)i, 0);
    }
  }
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_value_returns_maybe_usize(value) ? MACHO_RUNTIME_MATH_USIZE_OP : MACHO_RUNTIME_MATH_OP, patch, value, diag)) return false;
  if (value->type == IR_TYPE_MAYBE_SCALAR) {
    if (!macho_value_returns_maybe_usize(value)) macho_emit_parse_u32_result_to_maybe_regs(text);
  } else if (macho_type_is_scalar64(value->type)) {
    if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  } else {
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  }
  return true;
}

static bool macho_emit_search_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O std.search helper requires a span and needle", value ? value->line : 1, value ? value->column : 1, "invalid std.search input");
  if (!macho_emit_byte_view_to_scratch(text, fun, value->left, scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_byte_view_from_scratch(text, value->left, 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_SEARCH_OP, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
  return true;
}

static bool macho_emit_sort_runtime_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return macho_diag_at(diag, "direct AArch64 Mach-O std.sort helper requires a span", value ? value->line : 1, value ? value->column : 1, "invalid std.sort input");
  if (!macho_emit_byte_view_to_scratch(text, fun, value->left, scratch_slot, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_load_byte_view_from_scratch(text, value->left, 0, 1, scratch_slot, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  MachORuntimeHelper helper = value->type == IR_TYPE_BOOL ? MACHO_RUNTIME_SORT_IS_SORTED_OP : MACHO_RUNTIME_SORT_OP;
  if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
  if (value->type == IR_TYPE_BOOL && reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_crc32_bytes_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O CRC32 requires a byte view", value->line, value->column, "missing byte view");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 11, 10, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_crc32_bytes_loop(text, reg);
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
    if (!macho_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
  if (!macho_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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

static bool macho_emit_http_request_span_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 1, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 1, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool macho_emit_http_request_body_within_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->index) {
    return macho_diag_at(diag, "direct AArch64 Mach-O HTTP request body helper requires a request and limit", value ? value->line : 1, value ? value->column : 1, "missing HTTP request body input");
  }
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_USIZE, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_USIZE, scratch_slot + 2, value->index, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, value->int_value ? 1u : 0u);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_REQUEST_BODY_WITHIN, patch, value, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot + 3, value, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 8, IR_TYPE_U64, scratch_slot + 3, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 9, 8);
  z_aarch64_emit_add_x_reg(text, 1, 1, 9);
  z_aarch64_emit_lsr_x_imm(text, 2, 8, 32);
  z_aarch64_emit_movz_w(text, 10, 0x7fffffffu);
  z_aarch64_emit_and_w_reg(text, 2, 2, 10);
  z_aarch64_emit_lsr_x_imm(text, 0, 8, 63);
  return true;
}

static bool macho_emit_http_write_json_response_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U32, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 3, 4, frame_size, scratch_slot + 3, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 3, IR_TYPE_U64, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_store_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U32, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_load_scratch(text, 3, IR_TYPE_U64, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_WRITE_JSON_RESPONSE, patch, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 2, 0);
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 0, 0);
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_cond19(text, none, text->len);
  return true;
}

static bool macho_emit_http_request_matches_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->index || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O HTTP request matcher requires request, method, and path", value ? value->line : 1, value ? value->column : 1, "missing HTTP request matcher input");
  }
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->index, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_store_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->index, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 4, IR_TYPE_U64, scratch_slot + 4, value->right, diag)) return false;
  if (!macho_emit_store_scratch(text, 5, IR_TYPE_U32, scratch_slot + 5, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_load_scratch(text, 3, IR_TYPE_U32, scratch_slot + 3, value->index, diag)) return false;
  if (!macho_emit_load_scratch(text, 4, IR_TYPE_U64, scratch_slot + 4, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 5, IR_TYPE_U32, scratch_slot + 5, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_HTTP_REQUEST_MATCHES, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_http_status_class_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value->left || value->left->type != IR_TYPE_U16) {
    return macho_diag_at(diag, "direct AArch64 Mach-O HTTP status predicate expects a u16 status", value->line, value->column, "invalid HTTP status predicate");
  }
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 9, (uint32_t)value->int_value);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t below = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower than class start
  z_aarch64_emit_movz_w(text, 9, (uint32_t)value->data_len);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t above_or_equal = z_aarch64_emit_b_cond_placeholder(text, 2); // unsigned >= class end
  z_aarch64_emit_movz_w(text, reg, 1);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, below, text->len);
  z_aarch64_patch_cond19(text, above_or_equal, text->len);
  z_aarch64_emit_movz_w(text, reg, 0);
  z_aarch64_patch_branch26(text, end, text->len);
  return true;
}

static unsigned macho_fs_path_op_for_value(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_FS_EXISTS: return 0u;
    case IR_VALUE_FS_IS_DIR: return 1u;
    case IR_VALUE_FS_MAKE_DIR: return 2u;
    case IR_VALUE_FS_REMOVE_DIR: return 3u;
    case IR_VALUE_FS_REMOVE: return 4u;
    default: return 0u;
  }
}

static bool macho_emit_runtime_byte_views_at(ZBuf *text, const IrFunction *fun, const IrValue **views, unsigned count, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  for (unsigned i = 0; i < count; i++) {
    unsigned ptr_reg = i * 2u;
    unsigned len_reg = ptr_reg + 1u;
    unsigned slot = scratch_slot + ptr_reg;
    if (!macho_emit_byte_view_pair_at(text, fun, views[i], ptr_reg, len_reg, frame_size, slot, ctx, diag)) return false;
    if (!macho_emit_store_scratch(text, ptr_reg, IR_TYPE_U64, slot, views[i], diag)) return false;
    if (!macho_emit_store_scratch(text, len_reg, IR_TYPE_U32, slot + 1u, views[i], diag)) return false;
  }
  for (unsigned i = 0; i < count; i++) {
    unsigned ptr_reg = i * 2u;
    unsigned len_reg = ptr_reg + 1u;
    unsigned slot = scratch_slot + ptr_reg;
    if (!macho_emit_load_scratch(text, ptr_reg, IR_TYPE_U64, slot, views[i], diag)) return false;
    if (!macho_emit_load_scratch(text, len_reg, IR_TYPE_U32, slot + 1u, views[i], diag)) return false;
  }
  return true;
}

static bool macho_emit_fs_read_bytes_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.fs.readBytes requires a path and buffer", value ? value->line : 1, value ? value->column : 1, "missing readBytes input");
  }
  const IrValue *views[2] = {value->left, value->right};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 2, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_FS_READ_BYTES, patch, value, diag);
}

static bool macho_emit_fs_read_bytes_at_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->index || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.fs.readBytesAt requires a path, offset, and buffer", value ? value->line : 1, value ? value->column : 1, "missing readBytesAt input");
  }
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 3, 4, frame_size, scratch_slot + 3, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 3, IR_TYPE_U64, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_store_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 2, IR_TYPE_U64, scratch_slot + 2, value->index, diag)) return false;
  if (!macho_emit_load_scratch(text, 3, IR_TYPE_U64, scratch_slot + 3, value->right, diag)) return false;
  if (!macho_emit_load_scratch(text, 4, IR_TYPE_U32, scratch_slot + 4, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_FS_READ_BYTES_AT, patch, value, diag);
}

static bool macho_emit_fs_write_bytes_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, MachORuntimeHelper helper, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O filesystem write requires a path and byte span", value ? value->line : 1, value ? value->column : 1, "missing write input");
  }
  const IrValue *views[2] = {value->left, value->right};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 2, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag);
}

static bool macho_emit_fs_path_op_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.fs path operation requires a path", value ? value->line : 1, value ? value->column : 1, "missing filesystem path");
  }
  const IrValue *views[1] = {value->left};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 1, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 2, macho_fs_path_op_for_value(value->kind));
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_FS_PATH_OP, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_fs_rename_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.fs.rename requires source and destination paths", value ? value->line : 1, value ? value->column : 1, "missing rename input");
  }
  const IrValue *views[2] = {value->left, value->right};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 2, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_FS_RENAME, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_fs_atomic_write_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.fs.atomicWrite requires path, temp path, and bytes", value ? value->line : 1, value ? value->column : 1, "missing atomicWrite input");
  }
  const IrValue *views[3] = {value->left, value->right, value->index};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 3, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_FS_ATOMIC_WRITE, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_capture_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value && value->arg_len == 2) {
    const IrValue *views[3] = {value->args[0], value->args[1], value->right};
    if (!macho_emit_runtime_byte_views_at(text, fun, views, 3, frame_size, scratch_slot, ctx, diag)) return false;
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CAPTURE_ARGS, patch, value, diag);
  }
  if (!value || !value->left || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc.capture requires a command and output buffer", value ? value->line : 1, value ? value->column : 1, "missing process capture input");
  }
  const IrValue *views[2] = {value->left, value->right};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 2, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CAPTURE, patch, value, diag);
}

static bool macho_emit_proc_capture_files_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value && value->arg_len == 2) {
    const IrValue *views[4] = {value->args[0], value->args[1], value->right, value->index};
    if (!macho_emit_runtime_byte_views_at(text, fun, views, 4, frame_size, scratch_slot, ctx, diag)) return false;
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CAPTURE_FILES_ARGS, patch, value, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
    return true;
  }
  if (!value || !value->left || !value->right || !value->index) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc.captureFiles requires a command, stdout path, and stderr path", value ? value->line : 1, value ? value->column : 1, "missing process capture files input");
  }
  const IrValue *views[3] = {value->left, value->right, value->index};
  if (!macho_emit_runtime_byte_views_at(text, fun, views, 3, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CAPTURE_FILES, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_spawn_inherit_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value && value->arg_len == 4) {
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[0], 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[1], 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[2], 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[3], 6, 7, frame_size, scratch_slot + 6, ctx, diag)) return false;
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_SPAWN_INHERIT_ARGS, patch, value, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
    return true;
  }
  if (!value || !value->left) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc.spawnInherit requires a command", value ? value->line : 1, value ? value->column : 1, "missing process command");
  }
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_SPAWN_INHERIT, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_child_spawn_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value && value->arg_len == 4) {
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[0], 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[1], 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[2], 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
    if (!macho_emit_byte_view_pair_at(text, fun, value->args[3], 6, 7, frame_size, scratch_slot + 6, ctx, diag)) return false;
    size_t patch = z_aarch64_emit_bl_placeholder(text);
    MachORuntimeHelper helper = value->int_value ? MACHO_RUNTIME_PTY_SPAWN_ARGS : MACHO_RUNTIME_PROC_SPAWN_CHILD_ARGS;
    if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
    return true;
  }
  if (!value || !value->left) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc.spawnChild requires a command", value ? value->line : 1, value ? value->column : 1, "missing process command");
  }
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (value->right && !macho_emit_byte_view_pair_at(text, fun, value->right, 2, 3, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (value->index && !macho_emit_byte_view_pair_at(text, fun, value->index, 4, 5, frame_size, scratch_slot + 4, ctx, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  MachORuntimeHelper helper = MACHO_RUNTIME_PROC_SPAWN_CHILD;
  if (value->int_value) helper = value->index ? MACHO_RUNTIME_PTY_SPAWN_IN_ENV : (value->right ? MACHO_RUNTIME_PTY_SPAWN_IN : MACHO_RUNTIME_PTY_SPAWN);
  else helper = value->index ? MACHO_RUNTIME_PROC_SPAWN_CHILD_IN_ENV : (value->right ? MACHO_RUNTIME_PROC_SPAWN_CHILD_IN : MACHO_RUNTIME_PROC_SPAWN_CHILD);
  if (!z_macho_record_value_runtime_patch(ctx, helper, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_pty_resize_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right || !value->index) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.pty.resize requires a handle, columns, and rows", value ? value->line : 1, value ? value->column : 1, "missing pty resize input");
  }
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 8, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->right->type, scratch_slot + 1, value->right, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->index, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, value->left->type, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, value->right->type, scratch_slot + 1, value->right, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PTY_RESIZE, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_child_op_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc child op requires a handle", value ? value->line : 1, value ? value->column : 1, "missing process child handle");
  }
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 0, frame_size, scratch_slot, ctx, diag)) return false;
  z_aarch64_emit_movz_w(text, 1, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CHILD_OP, patch, value, diag)) return false;
  if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
  return true;
}

static bool macho_emit_proc_child_io_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) {
    return macho_diag_at(diag, "direct AArch64 Mach-O std.proc child I/O requires a handle and buffer", value ? value->line : 1, value ? value->column : 1, "missing process child I/O input");
  }
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 8, value->left->type, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_byte_view_pair_at(text, fun, value->right, 1, 2, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, value->left->type, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 3, (uint32_t)value->int_value);
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  return z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PROC_CHILD_IO, patch, value, diag);
}

static bool macho_emit_http_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  switch (value->kind) {
    case IR_VALUE_HTTP_STATUS_CLASS:
      return macho_emit_http_status_class_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_FETCH:
      return macho_emit_http_fetch_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_RESULT_OK: case IR_VALUE_HTTP_RESULT_STATUS: case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR: case IR_VALUE_HTTP_HEADER_FOUND: case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN:
      return macho_emit_http_result_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_RESPONSE_LEN: case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN: case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
      return macho_emit_http_response_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_HEADER_VALUE:
      return macho_emit_http_header_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH:
      (void)reg;
      return macho_emit_http_request_span_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN:
      (void)reg;
      return macho_emit_http_request_body_within_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_REQUEST_MATCHES:
      return macho_emit_http_request_matches_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE:
      (void)reg;
      return macho_emit_http_write_json_response_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    default:
      return macho_diag_at(diag, "direct AArch64 Mach-O HTTP value kind is unsupported", value->line, value->column, "unsupported HTTP value");
  }
}

static void macho_emit_parse_u32_result_to_maybe_regs(ZBuf *text) {
  z_aarch64_emit_mov_w(text, 1, 0);
  z_aarch64_emit_lsr_x_imm(text, 0, 0, 32);
}

static bool macho_emit_parse_u32_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value) return macho_diag_at(diag, "direct AArch64 Mach-O parse value is missing", 1, 1, "missing parse value");
  switch (value->kind) {
    case IR_VALUE_PARSE_I32:
    case IR_VALUE_PARSE_U32: {
      if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, value->kind == IR_VALUE_PARSE_I32 ? MACHO_RUNTIME_PARSE_I32 : MACHO_RUNTIME_PARSE_U32, patch, value, diag)) return false;
      macho_emit_parse_u32_result_to_maybe_regs(text);
      return true;
    }
    case IR_VALUE_ARGS_PARSE_U32: {
      if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O std.args.parseU32 requires an index", value->line, value->column, "missing index");
      if (!macho_emit_value_to_reg_at(text, fun, value->left, 10, frame_size, scratch_slot, ctx, diag)) return false;
      z_aarch64_emit_cmp_w(text, 10, 20);
      size_t in_range = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
      z_aarch64_emit_movz_w(text, 0, 0);
      z_aarch64_emit_movz_x(text, 1, 0);
      size_t end = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, in_range, text->len);

      z_aarch64_emit_add_x_reg_lsl(text, 12, 21, 10, 3);
      z_aarch64_emit_load_x_imm(text, 12, 12, 0);
      z_aarch64_emit_movz_w(text, 1, 0);
      size_t loop_start = text->len;
      z_aarch64_emit_add_x_reg(text, 13, 12, 1);
      z_aarch64_emit_load_b_imm(text, 14, 13, 0);
      size_t done_patch = z_aarch64_emit_cbz_w_placeholder(text, 14);
      z_aarch64_emit_add_w_imm(text, 1, 1, 1);
      size_t loop_patch = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_branch26(text, loop_patch, loop_start);
      z_aarch64_patch_cond19(text, done_patch, text->len);

      z_aarch64_emit_mov_x(text, 0, 12);
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PARSE_U32, patch, value, diag)) return false;
      macho_emit_parse_u32_result_to_maybe_regs(text);
      z_aarch64_patch_branch26(text, end, text->len);
      return true;
    }
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32: {
      size_t none = 0;
      size_t found = 0;
      if (!macho_emit_args_find_next_index_at(text, fun, value, frame_size, scratch_slot, ctx, diag, &none, &found)) return false;
      z_aarch64_patch_cond19(text, none, text->len);
      z_aarch64_emit_movz_w(text, 0, 0);
      z_aarch64_emit_movz_x(text, 1, 0);
      size_t end = z_aarch64_emit_b_placeholder(text);
      z_aarch64_patch_cond19(text, found, text->len);
      macho_emit_args_value_after_load_argv_value(text);
      macho_emit_strlen_x12_to_w10(text);
      z_aarch64_emit_mov_x(text, 0, 12);
      z_aarch64_emit_mov_w(text, 1, 10);
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_PARSE_U32, patch, value, diag)) return false;
      macho_emit_parse_u32_result_to_maybe_regs(text);
      z_aarch64_patch_branch26(text, end, text->len);
      return true;
    }
    default:
      return macho_diag_at(diag, "direct AArch64 Mach-O parse value kind is invalid for this helper", value->line, value->column, "invalid parse value");
  }
}

static bool macho_emit_args_find_to_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return macho_diag_at(diag, "direct AArch64 Mach-O args find helper requires a name", value ? value->line : 1, value ? value->column : 1, "missing args name");
  if (!macho_emit_args_find_call_at(text, fun, value, value->left, frame_size, scratch_slot, ctx, diag)) return false;
  if (value->kind == IR_VALUE_ARGS_CONTAINS) {
    z_aarch64_emit_lsr_x_imm(text, 0, 0, 32);
    if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
    return true;
  }
  macho_emit_parse_u32_result_to_maybe_regs(text);
  return true;
}

static bool macho_emit_args_value_after_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  size_t none = 0;
  size_t found = 0;
  if (!macho_emit_args_find_next_index_at(text, fun, value, frame_size, scratch_slot, ctx, diag, &none, &found)) return false;
  z_aarch64_patch_cond19(text, none, text->len);
  z_aarch64_emit_movz_w(text, 0, 0);
  z_aarch64_emit_movz_x(text, 1, 0);
  z_aarch64_emit_movz_w(text, 2, 0);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, found, text->len);
  macho_emit_args_value_after_load_argv_value(text);
  macho_emit_strlen_x12_to_w10(text);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_emit_mov_x(text, 1, 12);
  z_aarch64_emit_mov_w(text, 2, 10);
  z_aarch64_patch_branch26(text, end, text->len);
  return true;
}

static bool macho_emit_args_value_after_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned local_index, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  size_t none = 0;
  size_t found = 0;
  if (!macho_emit_args_find_next_index_at(text, fun, value, frame_size, 0, ctx, diag, &none, &found)) return false;
  z_aarch64_patch_cond19(text, none, text->len);
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, local_index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 8, local_index, 16, frame_size);
  size_t end = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, found, text->len);
  macho_emit_args_value_after_load_argv_value(text);
  macho_emit_strlen_x12_to_w10(text);
  z_aarch64_emit_movz_w(text, 8, 1);
  macho_emit_store_local_w(text, fun, 8, local_index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 12, local_index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 10, local_index, 16, frame_size);
  z_aarch64_patch_branch26(text, end, text->len);
  return true;
}

static bool macho_emit_fmt_u32_to_maybe_regs_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left || !value->right) return macho_diag_at(diag, "direct AArch64 Mach-O std.fmt helper requires a buffer and value", value ? value->line : 1, value ? value->column : 1, "missing fmt input");
  if (!macho_emit_byte_view_pair_at(text, fun, value->left, 0, 1, frame_size, scratch_slot, ctx, diag)) return false;
  if (!macho_emit_store_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_store_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 2, frame_size, scratch_slot + 2, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 0, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U32, scratch_slot + 1, value->left, diag)) return false;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!z_macho_record_value_runtime_patch(ctx, macho_runtime_helper_for_value(value->kind), patch, value, diag)) return false;
  z_aarch64_emit_mov_w(text, 2, 0);
  if (!macho_emit_load_scratch(text, 1, IR_TYPE_U64, scratch_slot, value->left, diag)) return false;
  z_aarch64_emit_movz_w(text, 0, 0);
  size_t none = z_aarch64_emit_cbz_w_placeholder(text, 2);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_cond19(text, none, text->len);
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

static bool macho_emit_vec_clear_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec clear requires a Vec local", value->line, value->column, "invalid Vec local");
  z_aarch64_emit_movz_w(text, reg, 0);
  macho_emit_store_local_w(text, fun, reg, value->local_index, 8, frame_size);
  return true;
}

static bool macho_emit_vec_pop_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec pop requires a Vec local", value->line, value->column, "invalid Vec local");
  macho_emit_load_local_w(text, fun, 8, value->local_index, 8, frame_size);
  size_t empty_patch = z_aarch64_emit_cbz_w_placeholder(text, 8);
  z_aarch64_emit_sub_w_imm(text, 8, 8, 1);
  macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
  z_aarch64_emit_movz_w(text, reg, 1);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, empty_patch, text->len);
  z_aarch64_emit_movz_w(text, reg, 0);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_vec_truncate_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec truncate requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec truncate requires a length", value->line, value->column, "missing Vec length");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t requested_patch = z_aarch64_emit_b_cond_placeholder(text, 3);
  z_aarch64_emit_mov_w(text, 8, 9);
  z_aarch64_patch_cond19(text, requested_patch, text->len);
  macho_emit_store_local_w(text, fun, 8, value->local_index, 8, frame_size);
  if (reg != 8) z_aarch64_emit_mov_w(text, reg, 8);
  return true;
}

static bool macho_emit_vec_remove_swap_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec swap-remove requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec swap-remove requires an index", value->line, value->column, "missing Vec index");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3);
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  z_aarch64_emit_sub_w_imm(text, 9, 9, 1);
  macho_emit_load_local_x(text, fun, 10, value->local_index, 0, frame_size);
  z_aarch64_emit_add_x_reg(text, 11, 10, 9);
  z_aarch64_emit_load_b_imm(text, 12, 11, 0);
  z_aarch64_emit_add_x_reg(text, 11, 10, 8);
  z_aarch64_emit_store_b_imm(text, 12, 11, 0);
  macho_emit_store_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_movz_w(text, reg, 1);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_vec_lookup_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec lookup requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec lookup requires a value", value->line, value->column, "missing Vec value");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 12, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_x(text, fun, 10, value->local_index, 0, frame_size);
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_vec_lookup_loop(text, reg, value->kind == IR_VALUE_VEC_CONTAINS);
  return true;
}

static bool macho_emit_vec_insert_unique_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec insert-unique requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec insert-unique requires a value", value->line, value->column, "missing Vec value");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 12, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_x(text, fun, 10, value->local_index, 0, frame_size);
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  macho_emit_load_local_w(text, fun, 13, value->local_index, 12, frame_size);
  z_aarch64_emit_vec_insert_unique_loop(text, 15);
  macho_emit_store_local_w(text, fun, 9, value->local_index, 8, frame_size);
  if (reg != 15) z_aarch64_emit_mov_w(text, reg, 15);
  return true;
}

static bool macho_emit_vec_remove_value_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec remove-value requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec remove-value requires a value", value->line, value->column, "missing Vec value");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 12, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_x(text, fun, 10, value->local_index, 0, frame_size);
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_vec_remove_value_loop(text, 15);
  macho_emit_store_local_w(text, fun, 9, value->local_index, 8, frame_size);
  if (reg != 15) z_aarch64_emit_mov_w(text, reg, 15);
  return true;
}

static bool macho_emit_vec_get_to_maybe_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec get requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec get requires an index", value->line, value->column, "missing Vec index");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, 0, 0);
  z_aarch64_emit_movz_x(text, 1, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  macho_emit_load_local_x(text, fun, 9, value->local_index, 0, frame_size);
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  z_aarch64_emit_load_b_imm(text, 1, 9, 0);
  z_aarch64_emit_movz_w(text, 0, 1);
  z_aarch64_patch_branch26(text, end_patch, text->len);
  return true;
}

static bool macho_emit_vec_set_to_reg_at(ZBuf *text, const IrFunction *fun, const IrValue *value, unsigned reg, unsigned frame_size, unsigned scratch_slot, MachOEmitContext *ctx, ZDiag *diag) {
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec set requires a Vec local", value->line, value->column, "invalid Vec local");
  if (!value->left) return macho_diag_at(diag, "direct AArch64 Mach-O Vec set requires an index", value->line, value->column, "missing Vec index");
  if (!value->right) return macho_diag_at(diag, "direct AArch64 Mach-O Vec set requires a value", value->line, value->column, "missing Vec value");
  if (!macho_emit_value_to_reg_at(text, fun, value->left, 8, frame_size, scratch_slot, ctx, diag)) return false;
  macho_emit_load_local_w(text, fun, 9, value->local_index, 8, frame_size);
  z_aarch64_emit_cmp_w(text, 8, 9);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 3); // unsigned lower
  z_aarch64_emit_movz_w(text, reg, 0);
  size_t end_patch = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  macho_emit_load_local_x(text, fun, 9, value->local_index, 0, frame_size);
  z_aarch64_emit_add_x_reg(text, 9, 9, 8);
  if (!macho_emit_store_scratch(text, 9, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  if (!macho_emit_value_to_reg_at(text, fun, value->right, 10, frame_size, scratch_slot + 1, ctx, diag)) return false;
  if (!macho_emit_load_scratch(text, 9, IR_TYPE_U64, scratch_slot, value, diag)) return false;
  z_aarch64_emit_store_b_imm(text, 10, 9, 0);
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
    case IR_VALUE_BINARY: return macho_emit_binary_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_COMPARE: return macho_emit_compare_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CALL: return macho_emit_call_to_reg(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_HOST:
      z_aarch64_emit_movz_w(text, reg, 0);
      return true;
    case IR_VALUE_FS_READ_BYTES_PATH: return macho_emit_fs_read_bytes_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_READ_BYTES_AT_PATH: return macho_emit_fs_read_bytes_at_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_WRITE_BYTES_PATH: return macho_emit_fs_write_bytes_to_maybe_regs_at(text, fun, value, MACHO_RUNTIME_FS_WRITE_BYTES, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_APPEND_BYTES_PATH: return macho_emit_fs_write_bytes_to_maybe_regs_at(text, fun, value, MACHO_RUNTIME_FS_APPEND_BYTES, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE_DIR:
    case IR_VALUE_FS_IS_DIR:
      return macho_emit_fs_path_op_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_RENAME: return macho_emit_fs_rename_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FS_ATOMIC_WRITE: return macho_emit_fs_atomic_write_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_CAPTURE: return macho_emit_proc_capture_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_CAPTURE_FILES: return macho_emit_proc_capture_files_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_SPAWN_INHERIT: return macho_emit_proc_spawn_inherit_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_CHILD_SPAWN: return macho_emit_proc_child_spawn_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_CHILD_OP: return macho_emit_proc_child_op_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_CHILD_IO: return macho_emit_proc_child_io_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PROC_PTY_RESIZE: return macho_emit_proc_pty_resize_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
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
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
      if (!macho_emit_json_diagnostic_call_at(text, fun, value, frame_size, scratch_slot, ctx, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_x(text, reg, 0);
      return true;
    case IR_VALUE_JSON_FIELD:
      (void)reg;
      return macho_emit_json_field_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_LOOKUP_SCALAR:
      (void)reg;
      return macho_emit_json_lookup_scalar_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_STRING_DECODE:
      (void)reg;
      return macho_emit_json_string_decode_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_WRITE_STRING:
      (void)reg;
      return macho_emit_json_string_decode_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_WRITE_RUNTIME:
      (void)reg;
      return macho_emit_json_write_runtime_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_JSON_STRING_FIELD:
      (void)reg;
      return macho_emit_json_string_field_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_HTTP_FETCH: case IR_VALUE_HTTP_RESULT_OK: case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN: case IR_VALUE_HTTP_RESULT_ERROR: case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET: case IR_VALUE_HTTP_HEADER_LEN: case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN: case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: case IR_VALUE_HTTP_HEADER_VALUE: case IR_VALUE_HTTP_REQUEST_MATCHES:
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: case IR_VALUE_HTTP_REQUEST_PATH: case IR_VALUE_HTTP_REQUEST_BODY_WITHIN: case IR_VALUE_HTTP_WRITE_JSON_RESPONSE: case IR_VALUE_HTTP_STATUS_CLASS:
      return macho_emit_http_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PARSE_I32: case IR_VALUE_PARSE_U32: case IR_VALUE_ARGS_PARSE_U32: case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
      (void)reg;
      return macho_emit_parse_u32_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ARGS_VALUE_AFTER:
      (void)reg;
      return macho_emit_args_value_after_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ARGS_FIND: case IR_VALUE_ARGS_CONTAINS:
      return macho_emit_args_find_to_regs_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FMT_BOOL: case IR_VALUE_FMT_HEX_U32: case IR_VALUE_FMT_I32: case IR_VALUE_FMT_U32: case IR_VALUE_FMT_USIZE:
      (void)reg;
      return macho_emit_fmt_u32_to_maybe_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return macho_diag_at(diag, "direct AArch64 Mach-O Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      macho_emit_load_local_w(text, fun, reg, value->local_index, value->kind == IR_VALUE_VEC_LEN ? 8 : 12, frame_size);
      return true;
    case IR_VALUE_VEC_PUSH:
      return macho_emit_vec_push_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_GET:
      (void)reg;
      return macho_emit_vec_get_to_maybe_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_SET:
      return macho_emit_vec_set_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_CLEAR:
      return macho_emit_vec_clear_to_reg_at(text, fun, value, reg, frame_size, diag);
    case IR_VALUE_VEC_POP:
      return macho_emit_vec_pop_to_reg_at(text, fun, value, reg, frame_size, diag);
    case IR_VALUE_VEC_TRUNCATE:
      return macho_emit_vec_truncate_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_REMOVE_SWAP:
      return macho_emit_vec_remove_swap_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_INDEX:
    case IR_VALUE_VEC_CONTAINS:
      return macho_emit_vec_lookup_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_INSERT_UNIQUE:
      return macho_emit_vec_insert_unique_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_VEC_REMOVE_VALUE:
      return macho_emit_vec_remove_value_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
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
    case IR_VALUE_RAND_NEXT_BELOW:
    case IR_VALUE_RAND_RANGE_U32:
      (void)reg;
      return macho_emit_rand_maybe_to_regs_at(text, fun, value, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_RAND_ENTROPY_U32: {
      size_t patch = z_aarch64_emit_bl_placeholder(text);
      if (!z_macho_record_value_runtime_patch(ctx, MACHO_RUNTIME_RAND_ENTROPY_U32, patch, value, diag)) return false;
      if (reg != 0) z_aarch64_emit_mov_w(text, reg, 0);
      return true;
    }
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
    case IR_VALUE_BYTE_VIEW_REMAINING:
      return macho_emit_byte_view_remaining_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_COPY:
      return macho_emit_byte_copy_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_FILL:
      return macho_emit_byte_fill_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ITEM_COPY:
      return macho_emit_item_copy_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ITEM_FILL:
      return macho_emit_item_fill_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ITEM_CONTAINS:
      return macho_emit_item_contains_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_EQ:
      return macho_emit_byte_view_eq_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ARGS_EQ:
      return macho_emit_args_eq_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_STR_CONTAINS:
      return macho_emit_str_contains_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_STR_RUNTIME:
      return macho_emit_str_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_ASCII_RUNTIME:
      return macho_emit_ascii_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_TEXT_RUNTIME:
      return macho_emit_text_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_PARSE_RUNTIME:
      return macho_emit_parse_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_TIME_RUNTIME:
      return macho_emit_time_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_TERM_RUNTIME:
      return macho_emit_term_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_MATH_RUNTIME:
      return macho_emit_math_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_SEARCH_RUNTIME:
      return macho_emit_search_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_SORT_RUNTIME:
      return macho_emit_sort_runtime_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_CRC32_BYTES:
      return macho_emit_crc32_bytes_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
      return macho_emit_byte_view_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_INDEX_LOAD:
      return macho_emit_index_load_to_reg_at(text, fun, value, reg, frame_size, scratch_slot, ctx, diag);
    case IR_VALUE_FIELD_LOAD:
      return macho_emit_field_load_to_reg(text, fun, value, reg, frame_size, diag);
    case IR_VALUE_RECORD_ADDR:
      return macho_emit_record_addr_to_reg(text, fun, value, reg, frame_size, diag);
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
  if (!macho_emit_trap(text, ctx, diag, Z_DIRECT_TRAP_WRITE_FAILED)) return false;
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

static bool macho_emit_env_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return macho_diag_at(diag, "direct AArch64 Mach-O std.env.get requires a key", value ? value->line : 1, value ? value->column : 1, "missing key");
  if (!macho_emit_byte_view_pair(text, fun, value->left, 9, 10, frame_size, ctx, diag)) return false;

  z_aarch64_emit_mov_w(text, 8, 20);
  z_aarch64_emit_add_w_imm(text, 8, 8, 1);
  z_aarch64_emit_add_x_reg_lsl(text, 8, 21, 8, 3);
  size_t env_loop = text->len;
  z_aarch64_emit_load_x_imm(text, 3, 8, 0);
  z_aarch64_emit_movz_x(text, 14, 0);
  z_aarch64_emit_cmp_x(text, 3, 14);
  size_t none = z_aarch64_emit_b_cond_placeholder(text, 0);
  z_aarch64_emit_movz_w(text, 1, 0);

  size_t compare_loop = text->len;
  z_aarch64_emit_cmp_w(text, 1, 10);
  size_t key_done = z_aarch64_emit_b_cond_placeholder(text, 2); // unsigned >= key length
  z_aarch64_emit_add_x_reg(text, 12, 9, 1);
  z_aarch64_emit_load_b_imm(text, 12, 12, 0);
  z_aarch64_emit_add_x_reg(text, 13, 3, 1);
  z_aarch64_emit_load_b_imm(text, 13, 13, 0);
  z_aarch64_emit_cmp_w(text, 12, 13);
  size_t next = z_aarch64_emit_b_cond_placeholder(text, 1); // not equal
  z_aarch64_emit_add_w_imm(text, 1, 1, 1);
  size_t compare_back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, compare_back, compare_loop);

  z_aarch64_patch_cond19(text, key_done, text->len);
  z_aarch64_emit_add_x_reg(text, 13, 3, 10);
  z_aarch64_emit_load_b_imm(text, 13, 13, 0);
  z_aarch64_emit_movz_w(text, 12, 0x3d);
  z_aarch64_emit_cmp_w(text, 13, 12);
  size_t next_after_key = z_aarch64_emit_b_cond_placeholder(text, 1); // not '='
  z_aarch64_emit_add_x_reg(text, 12, 3, 10);
  z_aarch64_emit_add_x_imm(text, 12, 12, 1);
  macho_emit_strlen_x12_to_w10(text);
  z_aarch64_emit_movz_w(text, 8, 1);
  macho_emit_store_local_w(text, fun, 8, local->index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 12, local->index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 10, local->index, 16, frame_size);
  size_t end = z_aarch64_emit_b_placeholder(text);

  z_aarch64_patch_cond19(text, next, text->len);
  z_aarch64_patch_cond19(text, next_after_key, text->len);
  z_aarch64_emit_add_x_imm(text, 8, 8, 8);
  size_t loop_back = z_aarch64_emit_b_placeholder(text);
  z_aarch64_patch_branch26(text, loop_back, env_loop);

  z_aarch64_patch_cond19(text, none, text->len);
  z_aarch64_emit_movz_w(text, 8, 0);
  macho_emit_store_local_w(text, fun, 8, local->index, 0, frame_size);
  macho_emit_store_local_x(text, fun, 8, local->index, 8, frame_size);
  macho_emit_store_local_w(text, fun, 8, local->index, 16, frame_size);
  z_aarch64_patch_branch26(text, end, text->len);
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
  if (instr->value && instr->value->kind == IR_VALUE_ENV_GET) {
    return macho_emit_env_get_to_local(text, fun, instr->value, &fun->locals[instr->local_index], frame_size, ctx, diag);
  }
  if (instr->value && instr->value->kind == IR_VALUE_ARGS_VALUE_AFTER) {
    return macho_emit_args_value_after_to_local(text, fun, instr->value, instr->local_index, frame_size, ctx, diag);
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
  if (instr->value && (instr->value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
                       instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
                       instr->value->kind == IR_VALUE_JSON_FIELD ||
                       instr->value->kind == IR_VALUE_JSON_STRING_DECODE ||
                       instr->value->kind == IR_VALUE_JSON_STRING_FIELD ||
                       instr->value->kind == IR_VALUE_JSON_WRITE_STRING ||
                       instr->value->kind == IR_VALUE_JSON_WRITE_RUNTIME ||
                       instr->value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN ||
                       instr->value->kind == IR_VALUE_HTTP_WRITE_JSON_RESPONSE ||
                       instr->value->kind == IR_VALUE_FMT_BOOL ||
                       instr->value->kind == IR_VALUE_FMT_HEX_U32 ||
                       instr->value->kind == IR_VALUE_FMT_I32 ||
                       instr->value->kind == IR_VALUE_FMT_U32 ||
                       instr->value->kind == IR_VALUE_FMT_USIZE ||
                       instr->value->kind == IR_VALUE_ARGS_VALUE_AFTER ||
                       instr->value->kind == IR_VALUE_STR_RUNTIME)) {
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
  if (instr->value->kind == IR_VALUE_VEC_GET ||
      instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
      instr->value->kind == IR_VALUE_PARSE_I32 || instr->value->kind == IR_VALUE_PARSE_U32 || instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR || instr->value->kind == IR_VALUE_ARGS_PARSE_U32 || instr->value->kind == IR_VALUE_ARGS_FIND ||
      instr->value->kind == IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32 ||
      instr->value->kind == IR_VALUE_ASCII_RUNTIME || instr->value->kind == IR_VALUE_TEXT_RUNTIME || instr->value->kind == IR_VALUE_MATH_RUNTIME ||
      instr->value->kind == IR_VALUE_TERM_RUNTIME ||
      instr->value->kind == IR_VALUE_RAND_NEXT_BELOW || instr->value->kind == IR_VALUE_RAND_RANGE_U32 ||
      instr->value->kind == IR_VALUE_PROC_CAPTURE ||
      instr->value->kind == IR_VALUE_PROC_CHILD_IO ||
      instr->value->kind == IR_VALUE_FS_READ_BYTES_PATH ||
      instr->value->kind == IR_VALUE_FS_READ_BYTES_AT_PATH || instr->value->kind == IR_VALUE_FS_WRITE_BYTES_PATH ||
      instr->value->kind == IR_VALUE_FS_APPEND_BYTES_PATH) {
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
  if (!macho_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
  if (instr->value->kind == IR_VALUE_VEC_GET ||
      instr->value->kind == IR_VALUE_PARSE_RUNTIME ||
      instr->value->kind == IR_VALUE_PARSE_I32 || instr->value->kind == IR_VALUE_PARSE_U32 || instr->value->kind == IR_VALUE_JSON_LOOKUP_SCALAR || instr->value->kind == IR_VALUE_ARGS_PARSE_U32 || instr->value->kind == IR_VALUE_ARGS_FIND ||
      instr->value->kind == IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32 ||
      instr->value->kind == IR_VALUE_ASCII_RUNTIME || instr->value->kind == IR_VALUE_TEXT_RUNTIME || instr->value->kind == IR_VALUE_MATH_RUNTIME ||
      instr->value->kind == IR_VALUE_TERM_RUNTIME ||
      instr->value->kind == IR_VALUE_RAND_NEXT_BELOW || instr->value->kind == IR_VALUE_RAND_RANGE_U32 ||
      instr->value->kind == IR_VALUE_PROC_CAPTURE ||
      instr->value->kind == IR_VALUE_PROC_CHILD_IO ||
      instr->value->kind == IR_VALUE_FS_READ_BYTES_PATH ||
      instr->value->kind == IR_VALUE_FS_READ_BYTES_AT_PATH || instr->value->kind == IR_VALUE_FS_WRITE_BYTES_PATH ||
      instr->value->kind == IR_VALUE_FS_APPEND_BYTES_PATH) {
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
    } else if (instr->value->kind == IR_VALUE_HTTP_REQUEST_METHOD_NAME ||
               instr->value->kind == IR_VALUE_HTTP_REQUEST_PATH ||
               instr->value->kind == IR_VALUE_JSON_FIELD ||
               instr->value->kind == IR_VALUE_JSON_STRING_DECODE ||
               instr->value->kind == IR_VALUE_JSON_STRING_FIELD ||
               instr->value->kind == IR_VALUE_JSON_WRITE_STRING ||
               instr->value->kind == IR_VALUE_JSON_WRITE_RUNTIME ||
               instr->value->kind == IR_VALUE_HTTP_REQUEST_BODY_WITHIN ||
               instr->value->kind == IR_VALUE_HTTP_WRITE_JSON_RESPONSE ||
               instr->value->kind == IR_VALUE_FMT_BOOL ||
               instr->value->kind == IR_VALUE_FMT_HEX_U32 ||
               instr->value->kind == IR_VALUE_FMT_I32 ||
               instr->value->kind == IR_VALUE_FMT_U32 ||
               instr->value->kind == IR_VALUE_FMT_USIZE ||
               instr->value->kind == IR_VALUE_ARGS_VALUE_AFTER ||
               instr->value->kind == IR_VALUE_STR_RUNTIME) {
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

static bool macho_emit_field_store_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->local_index >= fun->local_len) return macho_diag_at(diag, "direct AArch64 Mach-O field store record is out of range", instr->line, instr->column, "invalid record local");
  const IrLocal *record_local = &fun->locals[instr->local_index];
  if (record_local->is_record_ref) {
    if (instr->value && instr->value->type == IR_TYPE_BYTE_VIEW) {
      if (!macho_emit_byte_view_pair(text, fun, instr->value, 8, 9, frame_size, ctx, diag)) return false;
      macho_emit_load_local_x(text, fun, 10, instr->local_index, 0, frame_size);
      macho_emit_store_field_indirect(text, 8, 10, instr->field_offset, IR_TYPE_U64);
      macho_emit_store_field_indirect(text, 9, 10, instr->field_offset + 8, IR_TYPE_U32);
      return true;
    }
    if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
    macho_emit_load_local_x(text, fun, 10, instr->local_index, 0, frame_size);
    macho_emit_store_field_indirect(text, 8, 10, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32);
    return true;
  }
  if (!record_local->is_record) return macho_diag_at(diag, "direct AArch64 Mach-O field store requires record local", instr->line, instr->column, "non-record local");
  if (instr->value && instr->value->type == IR_TYPE_BYTE_VIEW) {
    if (!macho_emit_byte_view_pair(text, fun, instr->value, 8, 9, frame_size, ctx, diag)) return false;
    macho_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, IR_TYPE_U64, frame_size);
    macho_emit_store_field(text, fun, 9, instr->local_index, instr->field_offset + 8, IR_TYPE_U32, frame_size);
    return true;
  }
  if (!macho_emit_value_to_reg(text, fun, instr->value, 8, frame_size, ctx, diag)) return false;
  macho_emit_store_field(text, fun, 8, instr->local_index, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32, frame_size);
  return true;
}

static bool macho_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return macho_emit_world_write(text, fun, instr, frame_size, ctx, diag);
  }
  if (instr->kind == IR_INSTR_LOCAL_SET) return macho_emit_local_set(text, fun, instr, frame_size, ctx, diag);
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    return macho_emit_field_store_instr(text, fun, instr, frame_size, ctx, diag);
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
      if (!macho_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
    if (!macho_emit_u32_bounds_check(text, 8, 9, ctx, diag)) return false;
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
    ZDirectLoopFrame frame = {.continue_target = loop_start};
    ZDirectLoopFrame *parent = ctx->loop;
    ctx->loop = &frame;
    bool body_ok = macho_emit_instrs(text, fun, instr->then_instrs, instr->then_len, frame_size, restore_process_args, ctx, diag);
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
    if (!ctx->loop) return macho_diag_at(diag, "direct AArch64 Mach-O break or continue requires an enclosing loop", instr->line, instr->column, instr->kind == IR_INSTR_BREAK ? "break" : "continue");
    size_t patch = z_aarch64_emit_b_placeholder(text);
    if (instr->kind == IR_INSTR_CONTINUE) z_aarch64_patch_branch26(text, patch, ctx->loop->continue_target);
    else if (!z_direct_loop_frame_add_break(ctx->loop, patch)) return macho_diag_at(diag, "direct AArch64 Mach-O break patch list allocation failed", instr->line, instr->column, "out of memory");
    return true;
  }
  char actual[64];
  snprintf(actual, sizeof(actual), "unsupported instruction kind %d", instr ? (int)instr->kind : -1);
  return macho_diag_at(diag, "direct AArch64 Mach-O instruction kind is unsupported", instr->line, instr->column, actual);
}

// Emits a register-only fill loop that writes `run->count` copies of the
// constant `run->fill_value` into the fixed-array local, instead of one
// unrolled store-with-bounds-check per element. x9 carries the running
// element pointer, x8 the remaining count, x10 the fill value.
static void macho_emit_fill_run(ZBuf *text, const IrFunction *fun, const ZDirectFillRun *run, unsigned frame_size) {
  unsigned shift = macho_type_index_shift(run->element_type);
  unsigned elem_size = 1u << shift;
  z_aarch64_emit_add_x_sp_imm(text, 9, macho_local_slot_offset(fun, run->array_index, 0, frame_size));
  z_aarch64_emit_movz_x(text, 8, 0);                  // running index
  z_aarch64_emit_movz_x(text, 11, run->count);        // element count
  z_aarch64_emit_movz_x(text, 10, run->fill_value);   // fill value
  size_t loop = text->len;
  macho_emit_store_ptr_element(text, 10, 9, run->element_type);
  z_aarch64_emit_add_x_imm(text, 9, 9, elem_size);
  z_aarch64_emit_add_x_imm(text, 8, 8, 1);
  z_aarch64_emit_cmp_x(text, 8, 11);
  size_t back = z_aarch64_emit_b_cond_placeholder(text, 1); // b.ne -> loop
  z_aarch64_patch_cond19(text, back, loop);
}

static bool macho_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, unsigned frame_size, bool restore_process_args, MachOEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    ZDirectFillRun run;
    if (z_direct_detect_fill_run(fun, instrs, len, i, MACHO_FILL_RUN_MIN, &run)) {
      macho_emit_fill_run(text, fun, &run, frame_size);
      i += run.count - 1;
      continue;
    }
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

static void macho_append_trap_messages(ZBuf *rodata, unsigned base_offset, ZDirectTrapMessages *messages) {
  for (unsigned kind = 0; kind < Z_DIRECT_TRAP_KIND_COUNT; kind++) {
    const char *text = z_direct_trap_message((ZDirectTrapKind)kind);
    size_t len = strlen(text);
    messages->offsets[kind] = base_offset + (unsigned)rodata->len;
    messages->lens[kind] = (unsigned)len;
    append_bytes(rodata, text, len);
  }
}

typedef struct {
  ZBuf text, rodata, relocs, strings;
  size_t *offsets;
  uint32_t *function_string_offsets, *external_string_offsets, runtime_string_offsets[MACHO_RUNTIME_HELPER_COUNT];
  ZMachOSymbol *symbols;
  size_t symbol_len;
  uint32_t symbol_count, rodata_string_offset;
  bool has_rodata;
  unsigned rodata_base_offset;
  MachOEmitContext ctx;
  ZDirectTrapMessages trap_messages;
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
  free(build->external_string_offsets);
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
  build->has_rodata = true;
  build->rodata_base_offset = macho_rodata_base_offset(program);
  macho_append_rodata(&build->rodata, program, build->rodata_base_offset);
  macho_append_trap_messages(&build->rodata, build->rodata_base_offset, &build->trap_messages);
  build->offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  build->function_string_offsets = z_checked_calloc(program->function_len, sizeof(uint32_t));
  build->external_string_offsets = program->external_function_len > 0 ? z_checked_calloc(program->external_function_len, sizeof(uint32_t)) : NULL;
  if (!build->offsets || !build->function_string_offsets || (program->external_function_len > 0 && !build->external_string_offsets)) {
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
    .seed_main_process_args = true,
    .trap_messages = build->trap_messages
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
  return macho_emit_trap_stubs(&build->text, &build->ctx, diag);
}

static void macho_object_append_relocations(MachOObjectBuild *build, const IrProgram *program) {
  if (build->has_rodata) z_macho_append_data_relocations(&build->relocs, &build->ctx, (unsigned)program->function_len);
  uint32_t next_symbol = (uint32_t)program->function_len + (build->has_rodata ? 1u : 0u);
  for (unsigned helper = 0; helper < MACHO_RUNTIME_HELPER_COUNT; helper++) {
    MachORuntimeHelper runtime_helper = (MachORuntimeHelper)helper;
    if (z_macho_runtime_patch_count(&build->ctx, runtime_helper) == 0) continue;
    z_macho_append_runtime_relocations(&build->relocs, &build->ctx, runtime_helper, next_symbol++);
  }
  z_macho_append_call_relocations(&build->relocs, &build->ctx, next_symbol);
  next_symbol += (uint32_t)program->external_function_len;
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
  for (size_t i = 0; i < build->ctx.program->external_function_len; i++) {
    build->external_string_offsets[i] = (uint32_t)build->strings.len;
    zbuf_append_char(&build->strings, '_');
    zbuf_append(&build->strings, build->ctx.program->external_functions[i].symbol ? build->ctx.program->external_functions[i].symbol : "zero_external");
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
  for (size_t i = 0; i < program->external_function_len; i++) {
    build->symbols[build->symbol_len++] = (ZMachOSymbol){ .string_offset = build->external_string_offsets[i], .type = 0x01 };
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
  ZDirectTrapMessages trap_messages;
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
  build->has_rodata = true;
  build->rodata_base_offset = macho_rodata_base_offset(program);
  macho_append_rodata(&build->rodata, program, build->rodata_base_offset);
  macho_append_trap_messages(&build->rodata, build->rodata_base_offset, &build->trap_messages);
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
    .pie_relative_data = true,
    .trap_messages = build->trap_messages
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
  return macho_emit_trap_stubs(&build->text, &build->ctx, diag);
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
  if (!z_direct_exe_reject_c_import_calls(program, diag, "AArch64 Mach-O")) return false;
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
