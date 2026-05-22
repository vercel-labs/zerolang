#include "zero.h"
#include "elf_emit_state.h"
#include "elf_format.h"
#include "x64_emit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool elf_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  diag->code = 4004;
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->expected, sizeof(diag->expected), "direct ELF64 object MVP subset");
  if (actual) snprintf(diag->actual, sizeof(diag->actual), "%s", actual);
  snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported primitive integer arithmetic functions");
  return false;
}

static bool elf_ir_diag(ZDiag *diag, const IrProgram *ir) {
  diag->code = 4004;
  diag->line = ir && ir->mir_line > 0 ? ir->mir_line : 1;
  diag->column = ir && ir->mir_column > 0 ? ir->mir_column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", ir && ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed");
  snprintf(diag->expected, sizeof(diag->expected), "direct ELF64 object MVP subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", ir && ir->mir_actual[0] ? ir->mir_actual : "unsupported construct");
  snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported primitive integer arithmetic functions");
  if (ir) z_diag_set_backend_blocker(diag, &ir->backend_blocker);
  return false;
}

static bool elf_type_is_scalar(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_I32 || type == IR_TYPE_U32;
}

static bool elf_type_is_i64(IrTypeKind type) {
  return type == IR_TYPE_I64 || type == IR_TYPE_U64;
}

static bool elf_type_is_supported_scalar(IrTypeKind type) {
  return elf_type_is_scalar(type) || elf_type_is_i64(type);
}

static bool elf_type_is_unsigned(IrTypeKind type) {
  return type == IR_TYPE_U8 || type == IR_TYPE_U16 || type == IR_TYPE_USIZE || type == IR_TYPE_U32 || type == IR_TYPE_U64;
}

static const char *elf_type_name(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_VOID: return "Void";
    case IR_TYPE_BOOL: return "Bool";
    case IR_TYPE_U8: return "u8";
    case IR_TYPE_U16: return "u16";
    case IR_TYPE_USIZE: return "usize";
    case IR_TYPE_I32: return "i32";
    case IR_TYPE_U32: return "u32";
    case IR_TYPE_I64: return "i64";
    case IR_TYPE_U64: return "u64";
    case IR_TYPE_MAYBE_SCALAR: return "Maybe<usize>";
    default: return "unsupported";
  }
}

static unsigned elf_local_offset(const IrFunction *fun, unsigned local_index) {
  if (fun && local_index < fun->local_len && fun->locals[local_index].frame_offset > 0) return fun->locals[local_index].frame_offset;
  return (local_index + 1) * 8;
}

static void elf_emit_load_local_rax(ZBuf *code, const IrFunction *fun, unsigned local_index) {
  bool wide = fun && local_index < fun->local_len && elf_type_is_i64(fun->locals[local_index].type);
  z_x64_emit_rbp_disp_reg(code, 0x8b, 0, elf_local_offset(fun, local_index), wide);
}

static void elf_emit_store_local_from_reg(ZBuf *code, const IrFunction *fun, unsigned local_index, unsigned reg) {
  bool wide = fun && local_index < fun->local_len && elf_type_is_i64(fun->locals[local_index].type);
  z_x64_emit_rbp_disp_reg(code, 0x89, reg, elf_local_offset(fun, local_index), wide);
}

static unsigned elf_record_field_disp(const IrLocal *local, unsigned field_offset) {
  if (!local || field_offset > local->frame_offset) return local ? local->frame_offset : 0;
  return local->frame_offset - field_offset;
}

static void elf_emit_load_field_rax(ZBuf *code, const IrLocal *local, unsigned field_offset, IrTypeKind type) {
  unsigned disp = elf_record_field_disp(local, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_append_u8(code, 0x0f);
    z_x64_emit_rbp_disp_reg(code, 0xb6, 0, disp, false);
  } else if (elf_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(code, 0x8b, 0, disp, true);
  } else {
    z_x64_emit_rbp_disp_reg(code, 0x8b, 0, disp, false);
  }
}

static void elf_emit_store_field_from_rax(ZBuf *code, const IrLocal *local, unsigned field_offset, IrTypeKind type) {
  unsigned disp = elf_record_field_disp(local, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    z_x64_emit_rbp_disp_reg(code, 0x88, 0, disp, false);
  } else if (elf_type_is_i64(type)) {
    z_x64_emit_rbp_disp_reg(code, 0x89, 0, disp, true);
  } else {
    z_x64_emit_rbp_disp_reg(code, 0x89, 0, disp, false);
  }
}

static unsigned elf_type_byte_size(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 1;
  if (elf_type_is_i64(type)) return 8;
  return 4;
}

static void elf_emit_lea_array_base_rax(ZBuf *code, const IrLocal *local) {
  z_x64_emit_rbp_disp_reg(code, 0x8d, 0, local->frame_offset, true);
}

static void elf_emit_scale_index_into_rax(ZBuf *code, IrTypeKind element_type) {
  z_x64_emit_lea_base_index_scale_disp_reg(code, 0, 0, 1, elf_type_byte_size(element_type), 0);
}

static unsigned elf_setcc_opcode(IrCompareOp op, bool uns) {
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

static ElfRuntimeHelper elf_runtime_helper_for_value(IrValueKind kind) {
  switch (kind) {
    case IR_VALUE_HTTP_RESULT_OK: return ELF_RUNTIME_HTTP_RESULT_OK;
    case IR_VALUE_HTTP_RESULT_STATUS: return ELF_RUNTIME_HTTP_RESULT_STATUS;
    case IR_VALUE_HTTP_RESULT_BODY_LEN: return ELF_RUNTIME_HTTP_RESULT_BODY_LEN;
    case IR_VALUE_HTTP_RESULT_ERROR: return ELF_RUNTIME_HTTP_RESULT_ERROR;
    case IR_VALUE_HTTP_RESPONSE_LEN: return ELF_RUNTIME_HTTP_RESPONSE_LEN;
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN: return ELF_RUNTIME_HTTP_RESPONSE_HEADERS_LEN;
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: return ELF_RUNTIME_HTTP_RESPONSE_BODY_OFFSET;
    case IR_VALUE_HTTP_HEADER_VALUE: return ELF_RUNTIME_HTTP_HEADER_VALUE;
    case IR_VALUE_HTTP_HEADER_FOUND: return ELF_RUNTIME_HTTP_HEADER_FOUND;
    case IR_VALUE_HTTP_HEADER_OFFSET: return ELF_RUNTIME_HTTP_HEADER_OFFSET;
    case IR_VALUE_HTTP_HEADER_LEN: return ELF_RUNTIME_HTTP_HEADER_LEN;
    default: return ELF_RUNTIME_HELPER_COUNT;
  }
}

static bool elf_emit_value(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag);
static bool elf_emit_read_all_or_raise_to_local(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag);

static bool elf_function_propagates_to_process_exit(const IrFunction *fun) {
  return fun && (fun->raises ||
                 (fun->is_exported &&
                  fun->name && strcmp(fun->name, "main") == 0 &&
                  fun->return_type == IR_TYPE_I32 &&
                  fun->value_return_type == IR_TYPE_VOID));
}

static bool elf_function_seeds_process_args(const IrFunction *fun, const ElfEmitContext *ctx) {
  return ctx && ctx->seed_main_process_args &&
         fun && fun->is_exported && fun->name && strcmp(fun->name, "main") == 0;
}

static unsigned elf_base_stack_size(const IrFunction *fun) {
  return (unsigned)z_elf_align(fun ? fun->frame_bytes : 0, 16);
}

static unsigned elf_total_stack_size(const IrFunction *fun, const ElfEmitContext *ctx) {
  unsigned base = elf_base_stack_size(fun);
  return base + (elf_function_seeds_process_args(fun, ctx) ? 32u : 0u);
}

static void elf_emit_epilogue(ZBuf *code, const IrFunction *fun, const ElfEmitContext *ctx) {
  if (elf_function_seeds_process_args(fun, ctx)) {
    unsigned base = elf_base_stack_size(fun);
    z_x64_emit_rbp_disp_reg(code, 0x8b, 13, base + 8, true);
    z_x64_emit_rbp_disp_reg(code, 0x8b, 14, base + 16, true);
    z_x64_emit_rbp_disp_reg(code, 0x8b, 15, base + 24, true);
  }
  z_x64_emit_epilogue(code);
}

static void elf_emit_push_rax(ZBuf *code) {
  z_x64_emit_push_rax(code);
}

static void elf_emit_store_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide);
static void elf_emit_store_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset);
static bool elf_emit_byte_view_ptr(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);

static void elf_emit_strlen_rax_to_ecx(ZBuf *code) {
  z_x64_emit_mov_rdx_from_rax(code);
  z_x64_emit_xor_ecx_ecx(code);
  size_t loop = code->len;
  z_x64_emit_cmp_base_index_u8(code, 2, 1, 0);
  size_t done = z_x64_emit_jcc32_placeholder(code, 0x84);
  z_x64_emit_inc_ecx(code);
  size_t back = z_x64_emit_jmp32_placeholder(code, 0xe9);
  z_x64_patch_rel32(code, back, loop);
  z_x64_patch_rel32(code, done, code->len);
}

static void elf_emit_maybe_clear(ZBuf *code, const IrLocal *local) {
  z_x64_emit_xor_eax_eax(code);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  elf_emit_store_local_slot_reg(code, local, 8, 0, true);
  elf_emit_store_local_slot_reg(code, local, 16, 0, false);
}

static void elf_emit_maybe_scalar_clear(ZBuf *code, const IrLocal *local) {
  z_x64_emit_xor_eax_eax(code);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  elf_emit_store_local_slot_reg(code, local, 8, 0, true);
}

static void elf_emit_maybe_scalar_store_rax(ZBuf *code, const IrLocal *local) {
  z_x64_emit_push_rax(code);
  z_x64_emit_mov_eax_u32(code, 1);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  z_x64_emit_pop_rax(code);
  elf_emit_store_local_slot_rax(code, local, 8);
}

static size_t elf_emit_js_placeholder(ZBuf *code) {
  z_x64_append_u8(code, 0x0f);
  z_x64_append_u8(code, 0x88);
  size_t patch = code->len;
  z_x64_append_u32(code, 0);
  return patch;
}

static bool elf_emit_openat_path(ZBuf *code, const IrFunction *fun, const IrValue *path, unsigned flags, unsigned mode, ElfEmitContext *ctx, ZDiag *diag) {
  if (!elf_emit_byte_view_ptr(code, fun, path, ctx, diag)) return false;
  z_x64_emit_mov_rsi_from_rax(code);
  z_x64_emit_mov_reg_u64(code, 7, 0xffffffffffffff9cULL);
  z_x64_emit_mov_reg_u32(code, 2, flags);
  z_x64_emit_mov_reg_u32(code, 10, mode);
  z_x64_emit_mov_eax_u32(code, 257);
  z_x64_emit_syscall(code);
  return true;
}

static void elf_emit_close_rax_fd(ZBuf *code) {
  z_x64_emit_mov_rdi_from_rax(code);
  z_x64_emit_mov_eax_u32(code, 3);
  z_x64_emit_syscall(code);
}

static bool elf_emit_bounds_checked_address(ZBuf *code, const IrFunction *fun, const IrLocal *local, const IrValue *index, ElfEmitContext *ctx, ZDiag *diag) {
  if (!local || !local->is_array) return elf_diag(diag, "direct ELF64 indexed access requires fixed array local", index ? index->line : 1, index ? index->column : 1, "non-array local");
  if (!elf_emit_value(code, fun, index, ctx, diag)) return false;
  z_x64_append_u8(code, 0x3d);
  z_x64_append_u32(code, local->array_len);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(code, 0x82);
  z_x64_emit_ud2(code);
  z_x64_patch_rel32(code, ok_patch, code->len);
  z_x64_emit_mov_rcx_from_rax(code, false);
  elf_emit_lea_array_base_rax(code, local);
  elf_emit_scale_index_into_rax(code, local->element_type);
  return true;
}

static bool elf_const_u32_value(const IrValue *value, unsigned *out) {
  if (!value || value->kind != IR_VALUE_INT) return false;
  if (value->int_value > UINT32_MAX) return false;
  if (out) *out = (unsigned)value->int_value;
  return true;
}

static bool elf_readonly_data_byte(const IrProgram *ir, unsigned offset, unsigned char *out) {
  if (!ir) return false;
  for (size_t i = 0; i < ir->data_segment_len; i++) {
    const IrDataSegment *segment = &ir->data_segments[i];
    if (offset >= segment->offset && offset < segment->offset + segment->len) {
      if (out) *out = segment->bytes[offset - segment->offset];
      return true;
    }
  }
  return false;
}

static bool elf_byte_view_const_len(const IrFunction *fun, const IrValue *view, unsigned *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL || view->kind == IR_VALUE_ARRAY_BYTE_VIEW) {
    if (out) *out = view->data_len;
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!elf_byte_view_const_len(fun, view->left, &base_len)) return false;
    unsigned start = 0;
    unsigned end = base_len;
    if (view->index && !elf_const_u32_value(view->index, &start)) return false;
    if (view->right && !elf_const_u32_value(view->right, &end)) return false;
    if (start > end || end > base_len) return false;
    if (out) *out = end - start;
    return true;
  }
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    return false;
  }
  return false;
}

static bool elf_byte_view_const_byte(const IrProgram *ir, const IrFunction *fun, const IrValue *view, unsigned index, unsigned char *out) {
  if (!view) return false;
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    if (index >= view->data_len) return false;
    return elf_readonly_data_byte(ir, view->data_offset + index, out);
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    unsigned base_len = 0;
    if (!elf_byte_view_const_len(fun, view, &base_len) || index >= base_len) return false;
    unsigned start = 0;
    if (view->index && !elf_const_u32_value(view->index, &start)) return false;
    return elf_byte_view_const_byte(ir, fun, view->left, start + index, out);
  }
  return false;
}

static void elf_emit_error_condition_from_rax(ZBuf *code) {
  z_x64_emit_mov_rcx_from_rax(code, true);
  z_x64_emit_shr_rcx_imm8(code, 32);
  z_x64_emit_test_ecx_ecx(code);
}

static void elf_emit_packed_error_rax(ZBuf *code, unsigned code_value) {
  z_x64_emit_mov_rax_u64(code, ((uint64_t)code_value) << 32);
}

static bool elf_emit_rodata_ptr_rax(ZBuf *code, unsigned data_offset, ElfEmitContext *ctx, ZDiag *diag, const IrValue *value) {
  unsigned compact_offset = ctx ? data_offset - ctx->rodata_base_offset : data_offset;
  size_t imm_offset = z_x64_emit_mov_rax_u64_patchable(code, ctx && ctx->emit_rodata_relocations ? 0 : (ctx ? ctx->rodata_addr : 0) + compact_offset);
  return z_elf_record_rodata_patch(ctx, imm_offset, data_offset, diag, value);
}

static void elf_emit_store_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  z_x64_emit_rbp_disp_reg(code, 0x89, 0, disp, true);
}

static void elf_emit_store_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  z_x64_emit_rbp_disp_reg(code, 0x89, reg, disp, wide);
}

static void elf_emit_load_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  z_x64_emit_rbp_disp_reg(code, 0x8b, 0, disp, true);
}

static void elf_emit_load_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  z_x64_emit_rbp_disp_reg(code, 0x8b, reg, disp, wide);
}

static bool elf_emit_byte_view_len(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);
static bool elf_emit_byte_view_ptr(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);

static bool elf_emit_json_parse_bytes_call(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag) {
  if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
  elf_emit_push_rax(code);
  if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
  elf_emit_push_rax(code);
  z_x64_emit_pop_reg64(code, 6);
  z_x64_emit_pop_reg64(code, 7);
  size_t patch = z_x64_emit_call32_placeholder(code);
  return z_elf_record_value_runtime_patch(ctx, ELF_RUNTIME_JSON_PARSE_BYTES, patch, diag, value);
}

static bool elf_emit_byte_view_len(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag) {
  unsigned len = 0;
  if (elf_byte_view_const_len(fun, view, &len)) {
    z_x64_emit_mov_eax_u32(code, len);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->index && view->right) {
    unsigned start = 0;
    unsigned end = 0;
    if (elf_const_u32_value(view->index, &start) && elf_const_u32_value(view->right, &end) && start <= end) {
      z_x64_emit_mov_eax_u32(code, end - start);
      return true;
    }
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->right) {
    unsigned start = 0;
    if ((!view->index || elf_const_u32_value(view->index, &start)) &&
        !elf_const_u32_value(view->right, NULL)) {
      if (!elf_emit_value(code, fun, view->right, ctx, diag)) return false;
      if (start > 0) {
        z_x64_emit_sub_rax_u32(code, start, true);
      }
      return true;
    }
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->index && view->right) {
    if (!elf_emit_value(code, fun, view->index, ctx, diag)) return false;
    elf_emit_push_rax(code);
    if (!elf_emit_value(code, fun, view->right, ctx, diag)) return false;
    z_x64_emit_pop_reg64(code, 1);
    z_x64_emit_sub_reg_reg(code, 0, 1, true);
    return true;
  }
  if (view && view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    elf_emit_load_local_slot_rax(code, &fun->locals[view->local_index], 8);
    return true;
  }
  if (view && view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    elf_emit_load_local_slot_reg(code, &fun->locals[view->local_index], 16, 0, false);
    return true;
  }
  (void)ctx;
  return elf_diag(diag, "direct ELF64 byte-view length currently requires a literal, constant slice, fixed byte array, or byte-view local", view ? view->line : 1, view ? view->column : 1, "unsupported byte view length");
}

static bool elf_emit_byte_view_ptr(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag) {
  if (!view) return elf_diag(diag, "direct ELF64 byte view is missing", 1, 1, "missing byte view");
  if (view->kind == IR_VALUE_LOCAL && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_BYTE_VIEW) {
    elf_emit_load_local_slot_rax(code, &fun->locals[view->local_index], 0);
    return true;
  }
  if (view->kind == IR_VALUE_MAYBE_VALUE && view->local_index < fun->local_len && fun->locals[view->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
    elf_emit_load_local_slot_rax(code, &fun->locals[view->local_index], 8);
    return true;
  }
  if (view->kind == IR_VALUE_STRING_LITERAL) {
    return elf_emit_rodata_ptr_rax(code, view->data_offset, ctx, diag, view);
  }
  if (view->kind == IR_VALUE_ARRAY_BYTE_VIEW && view->array_index < fun->local_len) {
    const IrLocal *local = &fun->locals[view->array_index];
    if (!local->is_array || local->element_type != IR_TYPE_U8) return elf_diag(diag, "direct ELF64 byte-view array requires [N]u8", view->line, view->column, "non-u8 array view");
    elf_emit_lea_array_base_rax(code, local);
    return true;
  }
  if (view->kind == IR_VALUE_BYTE_SLICE) {
    if (!elf_emit_byte_view_ptr(code, fun, view->left, ctx, diag)) return false;
    z_x64_emit_push_rax(code);
    if (view->index) {
      if (!elf_emit_value(code, fun, view->index, ctx, diag)) return false;
    } else {
      z_x64_emit_mov_eax_u32(code, 0);
    }
    z_x64_emit_pop_reg64(code, 1);
    z_x64_emit_add_rax_rcx(code, true);
    return true;
  }
  return elf_diag(diag, "direct ELF64 value is not a supported byte view", view->line, view->column, "unsupported byte view");
}

static bool elf_emit_value(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag) {
  if (!value) return elf_diag(diag, "direct ELF64 expression is missing", 1, 1, "missing expression");
  if (!elf_type_is_supported_scalar(value->type) && !((value->kind == IR_VALUE_CALL || value->kind == IR_VALUE_CHECK) && value->type == IR_TYPE_VOID) &&
      value->kind != IR_VALUE_MAYBE_HAS && value->kind != IR_VALUE_VEC_LEN && value->kind != IR_VALUE_VEC_CAPACITY &&
      value->kind != IR_VALUE_VEC_PUSH && value->kind != IR_VALUE_ARGS_LEN &&
      value->type != IR_TYPE_MAYBE_SCALAR && value->kind != IR_VALUE_FS_CLOSE_FILE) {
    return elf_diag(diag, "direct ELF64 object backend currently supports only primitive integer values", value->line, value->column, elf_type_name(value->type));
  }
  switch (value->kind) {
    case IR_VALUE_BOOL:
    case IR_VALUE_INT:
      if (elf_type_is_i64(value->type)) {
        z_x64_emit_mov_rax_u64(code, (uint64_t)value->int_value);
      } else {
        z_x64_emit_mov_eax_u32(code, (uint32_t)value->int_value);
      }
      return true;
    case IR_VALUE_LOCAL:
      if (value->local_index >= fun->local_len) {
        return elf_diag(diag, "direct ELF64 local index is out of range", value->line, value->column, "invalid local");
      }
      if (fun->locals[value->local_index].is_array) {
        return elf_diag(diag, "direct ELF64 cannot use fixed array locals as scalar values", value->line, value->column, "array local");
      }
      elf_emit_load_local_rax(code, fun, value->local_index);
      return true;
    case IR_VALUE_BINARY: {
      bool wide = elf_type_is_i64(value->type);
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(code, wide);
      z_x64_emit_pop_rax(code);
      if (value->binary_op == IR_BIN_ADD) {
        z_x64_emit_add_rax_rcx(code, wide);
      } else if (value->binary_op == IR_BIN_SUB) {
        z_x64_emit_sub_rax_rcx(code, wide);
      } else if (value->binary_op == IR_BIN_MUL) {
        z_x64_emit_imul_rax_rcx(code, wide);
      } else if (value->binary_op == IR_BIN_AND) {
        z_x64_emit_and_rax_rcx(code, wide);
      } else if (value->binary_op == IR_BIN_OR) {
        z_x64_emit_or_rax_rcx(code, wide);
      } else if (value->binary_op == IR_BIN_DIV) {
        z_x64_emit_div_rax_rcx(code, wide, elf_type_is_unsigned(value->type), false);
      } else if (value->binary_op == IR_BIN_MOD) {
        z_x64_emit_div_rax_rcx(code, wide, elf_type_is_unsigned(value->type), true);
      } else {
        return elf_diag(diag, "direct ELF64 binary operator is unsupported", value->line, value->column, "unsupported operator");
      }
      return true;
    }
    case IR_VALUE_COMPARE: {
      if (!value->left || !value->right || !elf_type_is_supported_scalar(value->left->type) || value->left->type != value->right->type) {
        return elf_diag(diag, "direct ELF64 comparison operands must have the same supported integer type", value->line, value->column, "unsupported comparison");
      }
      bool wide = elf_type_is_i64(value->left->type);
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(code, wide);
      z_x64_emit_pop_rax(code);
      z_x64_emit_cmp_rax_rcx_to_bool(code, elf_setcc_opcode(value->compare_op, elf_type_is_unsigned(value->left->type)), wide);
      return true;
    }
    case IR_VALUE_CALL: {
      static const unsigned param_regs[] = {7, 6, 2, 1, 8, 9};
      if (value->arg_len > 6) return elf_diag(diag, "direct ELF64 call supports at most six arguments", value->line, value->column, "too many arguments");
      for (size_t i = 0; i < value->arg_len; i++) {
        if (!elf_emit_value(code, fun, value->args[i], ctx, diag)) return false;
        elf_emit_push_rax(code);
      }
      for (size_t i = value->arg_len; i > 0; i--) {
        z_x64_emit_pop_reg64(code, param_regs[i - 1]);
      }
      size_t patch = z_x64_emit_call32_placeholder(code);
      return z_elf_record_call_patch(ctx, patch, value->callee_index, diag, value);
    }
    case IR_VALUE_JSON_PARSE_BYTES:
      return elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag);
    case IR_VALUE_JSON_VALIDATE_BYTES:
      if (!elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag)) return false;
      z_x64_emit_cmp_reg_i8(code, 0, 0, true);
      z_x64_emit_setcc_al_to_bool(code, 0x9d);
      return true;
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES: {
      if (!elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t ok = z_x64_emit_jcc32_placeholder(code, 0x89);
      z_x64_emit_xor_rax_rax(code);
      z_x64_patch_rel32(code, ok, code->len);
      return true;
    }
    case IR_VALUE_HTTP_FETCH: {
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_value(code, fun, value->index, ctx, diag)) return false;
      elf_emit_push_rax(code);
      z_x64_emit_pop_reg64(code, 8);
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_pop_reg64(code, 2);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      size_t patch = z_x64_emit_call32_placeholder(code);
      return z_elf_record_value_runtime_patch(ctx, ELF_RUNTIME_HTTP_FETCH, patch, diag, value);
    }
    case IR_VALUE_HTTP_RESULT_OK:
    case IR_VALUE_HTTP_RESULT_STATUS:
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
    case IR_VALUE_HTTP_RESULT_ERROR:
    case IR_VALUE_HTTP_HEADER_FOUND:
    case IR_VALUE_HTTP_HEADER_OFFSET:
    case IR_VALUE_HTTP_HEADER_LEN: {
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      z_x64_emit_pop_reg64(code, 7);
      size_t patch = z_x64_emit_call32_placeholder(code);
      return z_elf_record_value_runtime_patch(ctx, elf_runtime_helper_for_value(value->kind), patch, diag, value);
    }
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: {
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      size_t patch = z_x64_emit_call32_placeholder(code);
      return z_elf_record_value_runtime_patch(ctx, elf_runtime_helper_for_value(value->kind), patch, diag, value);
    }
    case IR_VALUE_HTTP_HEADER_VALUE: {
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_emit_push_rax(code);
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_pop_reg64(code, 2);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      size_t patch = z_x64_emit_call32_placeholder(code);
      return z_elf_record_value_runtime_patch(ctx, ELF_RUNTIME_HTTP_HEADER_VALUE, patch, diag, value);
    }
    case IR_VALUE_ARGS_LEN:
      if (ctx && ctx->seed_main_process_args) {
        z_x64_emit_push_reg64(code, 14);
        z_x64_emit_pop_reg64(code, 0);
        return true;
      }
      z_x64_emit_load_reg_ptr_reg(code, 0, 15, true);
      return true;
    case IR_VALUE_TIME_WALL_SECONDS:
      z_x64_emit_xor_rdi_rdi(code);
      z_x64_emit_mov_eax_u32(code, 201);
      z_x64_emit_syscall(code);
      return true;
    case IR_VALUE_TIME_MONOTONIC:
      z_x64_emit_sub_rsp(code, 16);
      z_x64_emit_mov_reg_u32(code, 7, 1);
      z_x64_emit_mov_rsi_from_rsp(code);
      z_x64_emit_mov_eax_u32(code, 228);
      z_x64_emit_syscall(code);
      z_x64_emit_load_rsp_offset_reg(code, 0, 0, true);
      z_x64_emit_imul_reg_i32(code, 0, 1000000000, true);
      z_x64_emit_add_rax_rsp_offset(code, 8);
      z_x64_emit_add_rsp(code, 16);
      return true;
    case IR_VALUE_TIME_AS_MS:
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_reg_i32(code, 1, 1000000);
      z_x64_emit_div_rax_rcx(code, true, false, false);
      return true;
    case IR_VALUE_RAND_NEXT_U32:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      elf_emit_load_local_rax(code, fun, value->local_index);
      z_x64_emit_imul_reg_i32(code, 0, 1664525, false);
      z_x64_emit_add_rax_u32(code, 1013904223u, false);
      elf_emit_store_local_from_reg(code, fun, value->local_index, 0);
      return true;
    case IR_VALUE_RAND_ENTROPY_U32:
      z_x64_emit_xor_rdi_rdi(code);
      z_x64_emit_mov_eax_u32(code, 201);
      z_x64_emit_syscall(code);
      z_x64_append_u8(code, 0x35);
      z_x64_append_u32(code, 0x9e3779b9u);
      return true;
    case IR_VALUE_FS_HOST:
      z_x64_emit_xor_eax_eax(code);
      return true;
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE: {
      bool create = value->kind == IR_VALUE_FS_CREATE;
      if (!elf_emit_openat_path(code, fun, value->left, create ? 577 : 0, create ? 0644 : 0, ctx, diag)) return false;
      if (value->type == IR_TYPE_I64) {
        z_x64_emit_test_rax_rax(code, true);
        size_t fail = elf_emit_js_placeholder(code);
        z_x64_emit_mov_reg_from_reg(code, 0, 0, false);
        size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
        z_x64_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        z_x64_patch_rel32(code, end, code->len);
      }
      return true;
    }
    case IR_VALUE_FS_CLOSE_FILE:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.close local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      elf_emit_close_rax_fd(code);
      return true;
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_IS_DIR: {
      unsigned flags = value->kind == IR_VALUE_FS_IS_DIR ? 65536u : 0u;
      if (!elf_emit_openat_path(code, fun, value->left, flags, 0, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t fail = elf_emit_js_placeholder(code);
      elf_emit_close_rax_fd(code);
      z_x64_emit_mov_eax_u32(code, 1);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, fail, code->len);
      z_x64_emit_mov_eax_u32(code, 0);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_REMOVE:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rdi_from_rax(code);
      z_x64_emit_mov_eax_u32(code, 87);
      z_x64_emit_syscall(code);
      z_x64_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_REMOVE_DIR:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rdi_from_rax(code);
      z_x64_emit_mov_eax_u32(code, 84);
      z_x64_emit_syscall(code);
      z_x64_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_MAKE_DIR:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rdi_from_rax(code);
      z_x64_emit_mov_reg_u32(code, 6, 0755);
      z_x64_emit_mov_eax_u32(code, 83);
      z_x64_emit_syscall(code);
      z_x64_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_RENAME:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rsi_from_rax(code);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_mov_eax_u32(code, 82);
      z_x64_emit_syscall(code);
      z_x64_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_DIR_ENTRY_COUNT: {
      if (!elf_emit_openat_path(code, fun, value->left, 65536, 0, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t open_fail = elf_emit_js_placeholder(code);
      z_x64_emit_sub_rsp(code, 1040);
      z_x64_emit_store_rsp_offset_reg(code, 0, 1024, true);
      z_x64_emit_mov_rsp_offset_u32(code, 1032, 0, true);
      size_t read_loop = code->len;
      z_x64_emit_load_rsp_offset_reg(code, 7, 1024, true);
      z_x64_emit_lea_rsp_offset_reg(code, 6, 0);
      z_x64_emit_mov_reg_u32(code, 2, 1024);
      z_x64_emit_mov_eax_u32(code, 217);
      z_x64_emit_syscall(code);
      z_x64_emit_test_rax_rax(code, true);
      size_t read_fail = elf_emit_js_placeholder(code);
      size_t done = z_x64_emit_jcc32_placeholder(code, 0x84);
      z_x64_emit_mov_reg_from_reg(code, 9, 4, true);
      z_x64_emit_lea_base_index_scale_disp_reg(code, 8, 4, 0, 1, 0);
      size_t scan_loop = code->len;
      z_x64_emit_cmp_reg_reg(code, 9, 8, true);
      size_t scan_done = z_x64_emit_jcc32_placeholder(code, 0x83);
      z_x64_emit_inc_rsp_offset64(code, 1032);
      z_x64_emit_movzx_reg32_ptr_reg_disp_u16(code, 0, 9, 16);
      z_x64_emit_add_reg_reg(code, 9, 0, true);
      size_t scan_back = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, scan_back, scan_loop);
      z_x64_patch_rel32(code, scan_done, code->len);
      size_t loop_back = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, loop_back, read_loop);
      z_x64_patch_rel32(code, done, code->len);
      z_x64_emit_load_rsp_offset_reg(code, 0, 1024, true);
      elf_emit_close_rax_fd(code);
      z_x64_emit_load_rsp_offset_reg(code, 0, 1032, true);
      z_x64_emit_add_rsp(code, 1040);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, read_fail, code->len);
      z_x64_emit_load_rsp_offset_reg(code, 0, 1024, true);
      elf_emit_close_rax_fd(code);
      z_x64_emit_add_rsp(code, 1040);
      z_x64_patch_rel32(code, open_fail, code->len);
      z_x64_emit_mov_reg_i32(code, 0, -1);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_ATOMIC_WRITE: {
      if (!value->left || !value->right || !value->index) return elf_diag(diag, "direct ELF64 std.fs.atomicWrite requires path, temp path, and bytes", value->line, value->column, "missing argument");
      if (!elf_emit_openat_path(code, fun, value->right, 577, 0644, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t open_fail = elf_emit_js_placeholder(code);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->index, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->index, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_load_rsp_offset_reg(code, 6, 8, true);
      z_x64_emit_load_rsp_offset_reg(code, 7, 16, true);
      z_x64_emit_mov_eax_u32(code, 1);
      z_x64_emit_syscall(code);
      z_x64_emit_test_rax_rax(code, true);
      size_t write_fail = elf_emit_js_placeholder(code);
      z_x64_emit_cmp_rax_rsp_offset(code, 0);
      size_t short_write = z_x64_emit_jcc32_placeholder(code, 0x85);
      z_x64_emit_load_rsp_offset_reg(code, 7, 16, true);
      z_x64_emit_mov_eax_u32(code, 3);
      z_x64_emit_syscall(code);
      z_x64_emit_add_rsp(code, 24);
      z_x64_emit_test_rax_rax(code, true);
      size_t close_fail = elf_emit_js_placeholder(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rsi_from_rax(code);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_mov_eax_u32(code, 82);
      z_x64_emit_syscall(code);
      z_x64_emit_bool_from_nonnegative_rax(code);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, write_fail, code->len);
      z_x64_patch_rel32(code, short_write, code->len);
      z_x64_emit_load_rsp_offset_reg(code, 7, 16, true);
      z_x64_emit_mov_eax_u32(code, 3);
      z_x64_emit_syscall(code);
      z_x64_emit_add_rsp(code, 24);
      z_x64_patch_rel32(code, open_fail, code->len);
      z_x64_patch_rel32(code, close_fail, code->len);
      z_x64_emit_mov_eax_u32(code, 0);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_FILE_LEN:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.fileLen local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      z_x64_emit_mov_rdi_from_rax(code);
      z_x64_emit_xor_reg_reg(code, 6, true);
      z_x64_emit_mov_reg_u32(code, 2, 2);
      z_x64_emit_mov_eax_u32(code, 8);
      z_x64_emit_syscall(code);
      if (value->type == IR_TYPE_I64) {
        z_x64_emit_test_rax_rax(code, true);
        size_t fail = elf_emit_js_placeholder(code);
        z_x64_emit_mov_reg_from_reg(code, 0, 0, false);
        size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
        z_x64_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        z_x64_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_READ_FILE:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.read local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_xor_eax_eax(code);
      z_x64_emit_syscall(code);
      if (value->type == IR_TYPE_I64) {
        z_x64_emit_test_rax_rax(code, true);
        size_t fail = elf_emit_js_placeholder(code);
        z_x64_emit_mov_reg_from_reg(code, 0, 0, false);
        size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
        z_x64_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        z_x64_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_WRITE_ALL_FILE:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.writeAll local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_mov_eax_u32(code, 1);
      z_x64_emit_syscall(code);
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_cmp_rax_rcx(code, false);
      z_x64_emit_setcc_al_to_bool(code, 0x94);
      if (value->type == IR_TYPE_I64) {
        z_x64_emit_test_rax_rax(code, false);
        size_t success = z_x64_emit_jcc32_placeholder(code, 0x85);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
        z_x64_patch_rel32(code, success, code->len);
        z_x64_emit_xor_rax_rax(code);
        z_x64_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH: {
      if (!elf_emit_openat_path(code, fun, value->left, 0, 0, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t open_fail = elf_emit_js_placeholder(code);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_xor_eax_eax(code);
      z_x64_emit_syscall(code);
      z_x64_emit_push_rax(code);
      z_x64_emit_mov_rax_from_rdi(code);
      elf_emit_close_rax_fd(code);
      z_x64_emit_pop_rax(code);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, open_fail, code->len);
      z_x64_emit_mov_reg_i32(code, 0, -1);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH: {
      if (!elf_emit_openat_path(code, fun, value->left, 577, 0644, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(code, true);
      size_t open_fail = elf_emit_js_placeholder(code);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_mov_eax_u32(code, 1);
      z_x64_emit_syscall(code);
      z_x64_emit_push_rax(code);
      z_x64_emit_mov_rax_from_rdi(code);
      elf_emit_close_rax_fd(code);
      z_x64_emit_pop_rax(code);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, open_fail, code->len);
      z_x64_emit_mov_reg_i32(code, 0, -1);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_MAYBE_HAS:
      if (value->local_index >= fun->local_len ||
          (fun->locals[value->local_index].type != IR_TYPE_MAYBE_BYTE_VIEW && fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR)) {
        return elf_diag(diag, "direct ELF64 maybe helper requires a Maybe local", value->line, value->column, "invalid maybe local");
      }
      elf_emit_load_local_slot_reg(code, &fun->locals[value->local_index], 0, 0, false);
      return true;
    case IR_VALUE_MAYBE_VALUE:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_MAYBE_SCALAR) return elf_diag(diag, "direct ELF64 maybe scalar value requires a Maybe scalar local", value->line, value->column, "invalid maybe value");
      elf_emit_load_local_slot_rax(code, &fun->locals[value->local_index], 8);
      return true;
    case IR_VALUE_VEC_LEN:
    case IR_VALUE_VEC_CAPACITY:
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return elf_diag(diag, "direct ELF64 Vec helper requires a Vec local", value->line, value->column, "invalid Vec local");
      elf_emit_load_local_slot_reg(code, &fun->locals[value->local_index], value->kind == IR_VALUE_VEC_LEN ? 8 : 12, 0, false);
      return true;
    case IR_VALUE_VEC_PUSH: {
      if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_VEC) return elf_diag(diag, "direct ELF64 Vec push requires a Vec local", value->line, value->column, "invalid Vec local");
      const IrLocal *local = &fun->locals[value->local_index];
      elf_emit_load_local_slot_reg(code, local, 8, 0, false);
      elf_emit_load_local_slot_reg(code, local, 12, 1, false);
      z_x64_emit_cmp_rax_rcx(code, false);
      size_t ok_patch = z_x64_emit_jcc32_placeholder(code, 0x82);
      z_x64_emit_mov_eax_u32(code, 0);
      size_t end_patch = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, ok_patch, code->len);
      z_x64_emit_push_rax(code);
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_pop_reg64(code, 1);
      elf_emit_load_local_slot_reg(code, local, 0, 2, true);
      z_x64_emit_add_rdx_rcx(code, true);
      z_x64_emit_store_ptr_reg8_from_reg(code, 2, 0);
      z_x64_emit_mov_eax_from_ecx(code);
      z_x64_emit_add_reg_i8(code, 0, 1, false);
      elf_emit_store_local_slot_reg(code, local, 8, 0, false);
      z_x64_emit_mov_eax_u32(code, 1);
      z_x64_patch_rel32(code, end_patch, code->len);
      return true;
    }
    case IR_VALUE_CHECK: {
      if (!value->left || value->left->type != IR_TYPE_I64) return elf_diag(diag, "direct ELF64 check requires a packed fallible call result", value->line, value->column, "non-fallible value");
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_emit_error_condition_from_rax(code);
      size_t ok_patch = z_x64_emit_jcc32_placeholder(code, 0x84);
      if (elf_function_propagates_to_process_exit(fun)) {
        elf_emit_epilogue(code, fun, ctx);
      } else {
        z_x64_emit_mov_eax_u32(code, 1);
        elf_emit_epilogue(code, fun, ctx);
      }
      z_x64_patch_rel32(code, ok_patch, code->len);
      if (!elf_type_is_i64(value->type)) {
        z_x64_emit_mov_reg_from_reg(code, 0, 0, false);
      }
      return true;
    }
    case IR_VALUE_RESCUE: {
      if (!value->left || !value->right || value->left->type != IR_TYPE_I64) {
        return elf_diag(diag, "direct ELF64 rescue requires a packed fallible call and fallback", value->line, value->column, "unsupported rescue");
      }
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_emit_error_condition_from_rax(code);
      size_t success_patch = z_x64_emit_jcc32_placeholder(code, 0x84);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      size_t end_patch = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, success_patch, code->len);
      if (!elf_type_is_i64(value->type)) {
        z_x64_emit_mov_reg_from_reg(code, 0, 0, false);
      }
      z_x64_patch_rel32(code, end_patch, code->len);
      return true;
    }
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= fun->local_len) return elf_diag(diag, "direct ELF64 indexed load array is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &fun->locals[value->array_index];
      if (!elf_emit_bounds_checked_address(code, fun, local, value->index, ctx, diag)) return false;
      if (local->element_type == IR_TYPE_U8) {
        z_x64_emit_movzx_reg32_ptr_reg_u8(code, 0, 0);
      } else if (elf_type_is_i64(local->element_type)) {
        z_x64_emit_load_reg_ptr_reg(code, 0, 0, true);
      } else {
        z_x64_emit_load_reg_ptr_reg(code, 0, 0, false);
      }
      return true;
    }
    case IR_VALUE_FIELD_LOAD: {
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 field load record is out of range", value->line, value->column, "invalid record local");
      const IrLocal *local = &fun->locals[value->local_index];
      if (!local->is_record) return elf_diag(diag, "direct ELF64 field load requires record local", value->line, value->column, "non-record local");
      elf_emit_load_field_rax(code, local, value->field_offset, value->type);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_LEN: {
      return elf_emit_byte_view_len(code, fun, value->left, ctx, diag);
    }
    case IR_VALUE_CRC32_BYTES: {
      if (!value->left) return elf_diag(diag, "direct ELF64 CRC32 requires a byte view", value->line, value->column, "missing byte view");
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_r9_from_rax(code);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_mov_eax_u32(code, 0xffffffffu);
      z_x64_emit_xor_ecx_ecx(code);
      size_t byte_loop = code->len;
      z_x64_emit_cmp_reg_reg(code, 1, 9, true);
      size_t done = z_x64_emit_jcc32_placeholder(code, 0x83);
      z_x64_emit_movzx_reg32_base_index_u8(code, 2, 6, 1);
      z_x64_emit_xor_reg_from_reg(code, 0, 2, false);
      z_x64_emit_mov_reg_u32(code, 8, 8);
      size_t bit_loop = code->len;
      z_x64_emit_mov_reg_from_reg(code, 2, 0, false);
      z_x64_emit_and_reg_i8(code, 2, 1, false);
      z_x64_emit_neg_reg(code, 2, false);
      z_x64_emit_and_reg_u32(code, 2, 0xedb88320u, false);
      z_x64_emit_shr_reg_one(code, 0, false);
      z_x64_emit_xor_reg_from_reg(code, 0, 2, false);
      z_x64_emit_dec_r8d(code);
      size_t bit_back = z_x64_emit_jcc32_placeholder(code, 0x85);
      z_x64_patch_rel32(code, bit_back, bit_loop);
      z_x64_emit_inc_rcx(code);
      size_t byte_back = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, byte_back, byte_loop);
      z_x64_patch_rel32(code, done, code->len);
      z_x64_append_u8(code, 0x35);
      z_x64_append_u32(code, 0xffffffffu);
      return true;
    }
    case IR_VALUE_BYTE_COPY: {
      if (!value->left || !value->right) return elf_diag(diag, "direct ELF64 byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_pop_reg64(code, 7);
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_pop_reg64(code, 6);
      z_x64_emit_cmp_rax_rcx(code, true);
      size_t keep_dst_len = z_x64_emit_jcc32_placeholder(code, 0x86);
      z_x64_emit_mov_rax_from_rcx(code);
      z_x64_patch_rel32(code, keep_dst_len, code->len);
      z_x64_emit_mov_rdx_from_rax(code);
      z_x64_emit_xor_r8d_r8d(code);
      size_t loop = code->len;
      z_x64_emit_cmp_reg_reg(code, 2, 8, true);
      size_t done = z_x64_emit_jcc32_placeholder(code, 0x86);
      z_x64_emit_load_reg8_base_index(code, 3, 6, 8);
      z_x64_emit_store_base_index_reg8(code, 7, 8, 3);
      z_x64_emit_inc_r8(code);
      size_t back = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, back, loop);
      z_x64_patch_rel32(code, done, code->len);
      z_x64_emit_mov_rax_from_rdx(code);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_EQ: {
      if (!value->left || !value->right) return elf_diag(diag, "direct ELF64 byte-view equality requires two byte views", value->line, value->column, "missing byte view");
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_cmp_reg_reg(code, 1, 0, false);
      size_t same_len = z_x64_emit_jcc32_placeholder(code, 0x84);
      z_x64_emit_mov_eax_u32(code, 0);
      size_t end = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, same_len, code->len);
      z_x64_emit_mov_reg_from_rax(code, 10, true);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_reg_from_rax(code, 8, true);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      z_x64_emit_mov_r9_from_rax(code);
      z_x64_emit_xor_ecx_ecx(code);
      size_t loop = code->len;
      z_x64_emit_cmp_reg_reg(code, 1, 10, true);
      size_t equal = z_x64_emit_jcc32_placeholder(code, 0x83);
      z_x64_emit_load_reg8_base_index(code, 0, 8, 1);
      z_x64_emit_cmp_base_index_reg8(code, 9, 1, 0);
      size_t mismatch = z_x64_emit_jcc32_placeholder(code, 0x85);
      z_x64_emit_inc_rcx(code);
      size_t back = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, back, loop);
      z_x64_patch_rel32(code, mismatch, code->len);
      z_x64_emit_mov_eax_u32(code, 0);
      size_t after_false = z_x64_emit_jmp32_placeholder(code, 0xe9);
      z_x64_patch_rel32(code, equal, code->len);
      z_x64_emit_mov_eax_u32(code, 1);
      z_x64_patch_rel32(code, after_false, code->len);
      z_x64_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: {
      unsigned const_index = 0;
      unsigned char byte = 0;
      if (elf_const_u32_value(value->index, &const_index) &&
          elf_byte_view_const_byte(ctx ? ctx->ir : NULL, fun, value->left, const_index, &byte)) {
        z_x64_emit_mov_eax_u32(code, byte);
        return true;
      }
      if (!value->index || !elf_emit_value(code, fun, value->index, ctx, diag)) return false;
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_mov_rcx_from_rax(code, false);
      z_x64_emit_pop_rax(code);
      z_x64_emit_cmp_rax_rcx(code, false);
      size_t ok_patch = z_x64_emit_jcc32_placeholder(code, 0x82);
      z_x64_emit_ud2(code);
      z_x64_patch_rel32(code, ok_patch, code->len);
      z_x64_emit_push_rax(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      z_x64_emit_pop_reg64(code, 1);
      z_x64_emit_add_rax_rcx(code, true);
      z_x64_emit_movzx_reg32_ptr_reg_u8(code, 0, 0);
      return true;
    }
    default:
      return elf_diag(diag, "direct ELF64 value kind is unsupported", value->line, value->column, "unsupported value");
  }
}

static bool elf_validate_function(const IrFunction *fun, ZDiag *diag) {
  if (fun->param_count > 8) return elf_diag(diag, "direct ELF64 object backend supports at most eight parameters", fun->line, fun->column, fun->name);
  if (fun->return_type != IR_TYPE_VOID && !elf_type_is_supported_scalar(fun->return_type)) {
    return elf_diag(diag, "direct ELF64 object backend currently supports only Void and primitive integer returns", fun->line, fun->column, elf_type_name(fun->return_type));
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    if (fun->locals[i].is_array) {
      if (fun->locals[i].element_type != IR_TYPE_U8 && fun->locals[i].element_type != IR_TYPE_I32 && fun->locals[i].element_type != IR_TYPE_U32 && fun->locals[i].element_type != IR_TYPE_I64 && fun->locals[i].element_type != IR_TYPE_U64) {
        return elf_diag(diag, "direct ELF64 object backend currently supports only primitive integer fixed-array locals", fun->locals[i].line, fun->locals[i].column, elf_type_name(fun->locals[i].element_type));
      }
      continue;
    }
    if (fun->locals[i].is_record) continue;
    if (fun->locals[i].type == IR_TYPE_BYTE_VIEW) {
      if (fun->locals[i].is_param) {
        return elf_diag(diag, "direct ELF64 object backend does not yet support byte-view parameters", fun->locals[i].line, fun->locals[i].column, fun->locals[i].name);
      }
      continue;
    }
    if (fun->locals[i].type == IR_TYPE_ALLOC || fun->locals[i].type == IR_TYPE_VEC ||
        fun->locals[i].type == IR_TYPE_MAYBE_BYTE_VIEW || fun->locals[i].type == IR_TYPE_MAYBE_SCALAR) continue;
    if (!elf_type_is_supported_scalar(fun->locals[i].type)) {
      return elf_diag(diag, "direct ELF64 object backend currently supports only primitive integer locals", fun->locals[i].line, fun->locals[i].column, elf_type_name(fun->locals[i].type));
    }
  }
  return true;
}

static bool elf_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, ElfEmitContext *ctx, ZDiag *diag);

static bool elf_emit_world_write(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag) {
  if (!instr || !instr->value) return elf_diag(diag, "direct ELF64 World write requires bytes", instr ? instr->line : 1, instr ? instr->column : 1, "missing byte view");
  if (!elf_emit_byte_view_ptr(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  if (!elf_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
  z_x64_emit_push_rax(text);
  z_x64_emit_pop_reg64(text, 2);
  z_x64_emit_pop_reg64(text, 6);
  z_x64_emit_mov_reg_u32(text, 7, instr->field_offset == 2 ? 2 : 1);
  z_x64_emit_mov_eax_u32(text, 1);
  z_x64_emit_syscall(text);
  z_x64_emit_test_rax_rax(text, true);
  size_t ok_patch = z_x64_emit_jcc32_placeholder(text, 0x89);
  z_x64_emit_ud2(text);
  z_x64_patch_rel32(text, ok_patch, text->len);
  return true;
}

static bool elf_emit_args_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, ElfEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return elf_diag(diag, "direct ELF64 std.args.get requires an index", value ? value->line : 1, value ? value->column : 1, "missing index");
  if (!elf_emit_value(text, fun, value->left, ctx, diag)) return false;
  if (ctx && ctx->seed_main_process_args) {
    z_x64_emit_push_reg64(text, 14);
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_cmp_rax_rcx(text, true);
  } else {
    z_x64_emit_cmp_reg_ptr_reg(text, 0, 15, true);
  }
  size_t in_range = z_x64_emit_jcc32_placeholder(text, 0x82);
  elf_emit_maybe_clear(text, local);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, in_range, text->len);

  if (ctx && ctx->seed_main_process_args) {
    z_x64_emit_load_base_index_scale_disp_reg(text, 0, 15, 0, 8, 0, true);
  } else {
    z_x64_emit_load_base_index_scale_disp_reg(text, 0, 15, 0, 8, 8, true);
  }
  z_x64_emit_push_rax(text);
  elf_emit_strlen_rax_to_ecx(text);
  z_x64_emit_mov_eax_u32(text, 1);
  elf_emit_store_local_slot_reg(text, local, 0, 0, false);
  z_x64_emit_pop_rax(text);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_store_local_slot_reg(text, local, 16, 1, false);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool elf_emit_env_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, ElfEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return elf_diag(diag, "direct ELF64 std.env.get requires a key", value ? value->line : 1, value ? value->column : 1, "missing key");
  if (!elf_emit_byte_view_ptr(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_r9_from_rax(text);
  if (!elf_emit_byte_view_len(text, fun, value->left, ctx, diag)) return false;
  z_x64_emit_mov_reg_from_reg(text, 10, 0, true);

  if (ctx && ctx->seed_main_process_args) {
    z_x64_emit_push_reg64(text, 13);
    z_x64_emit_pop_reg64(text, 8);
  } else {
    z_x64_emit_load_reg_ptr_reg(text, 8, 15, true);
    z_x64_emit_add_reg_i8(text, 8, 2, true);
    z_x64_emit_shl_reg_imm8(text, 8, 3, true);
    z_x64_emit_add_reg_reg(text, 8, 15, true);
  }

  size_t env_loop = text->len;
  z_x64_emit_load_reg_ptr_reg(text, 3, 8, true);
  z_x64_emit_test_reg_reg(text, 3, true);
  size_t none = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_xor_ecx_ecx(text);

  size_t compare_loop = text->len;
  z_x64_emit_cmp_reg_reg(text, 1, 10, true);
  size_t key_done = z_x64_emit_jcc32_placeholder(text, 0x83);
  z_x64_emit_load_reg8_base_index(text, 0, 9, 1);
  z_x64_emit_cmp_base_index_reg8(text, 3, 1, 0);
  size_t next = z_x64_emit_jcc32_placeholder(text, 0x85);
  z_x64_emit_inc_rcx(text);
  size_t compare_back = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, compare_back, compare_loop);

  z_x64_patch_rel32(text, key_done, text->len);
  z_x64_emit_cmp_base_index_u8(text, 3, 10, 0x3d);
  size_t next_after_key = z_x64_emit_jcc32_placeholder(text, 0x85);
  z_x64_emit_lea_base_index_scale_disp_reg(text, 0, 3, 10, 1, 1);
  z_x64_emit_push_rax(text);
  elf_emit_strlen_rax_to_ecx(text);
  z_x64_emit_mov_eax_u32(text, 1);
  elf_emit_store_local_slot_reg(text, local, 0, 0, false);
  z_x64_emit_pop_rax(text);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_store_local_slot_reg(text, local, 16, 1, false);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);

  z_x64_patch_rel32(text, next, text->len);
  z_x64_patch_rel32(text, next_after_key, text->len);
  z_x64_emit_add_reg_i8(text, 8, 8, true);
  size_t loop_back = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, loop_back, env_loop);

  z_x64_patch_rel32(text, none, text->len);
  elf_emit_maybe_clear(text, local);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool elf_emit_read_all_or_raise_to_local(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag) {
  if (!fun || !instr || instr->local_index >= fun->local_len || fun->locals[instr->local_index].type != IR_TYPE_BYTE_VIEW) {
    return elf_diag(diag, "direct ELF64 std.fs.readAllOrRaise local is invalid", instr ? instr->line : 1, instr ? instr->column : 1, "invalid ByteBuf local");
  }
  if (!instr->value || instr->value->kind != IR_VALUE_CHECK || !instr->value->left || instr->value->left->kind != IR_VALUE_FS_READ_ALL) {
    return elf_diag(diag, "direct ELF64 checked std.fs.readAllOrRaise local requires a readAllOrRaise check", instr->line, instr->column, "unsupported checked readAll");
  }
  if (!elf_function_propagates_to_process_exit(fun)) {
    return elf_diag(diag, "direct ELF64 std.fs.readAllOrRaise check requires a fallible function context", instr->line, instr->column, "non-fallible context");
  }

  const IrValue *value = instr->value->left;
  if (value->local_index >= fun->local_len || fun->locals[value->local_index].type != IR_TYPE_ALLOC) {
    return elf_diag(diag, "direct ELF64 std.fs.readAllOrRaise allocator is invalid", value->line, value->column, "invalid allocator");
  }

  const IrLocal *local = &fun->locals[instr->local_index];
  const IrLocal *alloc = &fun->locals[value->local_index];
  if (!elf_emit_openat_path(text, fun, value->left, 0, 0, ctx, diag)) return false;
  z_x64_emit_test_rax_rax(text, true);
  size_t open_fail = elf_emit_js_placeholder(text);

  z_x64_emit_push_rax(text);
  z_x64_emit_mov_rdi_from_rax(text);
  z_x64_emit_xor_reg_reg(text, 6, true);
  z_x64_emit_mov_reg_u32(text, 2, 2);
  z_x64_emit_mov_eax_u32(text, 8);
  z_x64_emit_syscall(text);
  z_x64_emit_test_rax_rax(text, true);
  size_t tell_fail = elf_emit_js_placeholder(text);
  if (value->right) {
    z_x64_emit_push_rax(text);
    if (!elf_emit_value(text, fun, value->right, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    z_x64_emit_cmp_reg_reg(text, 1, 0, true);
    size_t size_ok = z_x64_emit_jcc32_placeholder(text, 0x83);
    z_x64_emit_pop_rax(text);
    elf_emit_close_rax_fd(text);
    elf_emit_packed_error_rax(text, IR_ERROR_TOO_LARGE);
    if (!fun->raises) {
      z_x64_emit_mov_eax_u32(text, 1);
    }
    elf_emit_epilogue(text, fun, ctx);
    z_x64_patch_rel32(text, size_ok, text->len);
  }
  z_x64_emit_pop_rax(text);

  z_x64_emit_push_rax(text);
  z_x64_emit_push_rax(text);
  elf_emit_load_local_slot_reg(text, alloc, 0, 6, true);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  z_x64_emit_add_reg_reg(text, 6, 1, true);
  elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
  z_x64_emit_sub_reg_reg(text, 2, 1, false);
  if (value->right) {
    if (!elf_emit_value(text, fun, value->right, ctx, diag)) return false;
    z_x64_emit_cmp_reg_reg(text, 2, 0, false);
    size_t keep_capacity = z_x64_emit_jcc32_placeholder(text, 0x86);
    z_x64_emit_mov_reg_from_reg(text, 2, 0, false);
    z_x64_patch_rel32(text, keep_capacity, text->len);
  }
  z_x64_emit_pop_reg64(text, 7);
  z_x64_emit_xor_eax_eax(text);
  z_x64_emit_syscall(text);
  z_x64_emit_push_rax(text);
  z_x64_emit_load_rsp_offset_reg(text, 0, 8, true);
  elf_emit_close_rax_fd(text);
  z_x64_emit_pop_rax(text);
  z_x64_emit_add_rsp(text, 8);
  z_x64_emit_test_rax_rax(text, true);
  size_t read_fail = elf_emit_js_placeholder(text);

  z_x64_emit_push_rax(text);
  elf_emit_load_local_slot_rax(text, alloc, 0);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  z_x64_emit_add_rax_rcx(text, true);
  elf_emit_store_local_slot_rax(text, local, 0);
  z_x64_emit_pop_rax(text);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  z_x64_emit_add_rax_rcx(text, false);
  elf_emit_store_local_slot_reg(text, alloc, 12, 0, false);
  size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);

  z_x64_patch_rel32(text, open_fail, text->len);
  elf_emit_packed_error_rax(text, IR_ERROR_NOT_FOUND);
  if (!fun->raises) {
    z_x64_emit_mov_eax_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);

  z_x64_patch_rel32(text, tell_fail, text->len);
  z_x64_emit_pop_rax(text);
  elf_emit_close_rax_fd(text);
  elf_emit_packed_error_rax(text, IR_ERROR_IO);
  if (!fun->raises) {
    z_x64_emit_mov_eax_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);

  z_x64_patch_rel32(text, read_fail, text->len);
  elf_emit_packed_error_rax(text, IR_ERROR_IO);
  if (!fun->raises) {
    z_x64_emit_mov_eax_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);
  z_x64_patch_rel32(text, end, text->len);
  return true;
}

static bool elf_emit_instr(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag) {
  if (instr->kind == IR_INSTR_WORLD_WRITE) {
    return elf_emit_world_write(text, fun, instr, ctx, diag);
  }
 if (instr->kind == IR_INSTR_LOCAL_SET) {
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_BYTE_VIEW) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (instr->value && instr->value->kind == IR_VALUE_CHECK && instr->value->left && instr->value->left->kind == IR_VALUE_FS_READ_ALL) {
        return elf_emit_read_all_or_raise_to_local(text, fun, instr, ctx, diag);
      }
      if (!elf_emit_byte_view_ptr(text, fun, instr->value, ctx, diag)) return false;
      elf_emit_store_local_slot_rax(text, local, 0);
      if (!elf_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
      elf_emit_store_local_slot_rax(text, local, 8);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_ALLOC) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!instr->value || instr->value->kind != IR_VALUE_FIXED_BUF_ALLOC) return elf_diag(diag, "direct ELF64 FixedBufAlloc local requires std.mem.fixedBufAlloc", instr->line, instr->column, "unsupported allocator initializer");
      if (!elf_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      elf_emit_store_local_slot_rax(text, local, 0);
      if (!elf_emit_byte_view_len(text, fun, instr->value->left, ctx, diag)) return false;
      elf_emit_store_local_slot_reg(text, local, 8, 0, false);
      z_x64_emit_mov_eax_u32(text, 0);
      elf_emit_store_local_slot_reg(text, local, 12, 0, false);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return elf_diag(diag, "direct ELF64 Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!elf_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      elf_emit_store_local_slot_rax(text, local, 0);
      z_x64_emit_mov_eax_u32(text, 0);
      elf_emit_store_local_slot_reg(text, local, 8, 0, false);
      if (!elf_emit_byte_view_len(text, fun, instr->value->left, ctx, diag)) return false;
      elf_emit_store_local_slot_reg(text, local, 12, 0, false);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_MAYBE_BYTE_VIEW) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (instr->value && instr->value->kind == IR_VALUE_FS_TEMP_NAME) {
        const IrValue *buf = instr->value->left;
        const IrValue *prefix = instr->value->right;
        if (!buf || buf->kind != IR_VALUE_ARRAY_BYTE_VIEW || buf->array_index >= fun->local_len) {
          return elf_diag(diag, "direct ELF64 std.fs.tempName requires a caller-provided fixed byte buffer", instr->line, instr->column, "unsupported temp buffer");
        }
        const IrLocal *buf_local = &fun->locals[buf->array_index];
        if (!buf_local->is_array || buf_local->element_type != IR_TYPE_U8) {
          return elf_diag(diag, "direct ELF64 std.fs.tempName buffer must be [N]u8", instr->line, instr->column, "non-byte temp buffer");
        }
        unsigned prefix_len = 0;
        if (!elf_byte_view_const_len(fun, prefix, &prefix_len)) {
          return elf_diag(diag, "direct ELF64 std.fs.tempName currently requires a literal prefix", instr->line, instr->column, "dynamic prefix");
        }
        unsigned char last = 0;
        if (prefix_len > 0 && elf_byte_view_const_byte(ctx ? ctx->ir : NULL, fun, prefix, prefix_len - 1, &last) && last == 0) prefix_len--;
        unsigned total_len = prefix_len + 4;
        if (buf_local->array_len <= total_len) {
          elf_emit_maybe_clear(text, local);
          return true;
        }
        elf_emit_lea_array_base_rax(text, buf_local);
        for (unsigned i = 0; i < prefix_len; i++) {
          unsigned char byte = 0;
          if (!elf_byte_view_const_byte(ctx ? ctx->ir : NULL, fun, prefix, i, &byte)) {
            return elf_diag(diag, "direct ELF64 std.fs.tempName prefix byte is unavailable", instr->line, instr->column, "unavailable prefix");
          }
          z_x64_emit_mov_ptr_reg_disp_u8(text, 0, i, byte);
        }
        const unsigned char suffix[] = {'-', 't', 'm', 'p', 0};
        for (unsigned i = 0; i < sizeof(suffix); i++) {
          z_x64_emit_mov_ptr_reg_disp_u8(text, 0, prefix_len + i, suffix[i]);
        }
        z_x64_emit_push_rax(text);
        z_x64_emit_mov_eax_u32(text, 1);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        z_x64_emit_pop_rax(text);
        elf_emit_store_local_slot_rax(text, local, 8);
        z_x64_emit_mov_eax_u32(text, total_len);
        elf_emit_store_local_slot_reg(text, local, 16, 0, false);
        return true;
      }
      if (instr->value && instr->value->kind == IR_VALUE_ARGS_GET) {
        return elf_emit_args_get_to_local(text, fun, instr->value, local, ctx, diag);
      }
      if (instr->value && instr->value->kind == IR_VALUE_ENV_GET) {
        return elf_emit_env_get_to_local(text, fun, instr->value, local, ctx, diag);
      }
      if (instr->value && instr->value->kind == IR_VALUE_FS_READ_ALL) {
        if (instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return elf_diag(diag, "direct ELF64 std.fs.readAll allocator is invalid", instr->line, instr->column, "invalid allocator");
        const IrLocal *alloc = &fun->locals[instr->value->local_index];
        if (!elf_emit_openat_path(text, fun, instr->value->left, 0, 0, ctx, diag)) return false;
        z_x64_emit_test_rax_rax(text, true);
        size_t open_fail = elf_emit_js_placeholder(text);
        z_x64_emit_push_rax(text);
        elf_emit_load_local_slot_reg(text, alloc, 0, 6, true);
        elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
        z_x64_emit_pop_reg64(text, 7);
        z_x64_emit_xor_eax_eax(text);
        z_x64_emit_syscall(text);
        z_x64_emit_push_rax(text);
        elf_emit_load_local_rax(text, fun, instr->value->local_index);
        (void)alloc;
        z_x64_emit_mov_rax_from_rdi(text);
        elf_emit_close_rax_fd(text);
        z_x64_emit_pop_rax(text);
        z_x64_emit_test_rax_rax(text, true);
        size_t read_fail = elf_emit_js_placeholder(text);
        z_x64_emit_push_rax(text);
        z_x64_emit_mov_eax_u32(text, 1);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        elf_emit_load_local_slot_reg(text, alloc, 0, 0, true);
        elf_emit_store_local_slot_reg(text, local, 8, 0, true);
        z_x64_emit_pop_rax(text);
        elf_emit_store_local_slot_reg(text, local, 16, 0, false);
        elf_emit_store_local_slot_reg(text, alloc, 12, 0, false);
        size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
        z_x64_patch_rel32(text, open_fail, text->len);
        z_x64_patch_rel32(text, read_fail, text->len);
        elf_emit_maybe_clear(text, local);
        z_x64_patch_rel32(text, end, text->len);
        return true;
      }
      if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return elf_diag(diag, "direct ELF64 allocation source is invalid", instr->line, instr->column, "invalid allocation");
      const IrLocal *alloc = &fun->locals[instr->value->local_index];
      if (!elf_emit_value(text, fun, instr->value->left, ctx, diag)) return false;
      z_x64_emit_push_rax(text);
      elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
      elf_emit_load_local_slot_reg(text, alloc, 0, 2, true);
      z_x64_emit_add_rdx_rcx(text, true);
      z_x64_emit_mov_eax_u32(text, 1);
      elf_emit_store_local_slot_reg(text, local, 0, 0, false);
      elf_emit_store_local_slot_reg(text, local, 8, 2, true);
      z_x64_emit_pop_rax(text);
      elf_emit_store_local_slot_reg(text, local, 16, 0, false);
      z_x64_emit_add_reg_reg(text, 1, 0, false);
      elf_emit_store_local_slot_reg(text, alloc, 12, 1, false);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_MAYBE_SCALAR) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!instr->value) return elf_diag(diag, "direct ELF64 Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
      if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
        z_x64_emit_mov_eax_u32(text, instr->value->data_len ? 1u : 0u);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        z_x64_emit_mov_eax_u32(text, (uint32_t)instr->value->int_value);
        elf_emit_store_local_slot_reg(text, local, 8, 0, true);
        return true;
      }
      if (instr->value->kind == IR_VALUE_JSON_PARSE_BYTES) {
        if (instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) {
          return elf_diag(diag, "direct ELF64 JSON parse allocator is invalid", instr->line, instr->column, "invalid allocator");
        }
        const IrLocal *alloc = &fun->locals[instr->value->local_index];
        if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
        z_x64_emit_test_rax_rax(text, true);
        size_t fail = elf_emit_js_placeholder(text);
        z_x64_emit_push_rax(text);
        elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
        z_x64_emit_add_reg_reg(text, 1, 0, false);
        elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
        z_x64_emit_cmp_reg_reg(text, 1, 2, false);
        size_t overflow = z_x64_emit_jcc32_placeholder(text, 0x87);
        z_x64_emit_pop_rax(text);
        elf_emit_maybe_scalar_store_rax(text, local);
        elf_emit_store_local_slot_reg(text, alloc, 12, 1, false);
        size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
        z_x64_patch_rel32(text, overflow, text->len);
        z_x64_emit_pop_rax(text);
        z_x64_patch_rel32(text, fail, text->len);
        elf_emit_maybe_scalar_clear(text, local);
        z_x64_patch_rel32(text, end, text->len);
        return true;
      }
      if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
      z_x64_emit_test_rax_rax(text, true);
      size_t fail = elf_emit_js_placeholder(text);
      elf_emit_maybe_scalar_store_rax(text, local);
      size_t end = z_x64_emit_jmp32_placeholder(text, 0xe9);
      z_x64_patch_rel32(text, fail, text->len);
      elf_emit_maybe_scalar_clear(text, local);
      z_x64_patch_rel32(text, end, text->len);
      return true;
    }
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    elf_emit_store_local_from_reg(text, fun, instr->local_index, 0);
    return true;
  }
  if (instr->kind == IR_INSTR_INDEX_STORE) {
    if (instr->array_index >= fun->local_len) return elf_diag(diag, "direct ELF64 indexed store array is out of range", instr->line, instr->column, "invalid array local");
    const IrLocal *local = &fun->locals[instr->array_index];
    if (!elf_emit_bounds_checked_address(text, fun, local, instr->index, ctx, diag)) return false;
    z_x64_emit_push_rax(text);
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_pop_reg64(text, 1);
    if (local->element_type == IR_TYPE_U8) {
      z_x64_emit_store_ptr_reg8_from_reg(text, 1, 0);
    } else if (elf_type_is_i64(local->element_type)) {
      z_x64_emit_store_ptr_reg_from_reg(text, 1, 0, true);
    } else {
      z_x64_emit_store_ptr_reg_from_reg(text, 1, 0, false);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_FIELD_STORE) {
    if (instr->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 field store record is out of range", instr->line, instr->column, "invalid record local");
    const IrLocal *local = &fun->locals[instr->local_index];
    if (!local->is_record) return elf_diag(diag, "direct ELF64 field store requires record local", instr->line, instr->column, "non-record local");
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    elf_emit_store_field_from_rax(text, local, instr->field_offset, instr->value ? instr->value->type : IR_TYPE_I32);
    return true;
  }
  if (instr->kind == IR_INSTR_EXPR) {
    if (instr->value && !elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    return true;
  }
  if (instr->kind == IR_INSTR_RAISE) {
    if (!elf_function_propagates_to_process_exit(fun)) return elf_diag(diag, "direct ELF64 raise requires a fallible function context", instr->line, instr->column, "non-fallible context");
    elf_emit_packed_error_rax(text, instr->error_code ? instr->error_code : IR_ERROR_UNKNOWN);
    elf_emit_epilogue(text, fun, ctx);
    return true;
  }
  if (instr->kind == IR_INSTR_RETURN) {
    if (instr->value && !elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    if (fun->raises && !instr->value) {
      z_x64_emit_xor_rax_rax(text);
    } else if (fun->raises && instr->value && !elf_type_is_i64(instr->value->type)) {
      z_x64_emit_mov_reg_from_reg(text, 0, 0, false);
    }
    elf_emit_epilogue(text, fun, ctx);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t false_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!elf_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
      z_x64_patch_rel32(text, false_patch, text->len);
      if (!elf_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
      z_x64_patch_rel32(text, end_patch, text->len);
    } else {
      z_x64_patch_rel32(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    z_x64_emit_test_rax_rax(text, false);
    size_t exit_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
    if (!elf_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    size_t back_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
    z_x64_patch_rel32(text, back_patch, loop_start);
    z_x64_patch_rel32(text, exit_patch, text->len);
    return true;
  }
  return elf_diag(diag, "direct ELF64 instruction kind is unsupported", instr->line, instr->column, "unsupported instruction");
}

static bool elf_emit_instrs(ZBuf *text, const IrFunction *fun, const IrInstr *instrs, size_t len, ElfEmitContext *ctx, ZDiag *diag) {
  for (size_t i = 0; i < len; i++) {
    if (!elf_emit_instr(text, fun, &instrs[i], ctx, diag)) return false;
  }
  return true;
}

static bool elf_emit_function_text(ZBuf *text, const IrFunction *fun, ElfEmitContext *ctx, ZDiag *diag) {
  static const unsigned param_regs[] = {7, 6, 2, 1, 8, 9};
  bool seed_process_args = elf_function_seeds_process_args(fun, ctx);
  unsigned base_stack_size = elf_base_stack_size(fun);
  unsigned stack_size = elf_total_stack_size(fun, ctx);
  z_x64_emit_prologue(text, stack_size);
  if (seed_process_args) {
    z_x64_emit_rbp_disp_reg(text, 0x89, 13, base_stack_size + 8, true);
    z_x64_emit_rbp_disp_reg(text, 0x89, 14, base_stack_size + 16, true);
    z_x64_emit_rbp_disp_reg(text, 0x89, 15, base_stack_size + 24, true);
    z_x64_emit_push_reg64(text, 7);
    z_x64_emit_pop_reg64(text, 14);
    z_x64_emit_push_reg64(text, 6);
    z_x64_emit_pop_reg64(text, 15);
    z_x64_emit_push_reg64(text, 2);
    z_x64_emit_pop_reg64(text, 13);
  }
  for (size_t i = 0; i < fun->param_count; i++) {
    if (i < 6) {
      elf_emit_store_local_from_reg(text, fun, (unsigned)i, param_regs[i]);
    } else {
      z_x64_emit_load_rbp_positive_reg(text, 0, 16u + (unsigned)(i - 6u) * 8u, false);
      elf_emit_store_local_from_reg(text, fun, (unsigned)i, 0);
    }
  }
  if (!elf_emit_instrs(text, fun, fun->instrs, fun->instr_len, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) elf_emit_epilogue(text, fun, ctx);
  return true;
}

static unsigned elf_rodata_base_offset(const IrProgram *ir) {
  if (!ir || ir->data_segment_len == 0) return 0;
  unsigned base = ir->data_segments[0].offset;
  for (size_t i = 1; i < ir->data_segment_len; i++) {
    if (ir->data_segments[i].offset < base) base = ir->data_segments[i].offset;
  }
  return base;
}

static void elf_append_rodata(ZBuf *rodata, const IrProgram *ir, unsigned base_offset) {
  for (size_t i = 0; ir && i < ir->data_segment_len; i++) {
    const IrDataSegment *segment = &ir->data_segments[i];
    z_elf_pad_to(rodata, segment->offset - base_offset);
    z_elf_append_bytes(rodata, segment->bytes, segment->len);
  }
}

bool z_emit_elf64_object_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return elf_diag(diag, "direct ELF64 object backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) return elf_ir_diag(diag, ir);
  if (ir->function_len == 0) return elf_diag(diag, "direct ELF64 object backend requires at least one exported function", 1, 1, "empty program");
  bool has_export = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported) has_export = true;
    if (!elf_validate_function(&ir->functions[i], diag)) return false;
  }
  if (!has_export) return elf_diag(diag, "direct ELF64 object backend requires at least one exported function", 1, 1, "no exported function");

  ZBuf text;
  ZBuf rodata;
  ZBuf rela_text;
  ZBuf strtab;
  ZBuf symtab;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&rela_text);
  zbuf_init(&strtab);
  zbuf_init(&symtab);
  z_elf_append_u8(&strtab, 0);
  z_elf_append_zeros(&symtab, 24);
  bool has_rodata = ir->readonly_data_bytes > 0 || ir->data_segment_len > 0;
  unsigned rodata_base_offset = elf_rodata_base_offset(ir);
  if (has_rodata) {
    elf_append_rodata(&rodata, ir, rodata_base_offset);
    z_elf_append_symbol(&symtab, 0, 0x03, 2, 0, 0);
  }

  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  size_t *function_sizes = z_checked_calloc(ir->function_len, sizeof(size_t));
  uint32_t *symbol_names = z_checked_calloc(ir->function_len, sizeof(uint32_t));
  if (!function_offsets || !function_sizes || !symbol_names) {
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    zbuf_free(&text);
    zbuf_free(&rodata);
    zbuf_free(&rela_text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    return elf_diag(diag, "direct ELF64 object backend ran out of memory", 1, 1, "allocation failed");
  }
  ElfEmitContext ctx = {
    .ir = ir,
    .function_offsets = function_offsets,
    .function_count = ir->function_len,
    .emit_rodata_relocations = true,
    .seed_main_process_args = true,
    .rodata_base_offset = rodata_base_offset
  };

  for (size_t i = 0; i < ir->function_len; i++) {
    z_elf_pad_to(&text, z_elf_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!elf_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      free(function_offsets);
      free(function_sizes);
      free(symbol_names);
      z_elf_emit_context_free(&ctx);
      zbuf_free(&text);
      zbuf_free(&rodata);
      zbuf_free(&rela_text);
      zbuf_free(&strtab);
      zbuf_free(&symtab);
      return false;
    }
    function_sizes[i] = text.len - function_offsets[i];
    symbol_names[i] = (uint32_t)strtab.len;
    zbuf_append(&strtab, ir->functions[i].name);
    z_elf_append_u8(&strtab, 0);
  }
  uint32_t runtime_names[ELF_RUNTIME_HELPER_COUNT] = {0};
  for (unsigned helper = 0; helper < ELF_RUNTIME_HELPER_COUNT; helper++) {
    ElfRuntimeHelper runtime_helper = (ElfRuntimeHelper)helper;
    if (z_elf_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    runtime_names[helper] = (uint32_t)strtab.len;
    zbuf_append(&strtab, z_elf_runtime_helper_symbol(runtime_helper));
    z_elf_append_u8(&strtab, 0);
  }
  z_elf_patch_call_patches(&text, &ctx);
  z_elf_append_rodata_relocations(&rela_text, &ctx, 1);
  const uint32_t function_symbol_base = has_rodata ? 2u : 1u;
  uint32_t next_runtime_symbol = function_symbol_base + (uint32_t)ir->function_len;
  uint32_t runtime_symbols[ELF_RUNTIME_HELPER_COUNT] = {0};
  for (unsigned helper = 0; helper < ELF_RUNTIME_HELPER_COUNT; helper++) {
    ElfRuntimeHelper runtime_helper = (ElfRuntimeHelper)helper;
    if (z_elf_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    runtime_symbols[helper] = next_runtime_symbol++;
    z_elf_append_runtime_relocations(&rela_text, &ctx, runtime_helper, runtime_symbols[helper]);
  }

  for (size_t i = 0; i < ir->function_len; i++) {
    z_elf_append_symbol(&symtab, symbol_names[i], ir->functions[i].is_exported ? 0x12 : 0x02, 1, function_offsets[i], function_sizes[i]);
  }
  for (unsigned helper = 0; helper < ELF_RUNTIME_HELPER_COUNT; helper++) {
    ElfRuntimeHelper runtime_helper = (ElfRuntimeHelper)helper;
    if (z_elf_runtime_patch_count(&ctx, runtime_helper) == 0) continue;
    z_elf_append_symbol(&symtab, runtime_names[helper], 0x12, 0, 0, 0);
  }
  ZElfObjectImage image = {
    .machine = Z_ELF_MACHINE_X86_64,
    .text = &text,
    .text_align = 16,
    .rodata = has_rodata ? &rodata : NULL,
    .rodata_align = 8,
    .rela_text = rela_text.len > 0 ? &rela_text : NULL,
    .symtab = &symtab,
    .strtab = &strtab,
    .local_symbol_count = has_rodata ? 2 : 1
  };
  z_elf_write_object64(out, &image);

  free(function_offsets);
  free(function_sizes);
  free(symbol_names);
  z_elf_emit_context_free(&ctx);
  zbuf_free(&text);
  zbuf_free(&rodata);
  zbuf_free(&rela_text);
  zbuf_free(&strtab);
  zbuf_free(&symtab);
  return true;
}

static const IrFunction *elf_find_executable_main(const IrProgram *ir, ZDiag *diag, unsigned *out_index) {
  const IrFunction *fun = NULL;
  unsigned index = 0;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported && strcmp(ir->functions[i].name, "main") == 0) {
      if (fun) {
        elf_diag(diag, "direct ELF64 executable backend requires exactly one exported main function", ir->functions[i].line, ir->functions[i].column, ir->functions[i].name);
        return NULL;
      }
      fun = &ir->functions[i];
      index = (unsigned)i;
    }
  }
  if (!fun) {
    elf_diag(diag, "direct ELF64 executable backend requires an exported main function", 1, 1, "missing main");
    return NULL;
  }
  if (fun->param_count != 0) {
    elf_diag(diag, "direct ELF64 executable main must not take parameters", fun->line, fun->column, fun->name);
    return NULL;
  }
  if (!elf_type_is_scalar(fun->return_type) || elf_type_is_i64(fun->return_type)) {
    elf_diag(diag, "direct ELF64 executable main must return i32 or u32", fun->line, fun->column, elf_type_name(fun->return_type));
    return NULL;
  }
  if (!elf_validate_function(fun, diag)) return NULL;
  if (out_index) *out_index = index;
  return fun;
}

static size_t elf_emit_start_stub(ZBuf *text) {
  z_x64_emit_mov_reg_from_reg(text, 15, 4, true);
  size_t patch = z_x64_emit_call32_placeholder(text);
  z_x64_emit_mov_rcx_from_rax(text, true);
  z_x64_emit_shr_rcx_imm8(text, 32);
  z_x64_emit_test_ecx_ecx(text);
  size_t success_patch = z_x64_emit_jcc32_placeholder(text, 0x84);
  z_x64_emit_mov_reg_u32(text, 7, 1);
  size_t exit_patch = z_x64_emit_jmp32_placeholder(text, 0xe9);
  z_x64_patch_rel32(text, success_patch, text->len);
  z_x64_emit_mov_reg_from_reg(text, 7, 0, false);
  z_x64_patch_rel32(text, exit_patch, text->len);
  z_x64_emit_mov_eax_u32(text, 60);
  z_x64_emit_syscall(text);
  return patch;
}

bool z_emit_elf64_exe_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return elf_diag(diag, "direct ELF64 executable backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) return elf_ir_diag(diag, ir);
  unsigned main_index = 0;
  const IrFunction *main_fun = elf_find_executable_main(ir, diag, &main_index);
  if (!main_fun) return false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (!elf_validate_function(&ir->functions[i], diag)) return false;
  }

  const uint64_t base_addr = 0x400000;
  const size_t ehdr_size = 64;
  const size_t phdr_size = 56;
  const size_t text_offset = ehdr_size + phdr_size;
  const uint64_t entry_addr = base_addr + text_offset;

  ZBuf text;
  ZBuf rodata;
  zbuf_init(&text);
  zbuf_init(&rodata);
  bool has_rodata = ir->readonly_data_bytes > 0 || ir->data_segment_len > 0;
  unsigned rodata_base_offset = elf_rodata_base_offset(ir);
  if (has_rodata) elf_append_rodata(&rodata, ir, rodata_base_offset);
  size_t start_stub_len = 3 + 5 + 2 + 5 + 2;
  size_t first_function_offset = z_elf_align(start_stub_len, 16);
  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  if (!function_offsets) {
    zbuf_free(&text);
    zbuf_free(&rodata);
    return elf_diag(diag, "direct ELF64 executable backend ran out of memory", 1, 1, "allocation failed");
  }
  ElfEmitContext ctx = {
    .ir = ir,
    .function_offsets = function_offsets,
    .function_count = ir->function_len,
    .rodata_base_offset = rodata_base_offset
  };
  size_t start_call_patch = elf_emit_start_stub(&text);
  z_elf_pad_to(&text, first_function_offset);
  for (size_t i = 0; i < ir->function_len; i++) {
    z_elf_pad_to(&text, z_elf_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!elf_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      free(function_offsets);
      z_elf_emit_context_free(&ctx);
      zbuf_free(&text);
      zbuf_free(&rodata);
      return false;
    }
  }
  if (z_elf_has_runtime_patches(&ctx)) {
    free(function_offsets);
    z_elf_emit_context_free(&ctx);
    zbuf_free(&text);
    zbuf_free(&rodata);
    return elf_diag(diag, "direct ELF64 executable runtime helpers require object emission and an explicit runtime link step", 1, 1, "use --emit obj and link zero_runtime.c");
  }
  z_x64_patch_rel32(&text, start_call_patch, function_offsets[main_index]);
  z_elf_patch_call_patches(&text, &ctx);

  size_t rodata_offset = has_rodata ? z_elf_align(text_offset + text.len, 8) : 0;
  ctx.rodata_addr = has_rodata ? base_addr + rodata_offset : 0;
  z_elf_patch_rodata_patches(&text, &ctx);
  ZElfExecutableImage image = {
    .machine = Z_ELF_MACHINE_X86_64,
    .base_addr = base_addr,
    .entry_addr = entry_addr,
    .text_offset = text_offset,
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .rodata_offset = rodata_offset,
    .segment_align = 0x1000
  };
  z_elf_write_executable64(out, &image);
  free(function_offsets);
  z_elf_emit_context_free(&ctx);
  zbuf_free(&text);
  zbuf_free(&rodata);
  return true;
}
