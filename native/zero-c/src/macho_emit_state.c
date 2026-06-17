#include "macho_emit_state.h"
#include "macho_format.h"

#include <stdio.h>
#include <stdlib.h>

static const char *const runtime_helper_symbols[MACHO_RUNTIME_HELPER_COUNT] = {
  "_zero_world_write",
  "_zero_json_parse_bytes",
  "_zero_json_diagnostic",
  "_zero_json_field",
  "_zero_json_lookup_scalar",
  "_zero_json_string_decode",
  "_zero_json_string_field",
  "_zero_json_write_string",
  "_zero_json_write_field_raw",
  "_zero_json_write_field_string",
  "_zero_json_write_field_u32",
  "_zero_json_write_field_bool",
  "_zero_json_write_object1_string",
  "_zero_json_write_object1_u32",
  "_zero_json_write_object1_bool",
  "_zero_json_write_object2_fields",
  "_zero_json_write_object2_string_field",
  "_zero_json_write_object2_u32_field",
  "_zero_json_write_object2_bool_field",
  "_zero_json_write_array2_strings",
  "_zero_json_write_array2_u32",
  "_zero_json_write_array2_bools",
  "_zero_http_fetch_result",
  "_zero_http_result_ok",
  "_zero_http_result_status",
  "_zero_http_result_body_len",
  "_zero_http_result_error",
  "_zero_http_response_len",
  "_zero_http_response_headers_len",
  "_zero_http_response_body_offset",
  "_zero_http_header_value",
  "_zero_http_header_found",
  "_zero_http_header_offset",
  "_zero_http_header_len",
  "_zero_http_write_json_response",
  "_zero_http_request_method_name",
  "_zero_http_request_path",
  "_zero_http_request_matches",
  "_zero_http_request_body_within",
  "_zero_ascii_op",
  "_zero_text_op",
  "_zero_parse_op",
  "_zero_parse_usize",
  "_zero_time_op",
  "_zero_math_op",
  "_zero_math_usize_op",
  "_zero_search_op",
  "_zero_sort_op",
  "_zero_sort_is_sorted_op",
  "_zero_str_contains",
  "_zero_str_buffer_op",
  "_zero_str_concat",
  "_zero_str_repeat",
  "_zero_str_trim_op",
  "_zero_str_pair_op",
  "_zero_str_count_byte",
  "_zero_str_word_count_ascii",
  "_zero_crypto_digest",
  "_zero_crypto_hmac_sha256",
  "_zero_crypto_hmac_sha256_hex",
  "_zero_fs_read_bytes",
  "_zero_fs_read_bytes_at",
  "_zero_args_find",
  "_zero_parse_i32",
  "_zero_parse_u32",
  "_zero_fmt_bool",
  "_zero_fmt_hex_lower_u32",
  "_zero_fmt_i32",
  "_zero_fmt_u32",
  "_zero_fmt_usize",
  "_zero_proc_capture",
};

static bool macho_emit_state_diag_at(ZDiag *diag, const char *message, int line, int column, const char *actual) {
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

static bool macho_runtime_helper_valid(MachORuntimeHelper helper) {
  return helper >= 0 && helper < MACHO_RUNTIME_HELPER_COUNT;
}

const char *z_macho_runtime_helper_symbol(MachORuntimeHelper helper) {
  if (!macho_runtime_helper_valid(helper)) return "";
  return runtime_helper_symbols[helper];
}

void z_macho_emit_context_free(MachOEmitContext *ctx) {
  if (!ctx) return;
  for (unsigned i = 0; i < MACHO_RUNTIME_HELPER_COUNT; i++) {
    free(ctx->runtime_patches[i].items);
  }
  free(ctx->data_patches);
  free(ctx->call_patches);
  z_direct_trap_branches_free(ctx->trap_branches, Z_DIRECT_TRAP_KIND_COUNT);
}

bool z_macho_record_call_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  if (!ctx || (((!value) || !value->external_call) && callee_index >= ctx->function_count)) {
    return macho_emit_state_diag_at(diag, "direct AArch64 Mach-O call target is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len == ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(MachOCallPatch));
  }
  ctx->call_patches[ctx->call_patch_len++] = (MachOCallPatch){
    .patch_offset = patch_offset,
    .callee_index = callee_index,
    .external_index = value ? value->external_index : 0,
    .line = value ? value->line : 1,
    .column = value ? value->column : 1,
    .external_call = value && value->external_call
  };
  return true;
}

bool z_macho_record_data_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  if (!ctx) return macho_emit_state_diag_at(diag, "direct AArch64 Mach-O data relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->data_patch_len == ctx->data_patch_cap) {
    ctx->data_patch_cap = z_grow_capacity(ctx->data_patch_cap, ctx->data_patch_len + 1, 8);
    ctx->data_patches = z_checked_reallocarray(ctx->data_patches, ctx->data_patch_cap, sizeof(MachODataPatch));
  }
  ctx->data_patches[ctx->data_patch_len++] = (MachODataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static bool macho_record_runtime_patch_at(MachOEmitContext *ctx, MachORuntimeHelper helper, size_t patch_offset, int line, int column, ZDiag *diag) {
  if (!ctx || !macho_runtime_helper_valid(helper)) {
    return macho_emit_state_diag_at(diag, "direct AArch64 Mach-O runtime relocation requires an emit context", line, column, "missing context");
  }
  MachOPatchList *list = &ctx->runtime_patches[helper];
  if (list->len == list->cap) {
    list->cap = z_grow_capacity(list->cap, list->len + 1, 4);
    list->items = z_checked_reallocarray(list->items, list->cap, sizeof(MachOPatch));
  }
  list->items[list->len++] = (MachOPatch){.patch_offset = patch_offset};
  return true;
}

bool z_macho_record_value_runtime_patch(MachOEmitContext *ctx, MachORuntimeHelper helper, size_t patch_offset, const IrValue *value, ZDiag *diag) {
  return macho_record_runtime_patch_at(ctx, helper, patch_offset, value ? value->line : 1, value ? value->column : 1, diag);
}

bool z_macho_record_instr_runtime_patch(MachOEmitContext *ctx, MachORuntimeHelper helper, size_t patch_offset, const IrInstr *instr, ZDiag *diag) {
  return macho_record_runtime_patch_at(ctx, helper, patch_offset, instr ? instr->line : 1, instr ? instr->column : 1, diag);
}

size_t z_macho_runtime_patch_count(const MachOEmitContext *ctx, MachORuntimeHelper helper) {
  if (!ctx || !macho_runtime_helper_valid(helper)) return 0;
  return ctx->runtime_patches[helper].len;
}

const MachOPatchList *z_macho_runtime_patch_list(const MachOEmitContext *ctx, MachORuntimeHelper helper) {
  if (!ctx || !macho_runtime_helper_valid(helper)) return NULL;
  return &ctx->runtime_patches[helper];
}

bool z_macho_has_unsupported_exe_runtime_patches(const MachOEmitContext *ctx) {
  if (!ctx) return false;
  for (unsigned i = 0; i < MACHO_RUNTIME_HELPER_COUNT; i++) {
    if (i == MACHO_RUNTIME_WORLD_WRITE) continue;
    if (ctx->runtime_patches[i].len > 0) return true;
  }
  return false;
}

static void macho_append_branch_relocations(ZBuf *relocs, const MachOPatchList *patches, unsigned symbol_index) {
  for (size_t i = 0; patches && i < patches->len; i++) {
    uint32_t reloc_info = (symbol_index & 0x00ffffffu) |
                          (1u << 24) |
                          (2u << 25) |
                          (1u << 27) |
                          (2u << 28);
    z_macho_append_u32(relocs, (uint32_t)patches->items[i].patch_offset);
    z_macho_append_u32(relocs, reloc_info);
  }
}

void z_macho_append_call_relocations(ZBuf *relocs, const MachOEmitContext *ctx, uint32_t external_symbol_base) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const MachOCallPatch *patch = &ctx->call_patches[i];
    uint32_t symbol_index = patch->external_call ? external_symbol_base + patch->external_index : patch->callee_index;
    uint32_t reloc_info = (symbol_index & 0x00ffffffu) |
                          (1u << 24) |
                          (2u << 25) |
                          (1u << 27) |
                          (2u << 28);
    z_macho_append_u32(relocs, (uint32_t)patch->patch_offset);
    z_macho_append_u32(relocs, reloc_info);
  }
}

void z_macho_append_runtime_relocations(ZBuf *relocs, const MachOEmitContext *ctx, MachORuntimeHelper helper, unsigned symbol_index) {
  macho_append_branch_relocations(relocs, z_macho_runtime_patch_list(ctx, helper), symbol_index);
}

size_t z_macho_data_relocation_count(const MachOEmitContext *ctx) {
  if (!ctx) return 0;
  if (!ctx->pie_relative_data) return ctx->data_patch_len;
  size_t count = ctx->data_patch_len * 2;
  for (size_t i = 0; i < ctx->data_patch_len; i++) {
    const MachODataPatch *patch = &ctx->data_patches[i];
    if (patch->data_offset != ctx->rodata_base_offset) count += 2;
  }
  return count;
}

size_t z_macho_text_relocation_count(const MachOEmitContext *ctx) {
  if (!ctx) return 0;
  size_t count = ctx->call_patch_len + z_macho_data_relocation_count(ctx);
  for (unsigned i = 0; i < MACHO_RUNTIME_HELPER_COUNT; i++) {
    count += ctx->runtime_patches[i].len;
  }
  return count;
}

static void macho_append_reloc(ZBuf *relocs, uint32_t address, uint32_t symbol_or_addend, bool pcrel, unsigned length, bool external, unsigned type) {
  uint32_t reloc_info = (symbol_or_addend & 0x00ffffffu) |
                        ((pcrel ? 1u : 0u) << 24) |
                        ((length & 3u) << 25) |
                        ((external ? 1u : 0u) << 27) |
                        ((type & 15u) << 28);
  z_macho_append_u32(relocs, address);
  z_macho_append_u32(relocs, reloc_info);
}

void z_macho_append_data_relocations(ZBuf *relocs, const MachOEmitContext *ctx, unsigned data_symbol_index) {
  for (size_t i = 0; ctx && i < ctx->data_patch_len; i++) {
    const MachODataPatch *patch = &ctx->data_patches[i];
    if (ctx->pie_relative_data) {
      uint32_t addend = patch->data_offset - ctx->rodata_base_offset;
      if (addend != 0) macho_append_reloc(relocs, (uint32_t)patch->patch_offset + 4u, addend, false, 2, false, 10);
      macho_append_reloc(relocs, (uint32_t)patch->patch_offset + 4u, data_symbol_index, false, 2, true, 4);
      if (addend != 0) macho_append_reloc(relocs, (uint32_t)patch->patch_offset, addend, false, 2, false, 10);
      macho_append_reloc(relocs, (uint32_t)patch->patch_offset, data_symbol_index, true, 2, true, 3);
    } else {
      macho_append_reloc(relocs, (uint32_t)patch->patch_offset, data_symbol_index, false, 3, true, 0);
    }
  }
}
