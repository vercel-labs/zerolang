#include "elf_emit_state.h"
#include "elf_format.h"
#include "x64_emit.h"

#include <stdio.h>
#include <stdlib.h>

static const char *const runtime_helper_symbols[ELF_RUNTIME_HELPER_COUNT] = {
  "zero_json_parse_bytes",
  "zero_json_diagnostic",
  "zero_json_field",
  "zero_json_lookup_scalar",
  "zero_json_string_decode",
  "zero_json_string_field",
  "zero_json_write_string",
  "zero_json_write_field_raw",
  "zero_json_write_field_string",
  "zero_json_write_field_u32",
  "zero_json_write_field_bool",
  "zero_json_write_object1_string",
  "zero_json_write_object1_u32",
  "zero_json_write_object1_bool",
  "zero_json_write_object2_fields",
  "zero_json_write_object2_string_field",
  "zero_json_write_object2_u32_field",
  "zero_json_write_object2_bool_field",
  "zero_json_write_array2_strings",
  "zero_json_write_array2_u32",
  "zero_json_write_array2_bools",
  "zero_http_fetch_result",
  "zero_http_result_ok",
  "zero_http_result_status",
  "zero_http_result_body_len",
  "zero_http_result_error",
  "zero_http_response_len",
  "zero_http_response_headers_len",
  "zero_http_response_body_offset",
  "zero_http_header_value",
  "zero_http_header_found",
  "zero_http_header_offset",
  "zero_http_header_len",
  "zero_http_write_json_response",
  "zero_http_request_method_name",
  "zero_http_request_path",
  "zero_http_request_matches",
  "zero_http_request_body_within",
  "zero_ascii_op",
  "zero_text_op",
  "zero_parse_op",
  "zero_parse_usize",
  "zero_time_op",
  "zero_math_op",
  "zero_math_usize_op",
  "zero_search_op",
  "zero_sort_op",
  "zero_sort_is_sorted_op",
  "zero_str_contains",
  "zero_str_buffer_op",
  "zero_str_concat",
  "zero_str_repeat",
  "zero_str_trim_op",
  "zero_str_pair_op",
  "zero_str_count_byte",
  "zero_str_word_count_ascii",
  "zero_crypto_digest",
  "zero_crypto_hmac_sha256",
  "zero_crypto_hmac_sha256_hex",
  "zero_args_find",
  "zero_parse_i32",
  "zero_parse_u32",
  "zero_fmt_bool",
  "zero_fmt_hex_lower_u32",
  "zero_fmt_i32",
  "zero_fmt_u32",
  "zero_fmt_usize",
  "zero_proc_capture",
  "zero_proc_capture_files",
};

static bool elf_emit_state_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct ELF64 object MVP subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to exported primitive integer arithmetic functions");
  }
  return false;
}

static bool elf_runtime_helper_valid(ElfRuntimeHelper helper) {
  return helper >= 0 && helper < ELF_RUNTIME_HELPER_COUNT;
}

const char *z_elf_runtime_helper_symbol(ElfRuntimeHelper helper) {
  if (!elf_runtime_helper_valid(helper)) return "";
  return runtime_helper_symbols[helper];
}

void z_elf_emit_context_free(ElfEmitContext *ctx) {
  if (!ctx) return;
  free(ctx->call_patches);
  free(ctx->rodata_patches);
  for (unsigned i = 0; i < ELF_RUNTIME_HELPER_COUNT; i++) {
    free(ctx->runtime_patches[i].items);
  }
  z_direct_trap_branches_free(ctx->trap_branches, Z_DIRECT_TRAP_KIND_COUNT);
}

bool z_elf_record_call_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned callee_index, ZDiag *diag, const IrValue *value) {
  if (!ctx || (((!value) || !value->external_call) && callee_index >= ctx->function_count)) {
    return elf_emit_state_diag(diag, "direct ELF64 call target index is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len + 1 > ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(ElfCallPatch));
  }
  ctx->call_patches[ctx->call_patch_len++] = (ElfCallPatch){
    .patch_offset = patch_offset,
    .callee_index = callee_index,
    .external_index = value ? value->external_index : 0,
    .external_call = value && value->external_call
  };
  return true;
}

bool z_elf_record_rodata_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned data_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx) return elf_emit_state_diag(diag, "direct ELF64 readonly data patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->rodata_patch_len + 1 > ctx->rodata_patch_cap) {
    ctx->rodata_patch_cap = z_grow_capacity(ctx->rodata_patch_cap, ctx->rodata_patch_len + 1, 8);
    ctx->rodata_patches = z_checked_reallocarray(ctx->rodata_patches, ctx->rodata_patch_cap, sizeof(ElfRodataPatch));
  }
  ctx->rodata_patches[ctx->rodata_patch_len++] = (ElfRodataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

bool z_elf_record_value_runtime_patch(ElfEmitContext *ctx, ElfRuntimeHelper helper, size_t patch_offset, ZDiag *diag, const IrValue *value) {
  if (!ctx || !elf_runtime_helper_valid(helper)) {
    return elf_emit_state_diag(diag, "direct ELF64 runtime patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  }
  ElfPatchList *list = &ctx->runtime_patches[helper];
  if (list->len + 1 > list->cap) {
    list->cap = z_grow_capacity(list->cap, list->len + 1, 4);
    list->items = z_checked_reallocarray(list->items, list->cap, sizeof(ElfPatch));
  }
  list->items[list->len++] = (ElfPatch){.patch_offset = patch_offset};
  return true;
}

size_t z_elf_runtime_patch_count(const ElfEmitContext *ctx, ElfRuntimeHelper helper) {
  if (!ctx || !elf_runtime_helper_valid(helper)) return 0;
  return ctx->runtime_patches[helper].len;
}

bool z_elf_has_runtime_patches(const ElfEmitContext *ctx) {
  if (!ctx) return false;
  for (unsigned i = 0; i < ELF_RUNTIME_HELPER_COUNT; i++) {
    if (ctx->runtime_patches[i].len > 0) return true;
  }
  return false;
}

void z_elf_patch_call_patches(ZBuf *code, const ElfEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const ElfCallPatch *patch = &ctx->call_patches[i];
    if (patch->external_call) continue;
    z_x64_patch_rel32(code, patch->patch_offset, ctx->function_offsets[patch->callee_index]);
  }
}

void z_elf_patch_rodata_patches(ZBuf *code, const ElfEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->rodata_patch_len; i++) {
    const ElfRodataPatch *patch = &ctx->rodata_patches[i];
    uint64_t addr = ctx->rodata_addr + (patch->data_offset - ctx->rodata_base_offset);
    for (unsigned b = 0; b < 8; b++) {
      code->data[patch->patch_offset + b] = (char)((addr >> (8 * b)) & 0xff);
    }
  }
}

void z_elf_append_rodata_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, uint32_t rodata_symbol) {
  for (size_t i = 0; ctx && i < ctx->rodata_patch_len; i++) {
    z_elf_append_rela(rela_text, ctx->rodata_patches[i].patch_offset, rodata_symbol, 1, ctx->rodata_patches[i].data_offset - ctx->rodata_base_offset);
  }
}

void z_elf_append_external_call_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, uint32_t external_symbol_base) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const ElfCallPatch *patch = &ctx->call_patches[i];
    if (!patch->external_call) continue;
    z_elf_append_rela(rela_text, patch->patch_offset, external_symbol_base + patch->external_index, 4, -4);
  }
}

void z_elf_append_runtime_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, ElfRuntimeHelper helper, uint32_t runtime_symbol) {
  if (!ctx || !elf_runtime_helper_valid(helper)) return;
  const ElfPatchList *patches = &ctx->runtime_patches[helper];
  for (size_t i = 0; i < patches->len; i++) {
    z_elf_append_rela(rela_text, patches->items[i].patch_offset, runtime_symbol, 4, -4);
  }
}
