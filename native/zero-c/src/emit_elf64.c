#include "zero.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void elf_append_u8(ZBuf *buf, unsigned value) {
  zbuf_append_char(buf, (char)(value & 0xff));
}

static void elf_append_u16(ZBuf *buf, uint16_t value) {
  elf_append_u8(buf, value);
  elf_append_u8(buf, value >> 8);
}

static void elf_append_u32(ZBuf *buf, uint32_t value) {
  elf_append_u8(buf, value);
  elf_append_u8(buf, value >> 8);
  elf_append_u8(buf, value >> 16);
  elf_append_u8(buf, value >> 24);
}

static void elf_append_u64(ZBuf *buf, uint64_t value) {
  elf_append_u32(buf, (uint32_t)value);
  elf_append_u32(buf, (uint32_t)(value >> 32));
}

static void elf_append_bytes(ZBuf *buf, const unsigned char *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) elf_append_u8(buf, bytes[i]);
}

static void elf_append_zeros(ZBuf *buf, size_t len) {
  for (size_t i = 0; i < len; i++) elf_append_u8(buf, 0);
}

static size_t elf_align(size_t value, size_t alignment) {
  size_t remainder = alignment ? value % alignment : 0;
  return remainder == 0 ? value : value + (alignment - remainder);
}

static void elf_pad_to(ZBuf *buf, size_t offset) {
  while (buf->len < offset) elf_append_u8(buf, 0);
}

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

static void elf_emit_rbp_disp_reg(ZBuf *code, unsigned opcode, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    elf_append_u8(code, rex);
  }
  elf_append_u8(code, opcode);
  unsigned reg_low = reg & 7;
  if (offset <= 127) {
    elf_append_u8(code, 0x40 | (reg_low << 3) | 0x05);
    elf_append_u8(code, (unsigned char)(-(int)offset));
  } else {
    elf_append_u8(code, 0x80 | (reg_low << 3) | 0x05);
    elf_append_u32(code, (uint32_t)(-(int32_t)offset));
  }
}

static void elf_emit_load_rbp_positive_reg(ZBuf *code, unsigned reg, unsigned offset, bool wide) {
  if (wide || reg >= 8) {
    unsigned rex = wide ? 0x48 : 0x40;
    if (reg >= 8) rex |= 0x04;
    elf_append_u8(code, rex);
  }
  elf_append_u8(code, 0x8b);
  unsigned reg_low = reg & 7;
  if (offset <= 127) {
    elf_append_u8(code, 0x40 | (reg_low << 3) | 0x05);
    elf_append_u8(code, (unsigned char)offset);
  } else {
    elf_append_u8(code, 0x80 | (reg_low << 3) | 0x05);
    elf_append_u32(code, offset);
  }
}

static void elf_emit_load_local_rax(ZBuf *code, const IrFunction *fun, unsigned local_index) {
  bool wide = fun && local_index < fun->local_len && elf_type_is_i64(fun->locals[local_index].type);
  elf_emit_rbp_disp_reg(code, 0x8b, 0, elf_local_offset(fun, local_index), wide);
}

static void elf_emit_store_local_from_reg(ZBuf *code, const IrFunction *fun, unsigned local_index, unsigned reg) {
  bool wide = fun && local_index < fun->local_len && elf_type_is_i64(fun->locals[local_index].type);
  elf_emit_rbp_disp_reg(code, 0x89, reg, elf_local_offset(fun, local_index), wide);
}

static unsigned elf_record_field_disp(const IrLocal *local, unsigned field_offset) {
  if (!local || field_offset > local->frame_offset) return local ? local->frame_offset : 0;
  return local->frame_offset - field_offset;
}

static void elf_emit_load_field_rax(ZBuf *code, const IrLocal *local, unsigned field_offset, IrTypeKind type) {
  unsigned disp = elf_record_field_disp(local, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    elf_append_u8(code, 0x0f);
    elf_emit_rbp_disp_reg(code, 0xb6, 0, disp, false);
  } else if (elf_type_is_i64(type)) {
    elf_emit_rbp_disp_reg(code, 0x8b, 0, disp, true);
  } else {
    elf_emit_rbp_disp_reg(code, 0x8b, 0, disp, false);
  }
}

static void elf_emit_store_field_from_rax(ZBuf *code, const IrLocal *local, unsigned field_offset, IrTypeKind type) {
  unsigned disp = elf_record_field_disp(local, field_offset);
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) {
    elf_emit_rbp_disp_reg(code, 0x88, 0, disp, false);
  } else if (elf_type_is_i64(type)) {
    elf_emit_rbp_disp_reg(code, 0x89, 0, disp, true);
  } else {
    elf_emit_rbp_disp_reg(code, 0x89, 0, disp, false);
  }
}

static unsigned elf_type_byte_size(IrTypeKind type) {
  if (type == IR_TYPE_U8 || type == IR_TYPE_BOOL) return 1;
  if (elf_type_is_i64(type)) return 8;
  return 4;
}

static void elf_emit_lea_array_base_rax(ZBuf *code, const IrLocal *local) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x8d);
  elf_append_u8(code, 0x85);
  elf_append_u32(code, (uint32_t)(-(int32_t)local->frame_offset));
}

static void elf_emit_scale_index_into_rax(ZBuf *code, IrTypeKind element_type) {
  unsigned size = elf_type_byte_size(element_type);
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x8d);
  if (size == 1) {
    elf_append_u8(code, 0x04);
    elf_append_u8(code, 0x08);
  } else if (size == 4) {
    elf_append_u8(code, 0x04);
    elf_append_u8(code, 0x88);
  } else {
    elf_append_u8(code, 0x04);
    elf_append_u8(code, 0xc8);
  }
}

static size_t elf_emit_jmp32_placeholder(ZBuf *code, unsigned opcode) {
  elf_append_u8(code, opcode);
  size_t patch = code->len;
  elf_append_u32(code, 0);
  return patch;
}

static size_t elf_emit_jcc32_placeholder(ZBuf *code, unsigned condition) {
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, condition);
  size_t patch = code->len;
  elf_append_u32(code, 0);
  return patch;
}

static void elf_patch_rel32(ZBuf *code, size_t patch_offset, size_t target_offset) {
  int64_t rel = (int64_t)target_offset - (int64_t)(patch_offset + 4);
  uint32_t value = (uint32_t)(int32_t)rel;
  code->data[patch_offset] = (char)(value & 0xff);
  code->data[patch_offset + 1] = (char)((value >> 8) & 0xff);
  code->data[patch_offset + 2] = (char)((value >> 16) & 0xff);
  code->data[patch_offset + 3] = (char)((value >> 24) & 0xff);
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

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
} ElfCallPatch;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} ElfRodataPatch;

typedef struct {
  size_t patch_offset;
} ElfRuntimePatch;

typedef struct {
  const IrProgram *ir;
  size_t *function_offsets;
  size_t function_count;
  ElfCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  ElfRodataPatch *rodata_patches;
  size_t rodata_patch_len;
  size_t rodata_patch_cap;
  ElfRuntimePatch *runtime_json_parse_bytes_patches;
  size_t runtime_json_parse_bytes_patch_len;
  size_t runtime_json_parse_bytes_patch_cap;
  ElfRuntimePatch *runtime_http_fetch_patches;
  size_t runtime_http_fetch_patch_len;
  size_t runtime_http_fetch_patch_cap;
  ElfRuntimePatch *runtime_http_result_ok_patches;
  size_t runtime_http_result_ok_patch_len;
  size_t runtime_http_result_ok_patch_cap;
  ElfRuntimePatch *runtime_http_result_status_patches;
  size_t runtime_http_result_status_patch_len;
  size_t runtime_http_result_status_patch_cap;
  ElfRuntimePatch *runtime_http_result_body_len_patches;
  size_t runtime_http_result_body_len_patch_len;
  size_t runtime_http_result_body_len_patch_cap;
  ElfRuntimePatch *runtime_http_result_error_patches;
  size_t runtime_http_result_error_patch_len;
  size_t runtime_http_result_error_patch_cap;
  ElfRuntimePatch *runtime_http_response_len_patches;
  size_t runtime_http_response_len_patch_len;
  size_t runtime_http_response_len_patch_cap;
  ElfRuntimePatch *runtime_http_response_headers_len_patches;
  size_t runtime_http_response_headers_len_patch_len;
  size_t runtime_http_response_headers_len_patch_cap;
  ElfRuntimePatch *runtime_http_response_body_offset_patches;
  size_t runtime_http_response_body_offset_patch_len;
  size_t runtime_http_response_body_offset_patch_cap;
  ElfRuntimePatch *runtime_http_header_value_patches;
  size_t runtime_http_header_value_patch_len;
  size_t runtime_http_header_value_patch_cap;
  ElfRuntimePatch *runtime_http_header_found_patches;
  size_t runtime_http_header_found_patch_len;
  size_t runtime_http_header_found_patch_cap;
  ElfRuntimePatch *runtime_http_header_offset_patches;
  size_t runtime_http_header_offset_patch_len;
  size_t runtime_http_header_offset_patch_cap;
  ElfRuntimePatch *runtime_http_header_len_patches;
  size_t runtime_http_header_len_patch_len;
  size_t runtime_http_header_len_patch_cap;
  bool emit_rodata_relocations;
  bool seed_main_process_args;
  unsigned rodata_base_offset;
  uint64_t rodata_addr;
} ElfEmitContext;

static bool elf_emit_value(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag);
static bool elf_emit_read_all_or_raise_to_local(ZBuf *text, const IrFunction *fun, const IrInstr *instr, ElfEmitContext *ctx, ZDiag *diag);

static bool elf_function_propagates_to_process_exit(const IrFunction *fun) {
  return fun && (fun->raises ||
                 (fun->is_exported &&
                  fun->name && strcmp(fun->name, "main") == 0 &&
                  fun->return_type == IR_TYPE_I32 &&
                  fun->value_return_type == IR_TYPE_VOID));
}

static bool elf_record_call_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned callee_index, ZDiag *diag, const IrValue *value) {
  if (!ctx || callee_index >= ctx->function_count) {
    return elf_diag(diag, "direct ELF64 call target index is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len + 1 > ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(ElfCallPatch));
  }
  ctx->call_patches[ctx->call_patch_len++] = (ElfCallPatch){.patch_offset = patch_offset, .callee_index = callee_index};
  return true;
}

static bool elf_record_rodata_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned data_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 readonly data patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->rodata_patch_len + 1 > ctx->rodata_patch_cap) {
    ctx->rodata_patch_cap = z_grow_capacity(ctx->rodata_patch_cap, ctx->rodata_patch_len + 1, 8);
    ctx->rodata_patches = z_checked_reallocarray(ctx->rodata_patches, ctx->rodata_patch_cap, sizeof(ElfRodataPatch));
  }
  ctx->rodata_patches[ctx->rodata_patch_len++] = (ElfRodataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static bool elf_record_runtime_patch(ElfRuntimePatch **items, size_t *len, size_t *cap, size_t patch_offset, ZDiag *diag, const IrValue *value, const char *name) {
  (void)name;
  if (!items || !len || !cap) return elf_diag(diag, "direct ELF64 runtime patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (*len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, *len + 1, 4);
    *items = z_checked_reallocarray(*items, *cap, sizeof(ElfRuntimePatch));
  }
  (*items)[(*len)++] = (ElfRuntimePatch){.patch_offset = patch_offset};
  return true;
}

static bool elf_record_runtime_json_parse_bytes_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 JSON runtime relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_json_parse_bytes_patches, &ctx->runtime_json_parse_bytes_patch_len, &ctx->runtime_json_parse_bytes_patch_cap, patch_offset, diag, value, "zero_json_parse_bytes");
}

static bool elf_record_runtime_http_fetch_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP runtime relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_fetch_patches, &ctx->runtime_http_fetch_patch_len, &ctx->runtime_http_fetch_patch_cap, patch_offset, diag, value, "zero_http_fetch_result");
}

static bool elf_record_runtime_http_result_ok_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP result relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_result_ok_patches, &ctx->runtime_http_result_ok_patch_len, &ctx->runtime_http_result_ok_patch_cap, patch_offset, diag, value, "zero_http_result_ok");
}

static bool elf_record_runtime_http_result_status_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP result relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_result_status_patches, &ctx->runtime_http_result_status_patch_len, &ctx->runtime_http_result_status_patch_cap, patch_offset, diag, value, "zero_http_result_status");
}

static bool elf_record_runtime_http_result_body_len_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP result relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_result_body_len_patches, &ctx->runtime_http_result_body_len_patch_len, &ctx->runtime_http_result_body_len_patch_cap, patch_offset, diag, value, "zero_http_result_body_len");
}

static bool elf_record_runtime_http_result_error_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP result relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_result_error_patches, &ctx->runtime_http_result_error_patch_len, &ctx->runtime_http_result_error_patch_cap, patch_offset, diag, value, "zero_http_result_error");
}

static bool elf_record_runtime_http_response_len_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP response relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_response_len_patches, &ctx->runtime_http_response_len_patch_len, &ctx->runtime_http_response_len_patch_cap, patch_offset, diag, value, "zero_http_response_len");
}

static bool elf_record_runtime_http_response_headers_len_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP response relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_response_headers_len_patches, &ctx->runtime_http_response_headers_len_patch_len, &ctx->runtime_http_response_headers_len_patch_cap, patch_offset, diag, value, "zero_http_response_headers_len");
}

static bool elf_record_runtime_http_response_body_offset_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP response relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_response_body_offset_patches, &ctx->runtime_http_response_body_offset_patch_len, &ctx->runtime_http_response_body_offset_patch_cap, patch_offset, diag, value, "zero_http_response_body_offset");
}

static bool elf_record_runtime_http_header_value_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP header relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_header_value_patches, &ctx->runtime_http_header_value_patch_len, &ctx->runtime_http_header_value_patch_cap, patch_offset, diag, value, "zero_http_header_value");
}

static bool elf_record_runtime_http_header_found_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP header relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_header_found_patches, &ctx->runtime_http_header_found_patch_len, &ctx->runtime_http_header_found_patch_cap, patch_offset, diag, value, "zero_http_header_found");
}

static bool elf_record_runtime_http_header_offset_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP header relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_header_offset_patches, &ctx->runtime_http_header_offset_patch_len, &ctx->runtime_http_header_offset_patch_cap, patch_offset, diag, value, "zero_http_header_offset");
}

static bool elf_record_runtime_http_header_len_patch(ElfEmitContext *ctx, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_diag(diag, "direct ELF64 HTTP header relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  return elf_record_runtime_patch(&ctx->runtime_http_header_len_patches, &ctx->runtime_http_header_len_patch_len, &ctx->runtime_http_header_len_patch_cap, patch_offset, diag, value, "zero_http_header_len");
}

static void elf_patch_call_patches(ZBuf *code, const ElfEmitContext *ctx) {
  for (size_t i = 0; i < ctx->call_patch_len; i++) {
    const ElfCallPatch *patch = &ctx->call_patches[i];
    elf_patch_rel32(code, patch->patch_offset, ctx->function_offsets[patch->callee_index]);
  }
}

static void elf_patch_rodata_patches(ZBuf *code, const ElfEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->rodata_patch_len; i++) {
    const ElfRodataPatch *patch = &ctx->rodata_patches[i];
    uint64_t addr = ctx->rodata_addr + (patch->data_offset - ctx->rodata_base_offset);
    for (unsigned b = 0; b < 8; b++) {
      code->data[patch->patch_offset + b] = (char)((addr >> (8 * b)) & 0xff);
    }
  }
}

static bool elf_function_seeds_process_args(const IrFunction *fun, const ElfEmitContext *ctx) {
  return ctx && ctx->seed_main_process_args &&
         fun && fun->is_exported && fun->name && strcmp(fun->name, "main") == 0;
}

static unsigned elf_base_stack_size(const IrFunction *fun) {
  return (unsigned)elf_align(fun ? fun->frame_bytes : 0, 16);
}

static unsigned elf_total_stack_size(const IrFunction *fun, const ElfEmitContext *ctx) {
  unsigned base = elf_base_stack_size(fun);
  return base + (elf_function_seeds_process_args(fun, ctx) ? 32u : 0u);
}

static void elf_emit_epilogue(ZBuf *code, const IrFunction *fun, const ElfEmitContext *ctx) {
  if (elf_function_seeds_process_args(fun, ctx)) {
    unsigned base = elf_base_stack_size(fun);
    elf_emit_rbp_disp_reg(code, 0x8b, 13, base + 8, true);
    elf_emit_rbp_disp_reg(code, 0x8b, 14, base + 16, true);
    elf_emit_rbp_disp_reg(code, 0x8b, 15, base + 24, true);
  }
  elf_append_u8(code, 0xc9);
  elf_append_u8(code, 0xc3);
}

static void elf_emit_pop_reg64(ZBuf *code, unsigned reg) {
  if (reg >= 8) elf_append_u8(code, 0x41);
  elf_append_u8(code, 0x58 + (reg & 7));
}

static void elf_emit_push_reg64(ZBuf *code, unsigned reg) {
  if (reg >= 8) elf_append_u8(code, 0x41);
  elf_append_u8(code, 0x50 + (reg & 7));
}

static void elf_emit_push_rax(ZBuf *code) {
  elf_append_u8(code, 0x50);
}

static void elf_emit_store_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide);
static void elf_emit_store_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset);
static bool elf_emit_byte_view_ptr(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);

static void elf_emit_strlen_rax_to_ecx(ZBuf *code) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x89);
  elf_append_u8(code, 0xc2);
  elf_append_u8(code, 0x31);
  elf_append_u8(code, 0xc9);
  size_t loop = code->len;
  elf_append_u8(code, 0x80);
  elf_append_u8(code, 0x3c);
  elf_append_u8(code, 0x0a);
  elf_append_u8(code, 0x00);
  size_t done = elf_emit_jcc32_placeholder(code, 0x84);
  elf_append_u8(code, 0xff);
  elf_append_u8(code, 0xc1);
  size_t back = elf_emit_jmp32_placeholder(code, 0xe9);
  elf_patch_rel32(code, back, loop);
  elf_patch_rel32(code, done, code->len);
}

static void elf_emit_maybe_clear(ZBuf *code, const IrLocal *local) {
  elf_append_u8(code, 0x31);
  elf_append_u8(code, 0xc0);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  elf_emit_store_local_slot_reg(code, local, 8, 0, true);
  elf_emit_store_local_slot_reg(code, local, 16, 0, false);
}

static void elf_emit_maybe_scalar_clear(ZBuf *code, const IrLocal *local) {
  elf_append_u8(code, 0x31);
  elf_append_u8(code, 0xc0);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  elf_emit_store_local_slot_reg(code, local, 8, 0, true);
}

static void elf_emit_maybe_scalar_store_rax(ZBuf *code, const IrLocal *local) {
  elf_append_u8(code, 0x50);
  elf_append_u8(code, 0xb8);
  elf_append_u32(code, 1);
  elf_emit_store_local_slot_reg(code, local, 0, 0, false);
  elf_append_u8(code, 0x58);
  elf_emit_store_local_slot_rax(code, local, 8);
}

static size_t elf_emit_js_placeholder(ZBuf *code) {
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0x88);
  size_t patch = code->len;
  elf_append_u32(code, 0);
  return patch;
}

static void elf_emit_bool_from_nonnegative_rax(ZBuf *code) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x85);
  elf_append_u8(code, 0xc0);
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0x99);
  elf_append_u8(code, 0xc0);
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0xb6);
  elf_append_u8(code, 0xc0);
}

static bool elf_emit_openat_path(ZBuf *code, const IrFunction *fun, const IrValue *path, unsigned flags, unsigned mode, ElfEmitContext *ctx, ZDiag *diag) {
  if (!elf_emit_byte_view_ptr(code, fun, path, ctx, diag)) return false;
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x89);
  elf_append_u8(code, 0xc6);
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0xbf);
  elf_append_u64(code, 0xffffffffffffff9cULL);
  elf_append_u8(code, 0xba);
  elf_append_u32(code, flags);
  elf_append_u8(code, 0x41);
  elf_append_u8(code, 0xba);
  elf_append_u32(code, mode);
  elf_append_u8(code, 0xb8);
  elf_append_u32(code, 257);
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0x05);
  return true;
}

static void elf_emit_close_rax_fd(ZBuf *code) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x89);
  elf_append_u8(code, 0xc7);
  elf_append_u8(code, 0xb8);
  elf_append_u32(code, 3);
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0x05);
}

static bool elf_emit_bounds_checked_address(ZBuf *code, const IrFunction *fun, const IrLocal *local, const IrValue *index, ElfEmitContext *ctx, ZDiag *diag) {
  if (!local || !local->is_array) return elf_diag(diag, "direct ELF64 indexed access requires fixed array local", index ? index->line : 1, index ? index->column : 1, "non-array local");
  if (!elf_emit_value(code, fun, index, ctx, diag)) return false;
  elf_append_u8(code, 0x3d);
  elf_append_u32(code, local->array_len);
  size_t ok_patch = elf_emit_jcc32_placeholder(code, 0x82);
  elf_append_u8(code, 0x0f);
  elf_append_u8(code, 0x0b);
  elf_patch_rel32(code, ok_patch, code->len);
  elf_append_u8(code, 0x89);
  elf_append_u8(code, 0xc1);
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
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0x89);
  elf_append_u8(code, 0xc1);
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0xc1);
  elf_append_u8(code, 0xe9);
  elf_append_u8(code, 32);
  elf_append_u8(code, 0x85);
  elf_append_u8(code, 0xc9);
}

static void elf_emit_packed_error_rax(ZBuf *code, unsigned code_value) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0xb8);
  elf_append_u64(code, ((uint64_t)code_value) << 32);
}

static bool elf_emit_rodata_ptr_rax(ZBuf *code, unsigned data_offset, ElfEmitContext *ctx, ZDiag *diag, const IrValue *value) {
  elf_append_u8(code, 0x48);
  elf_append_u8(code, 0xb8);
  size_t imm_offset = code->len;
  unsigned compact_offset = ctx ? data_offset - ctx->rodata_base_offset : data_offset;
  elf_append_u64(code, ctx && ctx->emit_rodata_relocations ? 0 : (ctx ? ctx->rodata_addr : 0) + compact_offset);
  return elf_record_rodata_patch(ctx, imm_offset, data_offset, diag, value);
}

static void elf_emit_store_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  elf_emit_rbp_disp_reg(code, 0x89, 0, disp, true);
}

static void elf_emit_store_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  elf_emit_rbp_disp_reg(code, 0x89, reg, disp, wide);
}

static void elf_emit_load_local_slot_rax(ZBuf *code, const IrLocal *local, unsigned slot_offset) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  elf_emit_rbp_disp_reg(code, 0x8b, 0, disp, true);
}

static void elf_emit_load_local_slot_reg(ZBuf *code, const IrLocal *local, unsigned slot_offset, unsigned reg, bool wide) {
  unsigned disp = local && local->frame_offset >= slot_offset ? local->frame_offset - slot_offset : 0;
  elf_emit_rbp_disp_reg(code, 0x8b, reg, disp, wide);
}

static bool elf_emit_byte_view_len(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);
static bool elf_emit_byte_view_ptr(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag);

static bool elf_emit_json_parse_bytes_call(ZBuf *code, const IrFunction *fun, const IrValue *value, ElfEmitContext *ctx, ZDiag *diag) {
  if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
  elf_emit_push_rax(code);
  if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
  elf_emit_push_rax(code);
  elf_emit_pop_reg64(code, 6);
  elf_emit_pop_reg64(code, 7);
  size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
  return elf_record_runtime_json_parse_bytes_patch(ctx, patch, diag, value);
}

static bool elf_emit_byte_view_len(ZBuf *code, const IrFunction *fun, const IrValue *view, ElfEmitContext *ctx, ZDiag *diag) {
  unsigned len = 0;
  if (elf_byte_view_const_len(fun, view, &len)) {
    elf_append_u8(code, 0xb8);
    elf_append_u32(code, len);
    return true;
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->index && view->right) {
    unsigned start = 0;
    unsigned end = 0;
    if (elf_const_u32_value(view->index, &start) && elf_const_u32_value(view->right, &end) && start <= end) {
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, end - start);
      return true;
    }
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->right) {
    unsigned start = 0;
    if ((!view->index || elf_const_u32_value(view->index, &start)) &&
        !elf_const_u32_value(view->right, NULL)) {
      if (!elf_emit_value(code, fun, view->right, ctx, diag)) return false;
      if (start > 0) {
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x2d);
        elf_append_u32(code, start);
      }
      return true;
    }
  }
  if (view && view->kind == IR_VALUE_BYTE_SLICE && view->index && view->right) {
    if (!elf_emit_value(code, fun, view->index, ctx, diag)) return false;
    elf_emit_push_rax(code);
    if (!elf_emit_value(code, fun, view->right, ctx, diag)) return false;
    elf_emit_pop_reg64(code, 1);
    elf_append_u8(code, 0x48);
    elf_append_u8(code, 0x29);
    elf_append_u8(code, 0xc8);
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
    elf_append_u8(code, 0x50);
    if (view->index) {
      if (!elf_emit_value(code, fun, view->index, ctx, diag)) return false;
    } else {
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
    }
    elf_append_u8(code, 0x59);
    elf_append_u8(code, 0x48);
    elf_append_u8(code, 0x01);
    elf_append_u8(code, 0xc8);
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
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0xb8);
        elf_append_u64(code, (uint64_t)value->int_value);
      } else {
        elf_append_u8(code, 0xb8);
        elf_append_u32(code, (uint32_t)value->int_value);
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
      elf_append_u8(code, 0x50);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      if (wide) elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc1);
      elf_append_u8(code, 0x58);
      if (value->binary_op == IR_BIN_ADD) {
        if (wide) elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x01);
        elf_append_u8(code, 0xc8);
      } else if (value->binary_op == IR_BIN_SUB) {
        if (wide) elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x29);
        elf_append_u8(code, 0xc8);
      } else if (value->binary_op == IR_BIN_MUL) {
        if (wide) elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x0f);
        elf_append_u8(code, 0xaf);
        elf_append_u8(code, 0xc1);
      } else if (value->binary_op == IR_BIN_AND || value->binary_op == IR_BIN_OR) {
        if (wide) elf_append_u8(code, 0x48);
        elf_append_u8(code, value->binary_op == IR_BIN_AND ? 0x21 : 0x09);
        elf_append_u8(code, 0xc8);
      } else if (value->binary_op == IR_BIN_DIV) {
        if (elf_type_is_unsigned(value->type)) {
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0x31);
          elf_append_u8(code, 0xd2);
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0xf7);
          elf_append_u8(code, 0xf1);
        } else {
          if (wide) {
            elf_append_u8(code, 0x48);
            elf_append_u8(code, 0x99);
          } else {
            elf_append_u8(code, 0x99);
          }
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0xf7);
          elf_append_u8(code, 0xf9);
        }
      } else if (value->binary_op == IR_BIN_MOD) {
        if (elf_type_is_unsigned(value->type)) {
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0x31);
          elf_append_u8(code, 0xd2);
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0xf7);
          elf_append_u8(code, 0xf1);
        } else {
          if (wide) {
            elf_append_u8(code, 0x48);
            elf_append_u8(code, 0x99);
          } else {
            elf_append_u8(code, 0x99);
          }
          if (wide) elf_append_u8(code, 0x48);
          elf_append_u8(code, 0xf7);
          elf_append_u8(code, 0xf9);
        }
        if (wide) elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xd0);
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
      elf_append_u8(code, 0x50);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      if (wide) elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc1);
      elf_append_u8(code, 0x58);
      if (wide) elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc8);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, elf_setcc_opcode(value->compare_op, elf_type_is_unsigned(value->left->type)));
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb6);
      elf_append_u8(code, 0xc0);
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
        elf_emit_pop_reg64(code, param_regs[i - 1]);
      }
      size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
      return elf_record_call_patch(ctx, patch, value->callee_index, diag, value);
    }
    case IR_VALUE_JSON_PARSE_BYTES:
      return elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag);
    case IR_VALUE_JSON_VALIDATE_BYTES:
      if (!elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xf8);
      elf_append_u8(code, 0x00);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x9d);
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb6);
      elf_append_u8(code, 0xc0);
      return true;
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES: {
      if (!elf_emit_json_parse_bytes_call(code, fun, value, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t ok = elf_emit_jcc32_placeholder(code, 0x89);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc0);
      elf_patch_rel32(code, ok, code->len);
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
      elf_emit_pop_reg64(code, 8);
      elf_emit_pop_reg64(code, 1);
      elf_emit_pop_reg64(code, 2);
      elf_emit_pop_reg64(code, 6);
      elf_emit_pop_reg64(code, 7);
      size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
      return elf_record_runtime_http_fetch_patch(ctx, patch, diag, value);
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
      elf_emit_pop_reg64(code, 7);
      size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
      if (value->kind == IR_VALUE_HTTP_RESULT_OK) return elf_record_runtime_http_result_ok_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_RESULT_STATUS) return elf_record_runtime_http_result_status_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_RESULT_BODY_LEN) return elf_record_runtime_http_result_body_len_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_RESULT_ERROR) return elf_record_runtime_http_result_error_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_HEADER_FOUND) return elf_record_runtime_http_header_found_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_HEADER_OFFSET) return elf_record_runtime_http_header_offset_patch(ctx, patch, diag, value);
      return elf_record_runtime_http_header_len_patch(ctx, patch, diag, value);
    }
    case IR_VALUE_HTTP_RESPONSE_LEN:
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET: {
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_emit_push_rax(code);
      elf_emit_pop_reg64(code, 6);
      elf_emit_pop_reg64(code, 7);
      size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
      if (value->kind == IR_VALUE_HTTP_RESPONSE_LEN) return elf_record_runtime_http_response_len_patch(ctx, patch, diag, value);
      if (value->kind == IR_VALUE_HTTP_RESPONSE_HEADERS_LEN) return elf_record_runtime_http_response_headers_len_patch(ctx, patch, diag, value);
      return elf_record_runtime_http_response_body_offset_patch(ctx, patch, diag, value);
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
      elf_emit_pop_reg64(code, 1);
      elf_emit_pop_reg64(code, 2);
      elf_emit_pop_reg64(code, 6);
      elf_emit_pop_reg64(code, 7);
      size_t patch = elf_emit_jmp32_placeholder(code, 0xe8);
      return elf_record_runtime_http_header_value_patch(ctx, patch, diag, value);
    }
    case IR_VALUE_ARGS_LEN:
      if (ctx && ctx->seed_main_process_args) {
        elf_emit_push_reg64(code, 14);
        elf_emit_pop_reg64(code, 0);
        return true;
      }
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x07);
      return true;
    case IR_VALUE_TIME_WALL_SECONDS:
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 201);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      return true;
    case IR_VALUE_TIME_MONOTONIC:
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xec);
      elf_append_u8(code, 0x10);
      elf_append_u8(code, 0xbf);
      elf_append_u32(code, 1);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xe6);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 228);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x04);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x69);
      elf_append_u8(code, 0xc0);
      elf_append_u32(code, 1000000000u);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x03);
      elf_append_u8(code, 0x44);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x08);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xc4);
      elf_append_u8(code, 0x10);
      return true;
    case IR_VALUE_TIME_AS_MS:
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xc1);
      elf_append_u32(code, 1000000u);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x99);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xf7);
      elf_append_u8(code, 0xf9);
      return true;
    case IR_VALUE_RAND_NEXT_U32:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.rand.nextU32 local is out of range", value->line, value->column, "invalid RandSource");
      elf_emit_load_local_rax(code, fun, value->local_index);
      elf_append_u8(code, 0x69);
      elf_append_u8(code, 0xc0);
      elf_append_u32(code, 1664525u);
      elf_append_u8(code, 0x05);
      elf_append_u32(code, 1013904223u);
      elf_emit_store_local_from_reg(code, fun, value->local_index, 0);
      return true;
    case IR_VALUE_RAND_ENTROPY_U32:
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 201);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x35);
      elf_append_u32(code, 0x9e3779b9u);
      return true;
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE: {
      bool create = value->kind == IR_VALUE_FS_CREATE;
      if (!elf_emit_openat_path(code, fun, value->left, create ? 577 : 0, create ? 0644 : 0, ctx, diag)) return false;
      if (value->type == IR_TYPE_I64) {
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x85);
        elf_append_u8(code, 0xc0);
        size_t fail = elf_emit_js_placeholder(code);
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xc0);
        size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
        elf_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        elf_patch_rel32(code, end, code->len);
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
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t fail = elf_emit_js_placeholder(code);
      elf_emit_close_rax_fd(code);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, fail, code->len);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
      elf_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_REMOVE:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 87);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_REMOVE_DIR:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 84);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_MAKE_DIR:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xbe);
      elf_append_u32(code, 0755);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 83);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_RENAME:
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc6);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 82);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_emit_bool_from_nonnegative_rax(code);
      return true;
    case IR_VALUE_FS_DIR_ENTRY_COUNT: {
      if (!elf_emit_openat_path(code, fun, value->left, 65536, 0, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t open_fail = elf_emit_js_placeholder(code);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x81);
      elf_append_u8(code, 0xec);
      elf_append_u32(code, 1040);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1024);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1032);
      elf_append_u32(code, 0);
      size_t read_loop = code->len;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0xbc);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1024);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8d);
      elf_append_u8(code, 0x34);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0xba);
      elf_append_u32(code, 1024);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 217);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t read_fail = elf_emit_js_placeholder(code);
      size_t done = elf_emit_jcc32_placeholder(code, 0x84);
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xe1);
      elf_append_u8(code, 0x4c);
      elf_append_u8(code, 0x8d);
      elf_append_u8(code, 0x04);
      elf_append_u8(code, 0x04);
      size_t scan_loop = code->len;
      elf_append_u8(code, 0x4d);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc1);
      size_t scan_done = elf_emit_jcc32_placeholder(code, 0x83);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1032);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb7);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0x10);
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x01);
      elf_append_u8(code, 0xc1);
      size_t scan_back = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, scan_back, scan_loop);
      elf_patch_rel32(code, scan_done, code->len);
      size_t loop_back = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, loop_back, read_loop);
      elf_patch_rel32(code, done, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1024);
      elf_emit_close_rax_fd(code);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1032);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x81);
      elf_append_u8(code, 0xc4);
      elf_append_u32(code, 1040);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, read_fail, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x84);
      elf_append_u8(code, 0x24);
      elf_append_u32(code, 1024);
      elf_emit_close_rax_fd(code);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x81);
      elf_append_u8(code, 0xc4);
      elf_append_u32(code, 1040);
      elf_patch_rel32(code, open_fail, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xc0);
      elf_append_u32(code, 0xffffffffu);
      elf_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_ATOMIC_WRITE: {
      if (!value->left || !value->right || !value->index) return elf_diag(diag, "direct ELF64 std.fs.atomicWrite requires path, temp path, and bytes", value->line, value->column, "missing argument");
      if (!elf_emit_openat_path(code, fun, value->right, 577, 0644, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t open_fail = elf_emit_js_placeholder(code);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->index, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->index, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x74);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x08);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x7c);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x10);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t write_fail = elf_emit_js_placeholder(code);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x3b);
      elf_append_u8(code, 0x04);
      elf_append_u8(code, 0x24);
      size_t short_write = elf_emit_jcc32_placeholder(code, 0x85);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x7c);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x10);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 3);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xc4);
      elf_append_u8(code, 0x18);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t close_fail = elf_emit_js_placeholder(code);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc6);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 82);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_emit_bool_from_nonnegative_rax(code);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, write_fail, code->len);
      elf_patch_rel32(code, short_write, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x8b);
      elf_append_u8(code, 0x7c);
      elf_append_u8(code, 0x24);
      elf_append_u8(code, 0x10);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 3);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xc4);
      elf_append_u8(code, 0x18);
      elf_patch_rel32(code, open_fail, code->len);
      elf_patch_rel32(code, close_fail, code->len);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
      elf_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_FILE_LEN:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.fileLen local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xf6);
      elf_append_u8(code, 0xba);
      elf_append_u32(code, 2);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 8);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      if (value->type == IR_TYPE_I64) {
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x85);
        elf_append_u8(code, 0xc0);
        size_t fail = elf_emit_js_placeholder(code);
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xc0);
        size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
        elf_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        elf_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_READ_FILE:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.read local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      if (value->type == IR_TYPE_I64) {
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x85);
        elf_append_u8(code, 0xc0);
        size_t fail = elf_emit_js_placeholder(code);
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xc0);
        size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
        elf_patch_rel32(code, fail, code->len);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        elf_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_WRITE_ALL_FILE:
      if (value->local_index >= fun->local_len) return elf_diag(diag, "direct ELF64 std.fs.writeAll local is out of range", value->line, value->column, "invalid File");
      elf_emit_load_local_rax(code, fun, value->local_index);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x59);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc8);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x94);
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb6);
      elf_append_u8(code, 0xc0);
      if (value->type == IR_TYPE_I64) {
        elf_append_u8(code, 0x85);
        elf_append_u8(code, 0xc0);
        size_t success = elf_emit_jcc32_placeholder(code, 0x85);
        elf_emit_packed_error_rax(code, value->error_code ? value->error_code : IR_ERROR_UNKNOWN);
        size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
        elf_patch_rel32(code, success, code->len);
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x31);
        elf_append_u8(code, 0xc0);
        elf_patch_rel32(code, end, code->len);
      }
      return true;
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH: {
      if (!elf_emit_openat_path(code, fun, value->left, 0, 0, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t open_fail = elf_emit_js_placeholder(code);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x50);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xf8);
      elf_emit_close_rax_fd(code);
      elf_append_u8(code, 0x58);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, open_fail, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xc0);
      elf_append_u32(code, 0xffffffffu);
      elf_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH: {
      if (!elf_emit_openat_path(code, fun, value->left, 577, 0644, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x85);
      elf_append_u8(code, 0xc0);
      size_t open_fail = elf_emit_js_placeholder(code);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x05);
      elf_append_u8(code, 0x50);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xf8);
      elf_emit_close_rax_fd(code);
      elf_append_u8(code, 0x58);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, open_fail, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xc7);
      elf_append_u8(code, 0xc0);
      elf_append_u32(code, 0xffffffffu);
      elf_patch_rel32(code, end, code->len);
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
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc8);
      size_t ok_patch = elf_emit_jcc32_placeholder(code, 0x82);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
      size_t end_patch = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, ok_patch, code->len);
      elf_append_u8(code, 0x50);
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x59);
      elf_emit_load_local_slot_reg(code, local, 0, 2, true);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x01);
      elf_append_u8(code, 0xca);
      elf_append_u8(code, 0x88);
      elf_append_u8(code, 0x02);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc8);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xc0);
      elf_append_u8(code, 0x01);
      elf_emit_store_local_slot_reg(code, local, 8, 0, false);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      elf_patch_rel32(code, end_patch, code->len);
      return true;
    }
    case IR_VALUE_CHECK: {
      if (!value->left || value->left->type != IR_TYPE_I64) return elf_diag(diag, "direct ELF64 check requires a packed fallible call result", value->line, value->column, "non-fallible value");
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_emit_error_condition_from_rax(code);
      size_t ok_patch = elf_emit_jcc32_placeholder(code, 0x84);
      if (elf_function_propagates_to_process_exit(fun)) {
        elf_emit_epilogue(code, fun, ctx);
      } else {
        elf_append_u8(code, 0xb8);
        elf_append_u32(code, 1);
        elf_emit_epilogue(code, fun, ctx);
      }
      elf_patch_rel32(code, ok_patch, code->len);
      if (!elf_type_is_i64(value->type)) {
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xc0);
      }
      return true;
    }
    case IR_VALUE_RESCUE: {
      if (!value->left || !value->right || value->left->type != IR_TYPE_I64) {
        return elf_diag(diag, "direct ELF64 rescue requires a packed fallible call and fallback", value->line, value->column, "unsupported rescue");
      }
      if (!elf_emit_value(code, fun, value->left, ctx, diag)) return false;
      elf_emit_error_condition_from_rax(code);
      size_t success_patch = elf_emit_jcc32_placeholder(code, 0x84);
      if (!elf_emit_value(code, fun, value->right, ctx, diag)) return false;
      size_t end_patch = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, success_patch, code->len);
      if (!elf_type_is_i64(value->type)) {
        elf_append_u8(code, 0x89);
        elf_append_u8(code, 0xc0);
      }
      elf_patch_rel32(code, end_patch, code->len);
      return true;
    }
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= fun->local_len) return elf_diag(diag, "direct ELF64 indexed load array is out of range", value->line, value->column, "invalid array local");
      const IrLocal *local = &fun->locals[value->array_index];
      if (!elf_emit_bounds_checked_address(code, fun, local, value->index, ctx, diag)) return false;
      if (local->element_type == IR_TYPE_U8) {
        elf_append_u8(code, 0x0f);
        elf_append_u8(code, 0xb6);
        elf_append_u8(code, 0x00);
      } else if (elf_type_is_i64(local->element_type)) {
        elf_append_u8(code, 0x48);
        elf_append_u8(code, 0x8b);
        elf_append_u8(code, 0x00);
      } else {
        elf_append_u8(code, 0x8b);
        elf_append_u8(code, 0x00);
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
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc1);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0xffffffffu);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc9);
      size_t byte_loop = code->len;
      elf_append_u8(code, 0x4c);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc9);
      size_t done = elf_emit_jcc32_placeholder(code, 0x83);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb6);
      elf_append_u8(code, 0x14);
      elf_append_u8(code, 0x0e);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xd0);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 8);
      size_t bit_loop = code->len;
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x83);
      elf_append_u8(code, 0xe2);
      elf_append_u8(code, 1);
      elf_append_u8(code, 0xf7);
      elf_append_u8(code, 0xda);
      elf_append_u8(code, 0x81);
      elf_append_u8(code, 0xe2);
      elf_append_u32(code, 0xedb88320u);
      elf_append_u8(code, 0xd1);
      elf_append_u8(code, 0xe8);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xd0);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xc8);
      size_t bit_back = elf_emit_jcc32_placeholder(code, 0x85);
      elf_patch_rel32(code, bit_back, bit_loop);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xc1);
      size_t byte_back = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, byte_back, byte_loop);
      elf_patch_rel32(code, done, code->len);
      elf_append_u8(code, 0x35);
      elf_append_u32(code, 0xffffffffu);
      return true;
    }
    case IR_VALUE_BYTE_COPY: {
      if (!value->left || !value->right) return elf_diag(diag, "direct ELF64 byte copy requires source and destination byte views", value->line, value->column, "missing byte view");
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x5f);
      elf_append_u8(code, 0x59);
      elf_append_u8(code, 0x5e);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc8);
      size_t keep_dst_len = elf_emit_jcc32_placeholder(code, 0x86);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc8);
      elf_patch_rel32(code, keep_dst_len, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      elf_append_u8(code, 0x45);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc0);
      size_t loop = code->len;
      elf_append_u8(code, 0x4c);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc2);
      size_t done = elf_emit_jcc32_placeholder(code, 0x86);
      elf_append_u8(code, 0x42);
      elf_append_u8(code, 0x8a);
      elf_append_u8(code, 0x1c);
      elf_append_u8(code, 0x06);
      elf_append_u8(code, 0x42);
      elf_append_u8(code, 0x88);
      elf_append_u8(code, 0x1c);
      elf_append_u8(code, 0x07);
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xc0);
      size_t back = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, back, loop);
      elf_patch_rel32(code, done, code->len);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xd0);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_EQ: {
      if (!value->left || !value->right) return elf_diag(diag, "direct ELF64 byte-view equality requires two byte views", value->line, value->column, "missing byte view");
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x59);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc1);
      size_t same_len = elf_emit_jcc32_placeholder(code, 0x84);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
      size_t end = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, same_len, code->len);
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc2);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc0);
      if (!elf_emit_byte_view_ptr(code, fun, value->right, ctx, diag)) return false;
      elf_append_u8(code, 0x49);
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc1);
      elf_append_u8(code, 0x31);
      elf_append_u8(code, 0xc9);
      size_t loop = code->len;
      elf_append_u8(code, 0x4c);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xd1);
      size_t equal = elf_emit_jcc32_placeholder(code, 0x83);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0x8a);
      elf_append_u8(code, 0x04);
      elf_append_u8(code, 0x08);
      elf_append_u8(code, 0x41);
      elf_append_u8(code, 0x38);
      elf_append_u8(code, 0x04);
      elf_append_u8(code, 0x09);
      size_t mismatch = elf_emit_jcc32_placeholder(code, 0x85);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0xff);
      elf_append_u8(code, 0xc1);
      size_t back = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, back, loop);
      elf_patch_rel32(code, mismatch, code->len);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 0);
      size_t after_false = elf_emit_jmp32_placeholder(code, 0xe9);
      elf_patch_rel32(code, equal, code->len);
      elf_append_u8(code, 0xb8);
      elf_append_u32(code, 1);
      elf_patch_rel32(code, after_false, code->len);
      elf_patch_rel32(code, end, code->len);
      return true;
    }
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD: {
      unsigned const_index = 0;
      unsigned char byte = 0;
      if (elf_const_u32_value(value->index, &const_index) &&
          elf_byte_view_const_byte(ctx ? ctx->ir : NULL, fun, value->left, const_index, &byte)) {
        elf_append_u8(code, 0xb8);
        elf_append_u32(code, byte);
        return true;
      }
      if (!value->index || !elf_emit_value(code, fun, value->index, ctx, diag)) return false;
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_len(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x89);
      elf_append_u8(code, 0xc1);
      elf_append_u8(code, 0x58);
      elf_append_u8(code, 0x39);
      elf_append_u8(code, 0xc8);
      size_t ok_patch = elf_emit_jcc32_placeholder(code, 0x82);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0x0b);
      elf_patch_rel32(code, ok_patch, code->len);
      elf_append_u8(code, 0x50);
      if (!elf_emit_byte_view_ptr(code, fun, value->left, ctx, diag)) return false;
      elf_append_u8(code, 0x59);
      elf_append_u8(code, 0x48);
      elf_append_u8(code, 0x01);
      elf_append_u8(code, 0xc8);
      elf_append_u8(code, 0x0f);
      elf_append_u8(code, 0xb6);
      elf_append_u8(code, 0x00);
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
  elf_append_u8(text, 0x50);
  if (!elf_emit_byte_view_len(text, fun, instr->value, ctx, diag)) return false;
  elf_append_u8(text, 0x50);
  elf_emit_pop_reg64(text, 2);
  elf_emit_pop_reg64(text, 6);
  elf_append_u8(text, 0xbf);
  elf_append_u32(text, instr->field_offset == 2 ? 2 : 1);
  elf_append_u8(text, 0xb8);
  elf_append_u32(text, 1);
  elf_append_u8(text, 0x0f);
  elf_append_u8(text, 0x05);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xc0);
  size_t ok_patch = elf_emit_jcc32_placeholder(text, 0x89);
  elf_append_u8(text, 0x0f);
  elf_append_u8(text, 0x0b);
  elf_patch_rel32(text, ok_patch, text->len);
  return true;
}

static bool elf_emit_args_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, ElfEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return elf_diag(diag, "direct ELF64 std.args.get requires an index", value ? value->line : 1, value ? value->column : 1, "missing index");
  if (!elf_emit_value(text, fun, value->left, ctx, diag)) return false;
  if (ctx && ctx->seed_main_process_args) {
    elf_emit_push_reg64(text, 14);
    elf_emit_pop_reg64(text, 1);
    elf_append_u8(text, 0x48);
    elf_append_u8(text, 0x39);
    elf_append_u8(text, 0xc8);
  } else {
    elf_append_u8(text, 0x49);
    elf_append_u8(text, 0x3b);
    elf_append_u8(text, 0x07);
  }
  size_t in_range = elf_emit_jcc32_placeholder(text, 0x82);
  elf_emit_maybe_clear(text, local);
  size_t end = elf_emit_jmp32_placeholder(text, 0xe9);
  elf_patch_rel32(text, in_range, text->len);

  if (ctx && ctx->seed_main_process_args) {
    elf_append_u8(text, 0x49);
    elf_append_u8(text, 0x8b);
    elf_append_u8(text, 0x04);
    elf_append_u8(text, 0xc7);
  } else {
    elf_append_u8(text, 0x49);
    elf_append_u8(text, 0x8b);
    elf_append_u8(text, 0x44);
    elf_append_u8(text, 0xc7);
    elf_append_u8(text, 0x08);
  }
  elf_append_u8(text, 0x50);
  elf_emit_strlen_rax_to_ecx(text);
  elf_append_u8(text, 0xb8);
  elf_append_u32(text, 1);
  elf_emit_store_local_slot_reg(text, local, 0, 0, false);
  elf_append_u8(text, 0x58);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_store_local_slot_reg(text, local, 16, 1, false);
  elf_patch_rel32(text, end, text->len);
  return true;
}

static bool elf_emit_env_get_to_local(ZBuf *text, const IrFunction *fun, const IrValue *value, const IrLocal *local, ElfEmitContext *ctx, ZDiag *diag) {
  if (!value || !value->left) return elf_diag(diag, "direct ELF64 std.env.get requires a key", value ? value->line : 1, value ? value->column : 1, "missing key");
  if (!elf_emit_byte_view_ptr(text, fun, value->left, ctx, diag)) return false;
  elf_append_u8(text, 0x49);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xc1);
  if (!elf_emit_byte_view_len(text, fun, value->left, ctx, diag)) return false;
  elf_append_u8(text, 0x49);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xc2);

  if (ctx && ctx->seed_main_process_args) {
    elf_emit_push_reg64(text, 13);
    elf_emit_pop_reg64(text, 8);
  } else {
    elf_append_u8(text, 0x4d);
    elf_append_u8(text, 0x8b);
    elf_append_u8(text, 0x07);
    elf_append_u8(text, 0x49);
    elf_append_u8(text, 0x83);
    elf_append_u8(text, 0xc0);
    elf_append_u8(text, 0x02);
    elf_append_u8(text, 0x49);
    elf_append_u8(text, 0xc1);
    elf_append_u8(text, 0xe0);
    elf_append_u8(text, 0x03);
    elf_append_u8(text, 0x4d);
    elf_append_u8(text, 0x01);
    elf_append_u8(text, 0xf8);
  }

  size_t env_loop = text->len;
  elf_append_u8(text, 0x49);
  elf_append_u8(text, 0x8b);
  elf_append_u8(text, 0x18);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xdb);
  size_t none = elf_emit_jcc32_placeholder(text, 0x84);
  elf_append_u8(text, 0x31);
  elf_append_u8(text, 0xc9);

  size_t compare_loop = text->len;
  elf_append_u8(text, 0x4c);
  elf_append_u8(text, 0x39);
  elf_append_u8(text, 0xd1);
  size_t key_done = elf_emit_jcc32_placeholder(text, 0x83);
  elf_append_u8(text, 0x41);
  elf_append_u8(text, 0x8a);
  elf_append_u8(text, 0x04);
  elf_append_u8(text, 0x09);
  elf_append_u8(text, 0x38);
  elf_append_u8(text, 0x04);
  elf_append_u8(text, 0x0b);
  size_t next = elf_emit_jcc32_placeholder(text, 0x85);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0xff);
  elf_append_u8(text, 0xc1);
  size_t compare_back = elf_emit_jmp32_placeholder(text, 0xe9);
  elf_patch_rel32(text, compare_back, compare_loop);

  elf_patch_rel32(text, key_done, text->len);
  elf_append_u8(text, 0x42);
  elf_append_u8(text, 0x80);
  elf_append_u8(text, 0x3c);
  elf_append_u8(text, 0x13);
  elf_append_u8(text, 0x3d);
  size_t next_after_key = elf_emit_jcc32_placeholder(text, 0x85);
  elf_append_u8(text, 0x4a);
  elf_append_u8(text, 0x8d);
  elf_append_u8(text, 0x44);
  elf_append_u8(text, 0x13);
  elf_append_u8(text, 0x01);
  elf_append_u8(text, 0x50);
  elf_emit_strlen_rax_to_ecx(text);
  elf_append_u8(text, 0xb8);
  elf_append_u32(text, 1);
  elf_emit_store_local_slot_reg(text, local, 0, 0, false);
  elf_append_u8(text, 0x58);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_store_local_slot_reg(text, local, 16, 1, false);
  size_t end = elf_emit_jmp32_placeholder(text, 0xe9);

  elf_patch_rel32(text, next, text->len);
  elf_patch_rel32(text, next_after_key, text->len);
  elf_append_u8(text, 0x49);
  elf_append_u8(text, 0x83);
  elf_append_u8(text, 0xc0);
  elf_append_u8(text, 0x08);
  size_t loop_back = elf_emit_jmp32_placeholder(text, 0xe9);
  elf_patch_rel32(text, loop_back, env_loop);

  elf_patch_rel32(text, none, text->len);
  elf_emit_maybe_clear(text, local);
  elf_patch_rel32(text, end, text->len);
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
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xc0);
  size_t open_fail = elf_emit_js_placeholder(text);

  elf_append_u8(text, 0x50);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xc7);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x31);
  elf_append_u8(text, 0xf6);
  elf_append_u8(text, 0xba);
  elf_append_u32(text, 2);
  elf_append_u8(text, 0xb8);
  elf_append_u32(text, 8);
  elf_append_u8(text, 0x0f);
  elf_append_u8(text, 0x05);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xc0);
  size_t tell_fail = elf_emit_js_placeholder(text);
  if (value->right) {
    elf_append_u8(text, 0x50);
    if (!elf_emit_value(text, fun, value->right, ctx, diag)) return false;
    elf_append_u8(text, 0x59);
    elf_append_u8(text, 0x48);
    elf_append_u8(text, 0x39);
    elf_append_u8(text, 0xc1);
    size_t size_ok = elf_emit_jcc32_placeholder(text, 0x83);
    elf_append_u8(text, 0x58);
    elf_emit_close_rax_fd(text);
    elf_emit_packed_error_rax(text, IR_ERROR_TOO_LARGE);
    if (!fun->raises) {
      elf_append_u8(text, 0xb8);
      elf_append_u32(text, 1);
    }
    elf_emit_epilogue(text, fun, ctx);
    elf_patch_rel32(text, size_ok, text->len);
  }
  elf_append_u8(text, 0x58);

  elf_append_u8(text, 0x50);
  elf_append_u8(text, 0x50);
  elf_emit_load_local_slot_reg(text, alloc, 0, 6, true);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x01);
  elf_append_u8(text, 0xce);
  elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
  elf_append_u8(text, 0x29);
  elf_append_u8(text, 0xca);
  if (value->right) {
    if (!elf_emit_value(text, fun, value->right, ctx, diag)) return false;
    elf_append_u8(text, 0x39);
    elf_append_u8(text, 0xc2);
    size_t keep_capacity = elf_emit_jcc32_placeholder(text, 0x86);
    elf_append_u8(text, 0x89);
    elf_append_u8(text, 0xc2);
    elf_patch_rel32(text, keep_capacity, text->len);
  }
  elf_append_u8(text, 0x5f);
  elf_append_u8(text, 0x31);
  elf_append_u8(text, 0xc0);
  elf_append_u8(text, 0x0f);
  elf_append_u8(text, 0x05);
  elf_append_u8(text, 0x50);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x8b);
  elf_append_u8(text, 0x44);
  elf_append_u8(text, 0x24);
  elf_append_u8(text, 0x08);
  elf_emit_close_rax_fd(text);
  elf_append_u8(text, 0x58);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x83);
  elf_append_u8(text, 0xc4);
  elf_append_u8(text, 0x08);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xc0);
  size_t read_fail = elf_emit_js_placeholder(text);

  elf_append_u8(text, 0x50);
  elf_emit_load_local_slot_rax(text, alloc, 0);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x01);
  elf_append_u8(text, 0xc8);
  elf_emit_store_local_slot_rax(text, local, 0);
  elf_append_u8(text, 0x58);
  elf_emit_store_local_slot_rax(text, local, 8);
  elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
  elf_append_u8(text, 0x01);
  elf_append_u8(text, 0xc8);
  elf_emit_store_local_slot_reg(text, alloc, 12, 0, false);
  size_t end = elf_emit_jmp32_placeholder(text, 0xe9);

  elf_patch_rel32(text, open_fail, text->len);
  elf_emit_packed_error_rax(text, IR_ERROR_NOT_FOUND);
  if (!fun->raises) {
    elf_append_u8(text, 0xb8);
    elf_append_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);

  elf_patch_rel32(text, tell_fail, text->len);
  elf_append_u8(text, 0x58);
  elf_emit_close_rax_fd(text);
  elf_emit_packed_error_rax(text, IR_ERROR_IO);
  if (!fun->raises) {
    elf_append_u8(text, 0xb8);
    elf_append_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);

  elf_patch_rel32(text, read_fail, text->len);
  elf_emit_packed_error_rax(text, IR_ERROR_IO);
  if (!fun->raises) {
    elf_append_u8(text, 0xb8);
    elf_append_u32(text, 1);
  }
  elf_emit_epilogue(text, fun, ctx);
  elf_patch_rel32(text, end, text->len);
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
      elf_append_u8(text, 0xb8);
      elf_append_u32(text, 0);
      elf_emit_store_local_slot_reg(text, local, 12, 0, false);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_VEC) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!instr->value || instr->value->kind != IR_VALUE_VEC_INIT) return elf_diag(diag, "direct ELF64 Vec local requires std.mem.vec", instr->line, instr->column, "unsupported Vec initializer");
      if (!elf_emit_byte_view_ptr(text, fun, instr->value->left, ctx, diag)) return false;
      elf_emit_store_local_slot_rax(text, local, 0);
      elf_append_u8(text, 0xb8);
      elf_append_u32(text, 0);
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
          elf_append_u8(text, 0xc6);
          elf_append_u8(text, 0x80);
          elf_append_u32(text, i);
          elf_append_u8(text, byte);
        }
        const unsigned char suffix[] = {'-', 't', 'm', 'p', 0};
        for (unsigned i = 0; i < sizeof(suffix); i++) {
          elf_append_u8(text, 0xc6);
          elf_append_u8(text, 0x80);
          elf_append_u32(text, prefix_len + i);
          elf_append_u8(text, suffix[i]);
        }
        elf_append_u8(text, 0x50);
        elf_append_u8(text, 0xb8);
        elf_append_u32(text, 1);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        elf_append_u8(text, 0x58);
        elf_emit_store_local_slot_rax(text, local, 8);
        elf_append_u8(text, 0xb8);
        elf_append_u32(text, total_len);
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
        elf_append_u8(text, 0x48);
        elf_append_u8(text, 0x85);
        elf_append_u8(text, 0xc0);
        size_t open_fail = elf_emit_js_placeholder(text);
        elf_append_u8(text, 0x50);
        elf_emit_load_local_slot_reg(text, alloc, 0, 6, true);
        elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
        elf_append_u8(text, 0x5f);
        elf_append_u8(text, 0x31);
        elf_append_u8(text, 0xc0);
        elf_append_u8(text, 0x0f);
        elf_append_u8(text, 0x05);
        elf_append_u8(text, 0x50);
        elf_emit_load_local_rax(text, fun, instr->value->local_index);
        (void)alloc;
        elf_append_u8(text, 0x48);
        elf_append_u8(text, 0x89);
        elf_append_u8(text, 0xf8);
        elf_emit_close_rax_fd(text);
        elf_append_u8(text, 0x58);
        elf_append_u8(text, 0x48);
        elf_append_u8(text, 0x85);
        elf_append_u8(text, 0xc0);
        size_t read_fail = elf_emit_js_placeholder(text);
        elf_append_u8(text, 0x50);
        elf_append_u8(text, 0xb8);
        elf_append_u32(text, 1);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        elf_emit_load_local_slot_reg(text, alloc, 0, 0, true);
        elf_emit_store_local_slot_reg(text, local, 8, 0, true);
        elf_append_u8(text, 0x58);
        elf_emit_store_local_slot_reg(text, local, 16, 0, false);
        elf_emit_store_local_slot_reg(text, alloc, 12, 0, false);
        size_t end = elf_emit_jmp32_placeholder(text, 0xe9);
        elf_patch_rel32(text, open_fail, text->len);
        elf_patch_rel32(text, read_fail, text->len);
        elf_emit_maybe_clear(text, local);
        elf_patch_rel32(text, end, text->len);
        return true;
      }
      if (!instr->value || instr->value->kind != IR_VALUE_ALLOC_BYTES || instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) return elf_diag(diag, "direct ELF64 allocation source is invalid", instr->line, instr->column, "invalid allocation");
      const IrLocal *alloc = &fun->locals[instr->value->local_index];
      if (!elf_emit_value(text, fun, instr->value->left, ctx, diag)) return false;
      elf_append_u8(text, 0x50);
      elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
      elf_emit_load_local_slot_reg(text, alloc, 0, 2, true);
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x01);
      elf_append_u8(text, 0xca);
      elf_append_u8(text, 0xb8);
      elf_append_u32(text, 1);
      elf_emit_store_local_slot_reg(text, local, 0, 0, false);
      elf_emit_store_local_slot_reg(text, local, 8, 2, true);
      elf_append_u8(text, 0x58);
      elf_emit_store_local_slot_reg(text, local, 16, 0, false);
      elf_append_u8(text, 0x01);
      elf_append_u8(text, 0xc1);
      elf_emit_store_local_slot_reg(text, alloc, 12, 1, false);
      return true;
    }
    if (instr->local_index < fun->local_len && fun->locals[instr->local_index].type == IR_TYPE_MAYBE_SCALAR) {
      const IrLocal *local = &fun->locals[instr->local_index];
      if (!instr->value) return elf_diag(diag, "direct ELF64 Maybe scalar initializer is missing", instr->line, instr->column, "missing maybe value");
      if (instr->value->kind == IR_VALUE_MAYBE_SCALAR_LITERAL) {
        elf_append_u8(text, 0xb8);
        elf_append_u32(text, instr->value->data_len ? 1u : 0u);
        elf_emit_store_local_slot_reg(text, local, 0, 0, false);
        elf_append_u8(text, 0xb8);
        elf_append_u32(text, (uint32_t)instr->value->int_value);
        elf_emit_store_local_slot_reg(text, local, 8, 0, true);
        return true;
      }
      if (instr->value->kind == IR_VALUE_JSON_PARSE_BYTES) {
        if (instr->value->local_index >= fun->local_len || fun->locals[instr->value->local_index].type != IR_TYPE_ALLOC) {
          return elf_diag(diag, "direct ELF64 JSON parse allocator is invalid", instr->line, instr->column, "invalid allocator");
        }
        const IrLocal *alloc = &fun->locals[instr->value->local_index];
        if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
        elf_append_u8(text, 0x48);
        elf_append_u8(text, 0x85);
        elf_append_u8(text, 0xc0);
        size_t fail = elf_emit_js_placeholder(text);
        elf_append_u8(text, 0x50);
        elf_emit_load_local_slot_reg(text, alloc, 12, 1, false);
        elf_append_u8(text, 0x01);
        elf_append_u8(text, 0xc1);
        elf_emit_load_local_slot_reg(text, alloc, 8, 2, false);
        elf_append_u8(text, 0x39);
        elf_append_u8(text, 0xd1);
        size_t overflow = elf_emit_jcc32_placeholder(text, 0x87);
        elf_append_u8(text, 0x58);
        elf_emit_maybe_scalar_store_rax(text, local);
        elf_emit_store_local_slot_reg(text, alloc, 12, 1, false);
        size_t end = elf_emit_jmp32_placeholder(text, 0xe9);
        elf_patch_rel32(text, overflow, text->len);
        elf_append_u8(text, 0x58);
        elf_patch_rel32(text, fail, text->len);
        elf_emit_maybe_scalar_clear(text, local);
        elf_patch_rel32(text, end, text->len);
        return true;
      }
      if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x85);
      elf_append_u8(text, 0xc0);
      size_t fail = elf_emit_js_placeholder(text);
      elf_emit_maybe_scalar_store_rax(text, local);
      size_t end = elf_emit_jmp32_placeholder(text, 0xe9);
      elf_patch_rel32(text, fail, text->len);
      elf_emit_maybe_scalar_clear(text, local);
      elf_patch_rel32(text, end, text->len);
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
    elf_append_u8(text, 0x50);
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    elf_append_u8(text, 0x59);
    if (local->element_type == IR_TYPE_U8) {
      elf_append_u8(text, 0x88);
      elf_append_u8(text, 0x01);
    } else if (elf_type_is_i64(local->element_type)) {
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x89);
      elf_append_u8(text, 0x01);
    } else {
      elf_append_u8(text, 0x89);
      elf_append_u8(text, 0x01);
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
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x31);
      elf_append_u8(text, 0xc0);
    } else if (fun->raises && instr->value && !elf_type_is_i64(instr->value->type)) {
      elf_append_u8(text, 0x89);
      elf_append_u8(text, 0xc0);
    }
    elf_emit_epilogue(text, fun, ctx);
    return true;
  }
  if (instr->kind == IR_INSTR_IF) {
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    elf_append_u8(text, 0x85);
    elf_append_u8(text, 0xc0);
    size_t false_patch = elf_emit_jcc32_placeholder(text, 0x84);
    if (!elf_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    if (instr->else_len > 0) {
      size_t end_patch = elf_emit_jmp32_placeholder(text, 0xe9);
      elf_patch_rel32(text, false_patch, text->len);
      if (!elf_emit_instrs(text, fun, instr->else_instrs, instr->else_len, ctx, diag)) return false;
      elf_patch_rel32(text, end_patch, text->len);
    } else {
      elf_patch_rel32(text, false_patch, text->len);
    }
    return true;
  }
  if (instr->kind == IR_INSTR_WHILE) {
    size_t loop_start = text->len;
    if (!elf_emit_value(text, fun, instr->value, ctx, diag)) return false;
    elf_append_u8(text, 0x85);
    elf_append_u8(text, 0xc0);
    size_t exit_patch = elf_emit_jcc32_placeholder(text, 0x84);
    if (!elf_emit_instrs(text, fun, instr->then_instrs, instr->then_len, ctx, diag)) return false;
    size_t back_patch = elf_emit_jmp32_placeholder(text, 0xe9);
    elf_patch_rel32(text, back_patch, loop_start);
    elf_patch_rel32(text, exit_patch, text->len);
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
  elf_append_u8(text, 0x55);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xe5);
  bool seed_process_args = elf_function_seeds_process_args(fun, ctx);
  unsigned base_stack_size = elf_base_stack_size(fun);
  unsigned stack_size = elf_total_stack_size(fun, ctx);
  if (stack_size > 0) {
    if (stack_size <= 127) {
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x83);
      elf_append_u8(text, 0xec);
      elf_append_u8(text, stack_size);
    } else {
      elf_append_u8(text, 0x48);
      elf_append_u8(text, 0x81);
      elf_append_u8(text, 0xec);
      elf_append_u32(text, stack_size);
    }
  }
  if (seed_process_args) {
    elf_emit_rbp_disp_reg(text, 0x89, 13, base_stack_size + 8, true);
    elf_emit_rbp_disp_reg(text, 0x89, 14, base_stack_size + 16, true);
    elf_emit_rbp_disp_reg(text, 0x89, 15, base_stack_size + 24, true);
    elf_emit_push_reg64(text, 7);
    elf_emit_pop_reg64(text, 14);
    elf_emit_push_reg64(text, 6);
    elf_emit_pop_reg64(text, 15);
    elf_emit_push_reg64(text, 2);
    elf_emit_pop_reg64(text, 13);
  }
  for (size_t i = 0; i < fun->param_count; i++) {
    if (i < 6) {
      elf_emit_store_local_from_reg(text, fun, (unsigned)i, param_regs[i]);
    } else {
      elf_emit_load_rbp_positive_reg(text, 0, 16u + (unsigned)(i - 6u) * 8u, false);
      elf_emit_store_local_from_reg(text, fun, (unsigned)i, 0);
    }
  }
  if (!elf_emit_instrs(text, fun, fun->instrs, fun->instr_len, ctx, diag)) return false;
  if (fun->instr_len == 0 || fun->instrs[fun->instr_len - 1].kind != IR_INSTR_RETURN) elf_emit_epilogue(text, fun, ctx);
  return true;
}

static void elf_append_symbol(ZBuf *symtab, uint32_t name, unsigned char info, uint16_t shndx, uint64_t value, uint64_t size) {
  elf_append_u32(symtab, name);
  elf_append_u8(symtab, info);
  elf_append_u8(symtab, 0);
  elf_append_u16(symtab, shndx);
  elf_append_u64(symtab, value);
  elf_append_u64(symtab, size);
}

static void elf_append_rela(ZBuf *rela, uint64_t offset, uint32_t sym, uint32_t type, int64_t addend) {
  elf_append_u64(rela, offset);
  elf_append_u64(rela, ((uint64_t)sym << 32) | type);
  elf_append_u64(rela, (uint64_t)addend);
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
    elf_pad_to(rodata, segment->offset - base_offset);
    elf_append_bytes(rodata, segment->bytes, segment->len);
  }
}

static void elf_append_section_header(ZBuf *out, uint32_t name, uint32_t type, uint64_t flags, uint64_t offset, uint64_t size, uint32_t link, uint32_t info, uint64_t align, uint64_t entsize) {
  elf_append_u32(out, name);
  elf_append_u32(out, type);
  elf_append_u64(out, flags);
  elf_append_u64(out, 0);
  elf_append_u64(out, offset);
  elf_append_u64(out, size);
  elf_append_u32(out, link);
  elf_append_u32(out, info);
  elf_append_u64(out, align);
  elf_append_u64(out, entsize);
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
  ZBuf shstrtab;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&rela_text);
  zbuf_init(&strtab);
  zbuf_init(&symtab);
  zbuf_init(&shstrtab);
  elf_append_u8(&strtab, 0);
  elf_append_zeros(&symtab, 24);
  bool has_rodata = ir->readonly_data_bytes > 0 || ir->data_segment_len > 0;
  unsigned rodata_base_offset = elf_rodata_base_offset(ir);
  if (has_rodata) {
    elf_append_rodata(&rodata, ir, rodata_base_offset);
    elf_append_symbol(&symtab, 0, 0x03, 2, 0, 0);
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
    zbuf_free(&shstrtab);
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
    elf_pad_to(&text, elf_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!elf_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      free(function_offsets);
      free(function_sizes);
      free(symbol_names);
      free(ctx.call_patches);
      free(ctx.rodata_patches);
      free(ctx.runtime_json_parse_bytes_patches);
      free(ctx.runtime_http_fetch_patches);
      free(ctx.runtime_http_result_ok_patches);
      free(ctx.runtime_http_result_status_patches);
      free(ctx.runtime_http_result_body_len_patches);
      free(ctx.runtime_http_result_error_patches);
      free(ctx.runtime_http_response_len_patches);
      free(ctx.runtime_http_response_headers_len_patches);
      free(ctx.runtime_http_response_body_offset_patches);
      free(ctx.runtime_http_header_value_patches);
      free(ctx.runtime_http_header_found_patches);
      free(ctx.runtime_http_header_offset_patches);
      free(ctx.runtime_http_header_len_patches);
      zbuf_free(&text);
      zbuf_free(&rodata);
      zbuf_free(&rela_text);
      zbuf_free(&strtab);
      zbuf_free(&symtab);
      zbuf_free(&shstrtab);
      return false;
    }
    function_sizes[i] = text.len - function_offsets[i];
    symbol_names[i] = (uint32_t)strtab.len;
    zbuf_append(&strtab, ir->functions[i].name);
    elf_append_u8(&strtab, 0);
  }
  const bool has_runtime_json_parse_bytes = ctx.runtime_json_parse_bytes_patch_len > 0;
  const bool has_runtime_http_fetch = ctx.runtime_http_fetch_patch_len > 0;
  const bool has_runtime_http_result_ok = ctx.runtime_http_result_ok_patch_len > 0;
  const bool has_runtime_http_result_status = ctx.runtime_http_result_status_patch_len > 0;
  const bool has_runtime_http_result_body_len = ctx.runtime_http_result_body_len_patch_len > 0;
  const bool has_runtime_http_result_error = ctx.runtime_http_result_error_patch_len > 0;
  const bool has_runtime_http_response_len = ctx.runtime_http_response_len_patch_len > 0;
  const bool has_runtime_http_response_headers_len = ctx.runtime_http_response_headers_len_patch_len > 0;
  const bool has_runtime_http_response_body_offset = ctx.runtime_http_response_body_offset_patch_len > 0;
  const bool has_runtime_http_header_value = ctx.runtime_http_header_value_patch_len > 0;
  const bool has_runtime_http_header_found = ctx.runtime_http_header_found_patch_len > 0;
  const bool has_runtime_http_header_offset = ctx.runtime_http_header_offset_patch_len > 0;
  const bool has_runtime_http_header_len = ctx.runtime_http_header_len_patch_len > 0;
  uint32_t runtime_json_parse_bytes_name = 0;
  uint32_t runtime_http_fetch_name = 0;
  uint32_t runtime_http_result_ok_name = 0;
  uint32_t runtime_http_result_status_name = 0;
  uint32_t runtime_http_result_body_len_name = 0;
  uint32_t runtime_http_result_error_name = 0;
  uint32_t runtime_http_response_len_name = 0;
  uint32_t runtime_http_response_headers_len_name = 0;
  uint32_t runtime_http_response_body_offset_name = 0;
  uint32_t runtime_http_header_value_name = 0;
  uint32_t runtime_http_header_found_name = 0;
  uint32_t runtime_http_header_offset_name = 0;
  uint32_t runtime_http_header_len_name = 0;
  if (has_runtime_json_parse_bytes) {
    runtime_json_parse_bytes_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_json_parse_bytes");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_fetch) {
    runtime_http_fetch_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_fetch_result");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_result_ok) {
    runtime_http_result_ok_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_result_ok");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_result_status) {
    runtime_http_result_status_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_result_status");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_result_body_len) {
    runtime_http_result_body_len_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_result_body_len");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_result_error) {
    runtime_http_result_error_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_result_error");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_response_len) {
    runtime_http_response_len_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_response_len");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_response_headers_len) {
    runtime_http_response_headers_len_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_response_headers_len");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_response_body_offset) {
    runtime_http_response_body_offset_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_response_body_offset");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_header_value) {
    runtime_http_header_value_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_header_value");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_header_found) {
    runtime_http_header_found_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_header_found");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_header_offset) {
    runtime_http_header_offset_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_header_offset");
    elf_append_u8(&strtab, 0);
  }
  if (has_runtime_http_header_len) {
    runtime_http_header_len_name = (uint32_t)strtab.len;
    zbuf_append(&strtab, "zero_http_header_len");
    elf_append_u8(&strtab, 0);
  }
  elf_patch_call_patches(&text, &ctx);
  for (size_t i = 0; i < ctx.rodata_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.rodata_patches[i].patch_offset, 1, 1, ctx.rodata_patches[i].data_offset - ctx.rodata_base_offset);
  }
  const uint32_t function_symbol_base = has_rodata ? 2u : 1u;
  uint32_t next_runtime_symbol = function_symbol_base + (uint32_t)ir->function_len;
  uint32_t runtime_json_parse_bytes_symbol = 0;
  uint32_t runtime_http_fetch_symbol = 0;
  uint32_t runtime_http_result_ok_symbol = 0;
  uint32_t runtime_http_result_status_symbol = 0;
  uint32_t runtime_http_result_body_len_symbol = 0;
  uint32_t runtime_http_result_error_symbol = 0;
  uint32_t runtime_http_response_len_symbol = 0;
  uint32_t runtime_http_response_headers_len_symbol = 0;
  uint32_t runtime_http_response_body_offset_symbol = 0;
  uint32_t runtime_http_header_value_symbol = 0;
  uint32_t runtime_http_header_found_symbol = 0;
  uint32_t runtime_http_header_offset_symbol = 0;
  uint32_t runtime_http_header_len_symbol = 0;
  if (has_runtime_json_parse_bytes) runtime_json_parse_bytes_symbol = next_runtime_symbol++;
  if (has_runtime_http_fetch) runtime_http_fetch_symbol = next_runtime_symbol++;
  if (has_runtime_http_result_ok) runtime_http_result_ok_symbol = next_runtime_symbol++;
  if (has_runtime_http_result_status) runtime_http_result_status_symbol = next_runtime_symbol++;
  if (has_runtime_http_result_body_len) runtime_http_result_body_len_symbol = next_runtime_symbol++;
  if (has_runtime_http_result_error) runtime_http_result_error_symbol = next_runtime_symbol++;
  if (has_runtime_http_response_len) runtime_http_response_len_symbol = next_runtime_symbol++;
  if (has_runtime_http_response_headers_len) runtime_http_response_headers_len_symbol = next_runtime_symbol++;
  if (has_runtime_http_response_body_offset) runtime_http_response_body_offset_symbol = next_runtime_symbol++;
  if (has_runtime_http_header_value) runtime_http_header_value_symbol = next_runtime_symbol++;
  if (has_runtime_http_header_found) runtime_http_header_found_symbol = next_runtime_symbol++;
  if (has_runtime_http_header_offset) runtime_http_header_offset_symbol = next_runtime_symbol++;
  if (has_runtime_http_header_len) runtime_http_header_len_symbol = next_runtime_symbol++;
  for (size_t i = 0; i < ctx.runtime_json_parse_bytes_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_json_parse_bytes_patches[i].patch_offset, runtime_json_parse_bytes_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_fetch_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_fetch_patches[i].patch_offset, runtime_http_fetch_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_result_ok_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_result_ok_patches[i].patch_offset, runtime_http_result_ok_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_result_status_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_result_status_patches[i].patch_offset, runtime_http_result_status_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_result_body_len_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_result_body_len_patches[i].patch_offset, runtime_http_result_body_len_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_result_error_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_result_error_patches[i].patch_offset, runtime_http_result_error_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_response_len_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_response_len_patches[i].patch_offset, runtime_http_response_len_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_response_headers_len_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_response_headers_len_patches[i].patch_offset, runtime_http_response_headers_len_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_response_body_offset_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_response_body_offset_patches[i].patch_offset, runtime_http_response_body_offset_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_header_value_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_header_value_patches[i].patch_offset, runtime_http_header_value_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_header_found_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_header_found_patches[i].patch_offset, runtime_http_header_found_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_header_offset_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_header_offset_patches[i].patch_offset, runtime_http_header_offset_symbol, 4, -4);
  }
  for (size_t i = 0; i < ctx.runtime_http_header_len_patch_len; i++) {
    elf_append_rela(&rela_text, ctx.runtime_http_header_len_patches[i].patch_offset, runtime_http_header_len_symbol, 4, -4);
  }

  for (size_t i = 0; i < ir->function_len; i++) {
    elf_append_symbol(&symtab, symbol_names[i], ir->functions[i].is_exported ? 0x12 : 0x02, 1, function_offsets[i], function_sizes[i]);
  }
  if (has_runtime_json_parse_bytes) {
    elf_append_symbol(&symtab, runtime_json_parse_bytes_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_fetch) {
    elf_append_symbol(&symtab, runtime_http_fetch_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_result_ok) {
    elf_append_symbol(&symtab, runtime_http_result_ok_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_result_status) {
    elf_append_symbol(&symtab, runtime_http_result_status_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_result_body_len) {
    elf_append_symbol(&symtab, runtime_http_result_body_len_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_result_error) {
    elf_append_symbol(&symtab, runtime_http_result_error_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_response_len) {
    elf_append_symbol(&symtab, runtime_http_response_len_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_response_headers_len) {
    elf_append_symbol(&symtab, runtime_http_response_headers_len_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_response_body_offset) {
    elf_append_symbol(&symtab, runtime_http_response_body_offset_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_header_value) {
    elf_append_symbol(&symtab, runtime_http_header_value_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_header_found) {
    elf_append_symbol(&symtab, runtime_http_header_found_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_header_offset) {
    elf_append_symbol(&symtab, runtime_http_header_offset_name, 0x12, 0, 0, 0);
  }
  if (has_runtime_http_header_len) {
    elf_append_symbol(&symtab, runtime_http_header_len_name, 0x12, 0, 0, 0);
  }

  elf_append_u8(&shstrtab, 0);
  uint32_t sh_name_text = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".text");
  elf_append_u8(&shstrtab, 0);
  uint32_t sh_name_rodata = 0;
  uint32_t sh_name_rela_text = 0;
  if (has_rodata) {
    sh_name_rodata = (uint32_t)shstrtab.len;
    zbuf_append(&shstrtab, ".rodata");
    elf_append_u8(&shstrtab, 0);
  }
  if (rela_text.len > 0) {
    sh_name_rela_text = (uint32_t)shstrtab.len;
    zbuf_append(&shstrtab, ".rela.text");
    elf_append_u8(&shstrtab, 0);
  }
  uint32_t sh_name_symtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".symtab");
  elf_append_u8(&shstrtab, 0);
  uint32_t sh_name_strtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".strtab");
  elf_append_u8(&shstrtab, 0);
  uint32_t sh_name_shstrtab = (uint32_t)shstrtab.len;
  zbuf_append(&shstrtab, ".shstrtab");
  elf_append_u8(&shstrtab, 0);

  const size_t ehdr_size = 64;
  const size_t shnum = 5 + (has_rodata ? 1 : 0) + (rela_text.len > 0 ? 1 : 0);
  const uint16_t symtab_shndx = (uint16_t)(2 + (has_rodata ? 1 : 0) + (rela_text.len > 0 ? 1 : 0));
  const uint16_t strtab_shndx = (uint16_t)(symtab_shndx + 1);
  const uint16_t shstrtab_shndx = (uint16_t)(strtab_shndx + 1);
  size_t text_offset = elf_align(ehdr_size, 16);
  size_t rodata_offset = has_rodata ? elf_align(text_offset + text.len, 8) : 0;
  size_t rela_text_offset = rela_text.len > 0 ? elf_align((has_rodata ? rodata_offset + rodata.len : text_offset + text.len), 8) : 0;
  size_t symtab_offset = elf_align((rela_text.len > 0 ? rela_text_offset + rela_text.len : (has_rodata ? rodata_offset + rodata.len : text_offset + text.len)), 8);
  size_t strtab_offset = symtab_offset + symtab.len;
  size_t shstrtab_offset = strtab_offset + strtab.len;
  size_t shoff = elf_align(shstrtab_offset + shstrtab.len, 8);

  zbuf_init(out);
  const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  elf_append_bytes(out, ident, sizeof(ident));
  elf_append_u16(out, 1);
  elf_append_u16(out, 62);
  elf_append_u32(out, 1);
  elf_append_u64(out, 0);
  elf_append_u64(out, 0);
  elf_append_u64(out, shoff);
  elf_append_u32(out, 0);
  elf_append_u16(out, 64);
  elf_append_u16(out, 0);
  elf_append_u16(out, 0);
  elf_append_u16(out, 64);
  elf_append_u16(out, (uint16_t)shnum);
  elf_append_u16(out, shstrtab_shndx);

  elf_pad_to(out, text_offset);
  elf_append_bytes(out, (const unsigned char *)text.data, text.len);
  if (has_rodata) {
    elf_pad_to(out, rodata_offset);
    elf_append_bytes(out, (const unsigned char *)rodata.data, rodata.len);
  }
  if (rela_text.len > 0) {
    elf_pad_to(out, rela_text_offset);
    elf_append_bytes(out, (const unsigned char *)rela_text.data, rela_text.len);
  }
  elf_pad_to(out, symtab_offset);
  elf_append_bytes(out, (const unsigned char *)symtab.data, symtab.len);
  elf_pad_to(out, strtab_offset);
  elf_append_bytes(out, (const unsigned char *)strtab.data, strtab.len);
  elf_pad_to(out, shstrtab_offset);
  elf_append_bytes(out, (const unsigned char *)shstrtab.data, shstrtab.len);
  elf_pad_to(out, shoff);

  elf_append_section_header(out, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  elf_append_section_header(out, sh_name_text, 1, 0x6, text_offset, text.len, 0, 0, 16, 0);
  if (has_rodata) elf_append_section_header(out, sh_name_rodata, 1, 0x2, rodata_offset, rodata.len, 0, 0, 8, 0);
  if (rela_text.len > 0) elf_append_section_header(out, sh_name_rela_text, 4, 0, rela_text_offset, rela_text.len, symtab_shndx, 1, 8, 24);
  elf_append_section_header(out, sh_name_symtab, 2, 0, symtab_offset, symtab.len, strtab_shndx, has_rodata ? 2 : 1, 8, 24);
  elf_append_section_header(out, sh_name_strtab, 3, 0, strtab_offset, strtab.len, 0, 0, 1, 0);
  elf_append_section_header(out, sh_name_shstrtab, 3, 0, shstrtab_offset, shstrtab.len, 0, 0, 1, 0);

  free(function_offsets);
  free(function_sizes);
  free(symbol_names);
  free(ctx.call_patches);
  free(ctx.rodata_patches);
  free(ctx.runtime_json_parse_bytes_patches);
  free(ctx.runtime_http_fetch_patches);
  free(ctx.runtime_http_result_ok_patches);
  free(ctx.runtime_http_result_status_patches);
  free(ctx.runtime_http_result_body_len_patches);
  free(ctx.runtime_http_result_error_patches);
  free(ctx.runtime_http_response_len_patches);
  free(ctx.runtime_http_response_headers_len_patches);
  free(ctx.runtime_http_response_body_offset_patches);
  free(ctx.runtime_http_header_value_patches);
  free(ctx.runtime_http_header_found_patches);
  free(ctx.runtime_http_header_offset_patches);
  free(ctx.runtime_http_header_len_patches);
  zbuf_free(&text);
  zbuf_free(&rodata);
  zbuf_free(&rela_text);
  zbuf_free(&strtab);
  zbuf_free(&symtab);
  zbuf_free(&shstrtab);
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
  elf_append_u8(text, 0x49);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xe7);
  size_t patch = elf_emit_jmp32_placeholder(text, 0xe8);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xc1);
  elf_append_u8(text, 0x48);
  elf_append_u8(text, 0xc1);
  elf_append_u8(text, 0xe9);
  elf_append_u8(text, 32);
  elf_append_u8(text, 0x85);
  elf_append_u8(text, 0xc9);
  size_t success_patch = elf_emit_jcc32_placeholder(text, 0x84);
  elf_append_u8(text, 0xbf);
  elf_append_u32(text, 1);
  size_t exit_patch = elf_emit_jmp32_placeholder(text, 0xe9);
  elf_patch_rel32(text, success_patch, text->len);
  elf_append_u8(text, 0x89);
  elf_append_u8(text, 0xc7);
  elf_patch_rel32(text, exit_patch, text->len);
  elf_append_u8(text, 0xb8);
  elf_append_u32(text, 60);
  elf_append_u8(text, 0x0f);
  elf_append_u8(text, 0x05);
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
  (void)entry_addr;

  ZBuf text;
  ZBuf rodata;
  zbuf_init(&text);
  zbuf_init(&rodata);
  bool has_rodata = ir->readonly_data_bytes > 0 || ir->data_segment_len > 0;
  unsigned rodata_base_offset = elf_rodata_base_offset(ir);
  if (has_rodata) elf_append_rodata(&rodata, ir, rodata_base_offset);
  size_t start_stub_len = 3 + 5 + 2 + 5 + 2;
  size_t first_function_offset = elf_align(start_stub_len, 16);
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
  elf_pad_to(&text, first_function_offset);
  for (size_t i = 0; i < ir->function_len; i++) {
    elf_pad_to(&text, elf_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!elf_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      free(function_offsets);
      free(ctx.call_patches);
      free(ctx.rodata_patches);
      free(ctx.runtime_json_parse_bytes_patches);
      free(ctx.runtime_http_fetch_patches);
      free(ctx.runtime_http_result_ok_patches);
      free(ctx.runtime_http_result_status_patches);
      free(ctx.runtime_http_result_body_len_patches);
      free(ctx.runtime_http_result_error_patches);
      free(ctx.runtime_http_response_len_patches);
      free(ctx.runtime_http_response_headers_len_patches);
      free(ctx.runtime_http_response_body_offset_patches);
      free(ctx.runtime_http_header_value_patches);
      free(ctx.runtime_http_header_found_patches);
      free(ctx.runtime_http_header_offset_patches);
      free(ctx.runtime_http_header_len_patches);
      zbuf_free(&text);
      zbuf_free(&rodata);
      return false;
    }
  }
  if (ctx.runtime_json_parse_bytes_patch_len > 0 ||
      ctx.runtime_http_fetch_patch_len > 0 ||
      ctx.runtime_http_result_ok_patch_len > 0 ||
      ctx.runtime_http_result_status_patch_len > 0 ||
      ctx.runtime_http_result_body_len_patch_len > 0 ||
      ctx.runtime_http_result_error_patch_len > 0 ||
      ctx.runtime_http_response_len_patch_len > 0 ||
      ctx.runtime_http_response_headers_len_patch_len > 0 ||
      ctx.runtime_http_response_body_offset_patch_len > 0 ||
      ctx.runtime_http_header_value_patch_len > 0 ||
      ctx.runtime_http_header_found_patch_len > 0 ||
      ctx.runtime_http_header_offset_patch_len > 0 ||
      ctx.runtime_http_header_len_patch_len > 0) {
    free(function_offsets);
    free(ctx.call_patches);
    free(ctx.rodata_patches);
    free(ctx.runtime_json_parse_bytes_patches);
    free(ctx.runtime_http_fetch_patches);
    free(ctx.runtime_http_result_ok_patches);
    free(ctx.runtime_http_result_status_patches);
    free(ctx.runtime_http_result_body_len_patches);
    free(ctx.runtime_http_result_error_patches);
    free(ctx.runtime_http_response_len_patches);
    free(ctx.runtime_http_response_headers_len_patches);
    free(ctx.runtime_http_response_body_offset_patches);
    free(ctx.runtime_http_header_value_patches);
    free(ctx.runtime_http_header_found_patches);
    free(ctx.runtime_http_header_offset_patches);
    free(ctx.runtime_http_header_len_patches);
    zbuf_free(&text);
    zbuf_free(&rodata);
    return elf_diag(diag, "direct ELF64 executable runtime helpers require object emission and an explicit runtime link step", 1, 1, "use --emit obj and link zero_runtime.c");
  }
  elf_patch_rel32(&text, start_call_patch, function_offsets[main_index]);
  elf_patch_call_patches(&text, &ctx);

  size_t rodata_offset = has_rodata ? elf_align(text_offset + text.len, 8) : 0;
  ctx.rodata_addr = has_rodata ? base_addr + rodata_offset : 0;
  elf_patch_rodata_patches(&text, &ctx);
  uint64_t file_size = has_rodata ? rodata_offset + rodata.len : text_offset + text.len;

  zbuf_init(out);
  const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  elf_append_bytes(out, ident, sizeof(ident));
  elf_append_u16(out, 2);
  elf_append_u16(out, 62);
  elf_append_u32(out, 1);
  elf_append_u64(out, entry_addr);
  elf_append_u64(out, ehdr_size);
  elf_append_u64(out, 0);
  elf_append_u32(out, 0);
  elf_append_u16(out, 64);
  elf_append_u16(out, 56);
  elf_append_u16(out, 1);
  elf_append_u16(out, 0);
  elf_append_u16(out, 0);
  elf_append_u16(out, 0);

  elf_append_u32(out, 1);
  elf_append_u32(out, 5);
  elf_append_u64(out, 0);
  elf_append_u64(out, base_addr);
  elf_append_u64(out, base_addr);
  elf_append_u64(out, file_size);
  elf_append_u64(out, file_size);
  elf_append_u64(out, 0x1000);

  elf_pad_to(out, text_offset);
  elf_append_bytes(out, (const unsigned char *)text.data, text.len);
  if (has_rodata) {
    elf_pad_to(out, rodata_offset);
    elf_append_bytes(out, (const unsigned char *)rodata.data, rodata.len);
  }
  free(function_offsets);
  free(ctx.call_patches);
  free(ctx.rodata_patches);
  free(ctx.runtime_json_parse_bytes_patches);
  free(ctx.runtime_http_fetch_patches);
  free(ctx.runtime_http_result_ok_patches);
  free(ctx.runtime_http_result_status_patches);
  free(ctx.runtime_http_result_body_len_patches);
  free(ctx.runtime_http_result_error_patches);
  free(ctx.runtime_http_response_len_patches);
  free(ctx.runtime_http_response_headers_len_patches);
  free(ctx.runtime_http_response_body_offset_patches);
  free(ctx.runtime_http_header_value_patches);
  free(ctx.runtime_http_header_found_patches);
  free(ctx.runtime_http_header_offset_patches);
  free(ctx.runtime_http_header_len_patches);
  zbuf_free(&text);
  zbuf_free(&rodata);
  return true;
}
