#include "zero.h"
#include "aarch64_direct.h"
#include "aarch64_emit.h"
#include "coff_format.h"
#include "direct_emit.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
  ZCoffImageDataPatch *items;
  size_t len;
  size_t cap;
} CoffA64DataPatches;

typedef struct {
  size_t *items;
  size_t len;
  size_t cap;
} CoffA64BranchPatches;

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
  unsigned external_index;
  bool external_call;
} CoffA64CallPatch;

typedef struct {
  CoffA64CallPatch *items;
  size_t len;
  size_t cap;
} CoffA64CallPatches;

typedef struct {
  size_t patch_offset;
} CoffA64RuntimePatch;

typedef struct {
  CoffA64RuntimePatch *items;
  size_t len;
  size_t cap;
} CoffA64RuntimePatches;

typedef struct {
  CoffA64DataPatches data;
  CoffA64CallPatches calls;
  CoffA64RuntimePatches runtime[A64_DIRECT_RUNTIME_HELPER_COUNT];
  CoffA64BranchPatches world_write;
  bool allow_runtime_patches;
} CoffA64EmitState;

static bool coff_a64_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct COFF AArch64 object subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to AArch64 COFF supported direct-backend constructs");
  }
  return false;
}

static bool coff_a64_record_data_patch(void *user, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  CoffA64EmitState *state = user;
  CoffA64DataPatches *patches = state ? &state->data : NULL;
  if (!patches) return coff_a64_diag(diag, "direct COFF AArch64 readonly data patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (patches->len == patches->cap) {
    patches->cap = z_grow_capacity(patches->cap, patches->len + 1, 8);
    patches->items = z_checked_reallocarray(patches->items, patches->cap, sizeof(ZCoffImageDataPatch));
  }
  if (!patches->items) return coff_a64_diag(diag, "direct COFF AArch64 backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  patches->items[patches->len++] = (ZCoffImageDataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static void coff_a64_data_patches_free(CoffA64DataPatches *patches) {
  if (!patches) return;
  free(patches->items);
}

static bool coff_a64_record_branch_patch(CoffA64BranchPatches *patches, size_t patch_offset, const IrInstr *instr, ZDiag *diag) {
  if (!patches) return coff_a64_diag(diag, "direct COFF AArch64 branch patch requires an emit context", instr ? instr->line : 1, instr ? instr->column : 1, "missing context");
  if (patches->len == patches->cap) {
    patches->cap = z_grow_capacity(patches->cap, patches->len + 1, 8);
    patches->items = z_checked_reallocarray(patches->items, patches->cap, sizeof(size_t));
  }
  if (!patches->items) return coff_a64_diag(diag, "direct COFF AArch64 backend ran out of memory", instr ? instr->line : 1, instr ? instr->column : 1, "allocation failed");
  patches->items[patches->len++] = patch_offset;
  return true;
}

static bool coff_a64_record_call_patch(void *user, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  CoffA64EmitState *state = user;
  if (!state) return coff_a64_diag(diag, "direct COFF AArch64 call patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (state->calls.len == state->calls.cap) {
    state->calls.cap = z_grow_capacity(state->calls.cap, state->calls.len + 1, 8);
    state->calls.items = z_checked_reallocarray(state->calls.items, state->calls.cap, sizeof(CoffA64CallPatch));
  }
  if (!state->calls.items) return coff_a64_diag(diag, "direct COFF AArch64 backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  state->calls.items[state->calls.len++] = (CoffA64CallPatch){
    .patch_offset = patch_offset,
    .callee_index = callee_index,
    .external_index = value ? value->external_index : 0,
    .external_call = value && value->external_call
  };
  return true;
}

static bool coff_a64_runtime_helper_valid(ZAArch64DirectRuntimeHelper helper) {
  return helper >= 0 && helper < A64_DIRECT_RUNTIME_HELPER_COUNT;
}

static const char *coff_a64_runtime_helper_symbol(ZAArch64DirectRuntimeHelper helper) {
  switch (helper) {
    case A64_DIRECT_RUNTIME_JSON_PARSE_BYTES: return "zero_json_parse_bytes";
    case A64_DIRECT_RUNTIME_JSON_DIAGNOSTIC: return "zero_json_diagnostic";
    case A64_DIRECT_RUNTIME_JSON_FIELD: return "zero_json_field";
    case A64_DIRECT_RUNTIME_JSON_LOOKUP_SCALAR: return "zero_json_lookup_scalar";
    case A64_DIRECT_RUNTIME_JSON_STRING_DECODE: return "zero_json_string_decode";
    case A64_DIRECT_RUNTIME_JSON_STRING_FIELD: return "zero_json_string_field";
    case A64_DIRECT_RUNTIME_JSON_WRITE_STRING: return "zero_json_write_string";
    case A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_RAW: return "zero_json_write_field_raw";
    case A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_STRING: return "zero_json_write_field_string";
    case A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_U32: return "zero_json_write_field_u32";
    case A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_BOOL: return "zero_json_write_field_bool";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_STRING: return "zero_json_write_object1_string";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_U32: return "zero_json_write_object1_u32";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_BOOL: return "zero_json_write_object1_bool";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_FIELDS: return "zero_json_write_object2_fields";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD: return "zero_json_write_object2_string_field";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD: return "zero_json_write_object2_u32_field";
    case A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD: return "zero_json_write_object2_bool_field";
    case A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_STRINGS: return "zero_json_write_array2_strings";
    case A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_U32: return "zero_json_write_array2_u32";
    case A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_BOOLS: return "zero_json_write_array2_bools";
    case A64_DIRECT_RUNTIME_STR_BUFFER_OP: return "zero_str_buffer_op";
    case A64_DIRECT_RUNTIME_STR_CONCAT: return "zero_str_concat";
    case A64_DIRECT_RUNTIME_STR_REPEAT: return "zero_str_repeat";
    case A64_DIRECT_RUNTIME_STR_TRIM_OP: return "zero_str_trim_op";
    case A64_DIRECT_RUNTIME_STR_PAIR_OP: return "zero_str_pair_op";
    case A64_DIRECT_RUNTIME_STR_COUNT_BYTE: return "zero_str_count_byte";
    case A64_DIRECT_RUNTIME_STR_WORD_COUNT_ASCII: return "zero_str_word_count_ascii";
    case A64_DIRECT_RUNTIME_CRYPTO_DIGEST: return "zero_crypto_digest";
    case A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256: return "zero_crypto_hmac_sha256";
    case A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256_HEX: return "zero_crypto_hmac_sha256_hex";
    case A64_DIRECT_RUNTIME_ASCII_OP: return "zero_ascii_op";
    case A64_DIRECT_RUNTIME_TEXT_OP: return "zero_text_op";
    case A64_DIRECT_RUNTIME_PARSE_OP: return "zero_parse_op";
    case A64_DIRECT_RUNTIME_PARSE_USIZE: return "zero_parse_usize";
    case A64_DIRECT_RUNTIME_PARSE_I32: return "zero_parse_i32";
    case A64_DIRECT_RUNTIME_PARSE_U32: return "zero_parse_u32";
    case A64_DIRECT_RUNTIME_FMT_BOOL: return "zero_fmt_bool";
    case A64_DIRECT_RUNTIME_FMT_HEX_U32: return "zero_fmt_hex_lower_u32";
    case A64_DIRECT_RUNTIME_FMT_I32: return "zero_fmt_i32";
    case A64_DIRECT_RUNTIME_FMT_U32: return "zero_fmt_u32";
    case A64_DIRECT_RUNTIME_FMT_USIZE: return "zero_fmt_usize";
    case A64_DIRECT_RUNTIME_TIME_OP: return "zero_time_op";
    case A64_DIRECT_RUNTIME_MATH_OP: return "zero_math_op";
    case A64_DIRECT_RUNTIME_MATH_USIZE_OP: return "zero_math_usize_op";
    case A64_DIRECT_RUNTIME_SEARCH_OP: return "zero_search_op";
    case A64_DIRECT_RUNTIME_SORT_OP: return "zero_sort_op";
    case A64_DIRECT_RUNTIME_SORT_IS_SORTED_OP: return "zero_sort_is_sorted_op";
    case A64_DIRECT_RUNTIME_HTTP_REQUEST_METHOD_NAME: return "zero_http_request_method_name";
    case A64_DIRECT_RUNTIME_HTTP_REQUEST_PATH: return "zero_http_request_path";
    case A64_DIRECT_RUNTIME_HELPER_COUNT: break;
  }
  return "zero_runtime_helper";
}

static bool coff_a64_record_runtime_patch(void *user, size_t patch_offset, ZAArch64DirectRuntimeHelper helper, const IrValue *value, ZDiag *diag) {
  CoffA64EmitState *state = user;
  if (!state || !coff_a64_runtime_helper_valid(helper)) return coff_a64_diag(diag, "direct COFF AArch64 runtime patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (!state->allow_runtime_patches) return coff_a64_diag(diag, "direct COFF AArch64 executable runtime helpers require object emission and an explicit runtime link step", value ? value->line : 1, value ? value->column : 1, "use --emit obj and link zero_runtime.c");
  CoffA64RuntimePatches *patches = &state->runtime[helper];
  if (patches->len == patches->cap) {
    patches->cap = z_grow_capacity(patches->cap, patches->len + 1, 4);
    patches->items = z_checked_reallocarray(patches->items, patches->cap, sizeof(CoffA64RuntimePatch));
  }
  if (!patches->items) return coff_a64_diag(diag, "direct COFF AArch64 backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  patches->items[patches->len++] = (CoffA64RuntimePatch){.patch_offset = patch_offset};
  return true;
}

static void coff_a64_emit_state_free(CoffA64EmitState *state) {
  if (!state) return;
  coff_a64_data_patches_free(&state->data);
  free(state->calls.items);
  for (unsigned i = 0; i < A64_DIRECT_RUNTIME_HELPER_COUNT; i++) free(state->runtime[i].items);
  free(state->world_write.items);
}

static void coff_a64_patch_call_branches(ZBuf *text, const CoffA64CallPatches *patches, const size_t *offsets, size_t function_count) {
  for (size_t i = 0; patches && i < patches->len; i++) {
    const CoffA64CallPatch *patch = &patches->items[i];
    if (patch->external_call) continue;
    if (patch->callee_index < function_count) z_aarch64_patch_branch26(text, patch->patch_offset, offsets[patch->callee_index]);
  }
}

static void coff_a64_append_external_call_relocations(ZBuf *relocs, const CoffA64CallPatches *patches, uint32_t external_symbol_base) {
  for (size_t i = 0; patches && i < patches->len; i++) {
    const CoffA64CallPatch *patch = &patches->items[i];
    if (!patch->external_call) continue;
    z_coff_append_reloc(relocs, (uint32_t)patch->patch_offset, external_symbol_base + patch->external_index, Z_COFF_RELOC_ARM64_BRANCH26);
  }
}

static size_t coff_a64_external_call_relocation_count(const CoffA64CallPatches *patches) {
  size_t count = 0;
  for (size_t i = 0; patches && i < patches->len; i++) {
    if (patches->items[i].external_call) count++;
  }
  return count;
}

static void coff_a64_append_runtime_relocations(ZBuf *relocs, const CoffA64EmitState *state, uint32_t runtime_symbol_base) {
  for (unsigned helper = 0; state && helper < A64_DIRECT_RUNTIME_HELPER_COUNT; helper++) {
    const CoffA64RuntimePatches *patches = &state->runtime[helper];
    for (size_t i = 0; i < patches->len; i++) {
      z_coff_append_reloc(relocs, (uint32_t)patches->items[i].patch_offset, runtime_symbol_base + helper, Z_COFF_RELOC_ARM64_BRANCH26);
    }
  }
}

static size_t coff_a64_runtime_relocation_count(const CoffA64EmitState *state) {
  size_t count = 0;
  for (unsigned helper = 0; state && helper < A64_DIRECT_RUNTIME_HELPER_COUNT; helper++) count += state->runtime[helper].len;
  return count;
}

static void coff_a64_append_object_rodata_relocations(ZBuf *text, ZBuf *relocs, const CoffA64DataPatches *patches, unsigned rodata_base_offset) {
  for (size_t i = 0; patches && i < patches->len; i++) {
    const ZCoffImageDataPatch *patch = &patches->items[i];
    z_coff_patch_u64(text, patch->patch_offset, patch->data_offset - rodata_base_offset);
    z_coff_append_reloc(relocs, (uint32_t)patch->patch_offset, 1u, Z_COFF_RELOC_ARM64_ADDR64);
  }
}

static size_t coff_a64_emit_import_call(ZBuf *text, ZCoffImportPatch *patches, size_t *patch_len, unsigned import_index) {
  while (((text->len + 8) % 8) != 0) z_aarch64_emit_nop(text);
  z_aarch64_emit_ldr_x_literal8(text, 16);
  z_aarch64_emit_b_offset_words(text, 3);
  size_t patch = text->len;
  z_aarch64_append_u64(text, 0);
  z_aarch64_emit_load_x_imm(text, 16, 16, 0);
  z_aarch64_emit_blr_x(text, 16);
  if (patches && patch_len && *patch_len < 8) {
    patches[*patch_len] = (ZCoffImportPatch){.patch_offset = patch, .import_index = import_index};
    *patch_len += 1;
  }
  return patch;
}

static size_t coff_a64_emit_exe_start_stub(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  z_aarch64_emit_sub_sp_imm(text, 16);
  size_t main_patch = z_aarch64_emit_bl_placeholder(text);
  coff_a64_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_EXIT_PROCESS);
  z_aarch64_emit_brk(text);
  return main_patch;
}

static bool coff_a64_emit_world_write_call(ZBuf *text, const IrInstr *instr, ZAArch64DirectContext *ctx, ZDiag *diag) {
  CoffA64EmitState *state = ctx ? ctx->patch_user : NULL;
  size_t patch = z_aarch64_emit_bl_placeholder(text);
  if (!coff_a64_record_branch_patch(state ? &state->world_write : NULL, patch, instr, diag)) return false;
  size_t ok_patch = z_aarch64_emit_cbz_w_placeholder(text, 0);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  return true;
}

static size_t coff_a64_emit_exe_world_write(ZBuf *text, ZCoffImportPatch *import_patches, size_t *import_patch_len) {
  size_t offset = text->len;
  z_aarch64_emit_sub_sp_imm(text, 48);
  z_aarch64_emit_store_x_sp(text, 1, 0);
  z_aarch64_emit_store_x_sp(text, 2, 8);
  z_aarch64_emit_store_w_sp(text, 31, 16);
  z_aarch64_emit_store_x_sp(text, 30, 40);
  z_aarch64_emit_movz_w(text, 8, 2);
  z_aarch64_emit_cmp_w(text, 0, 8);
  z_aarch64_emit_movz_w(text, 0, 0xfffffff5u);
  size_t stdout_patch = z_aarch64_emit_b_cond_placeholder(text, 1);
  z_aarch64_emit_movz_w(text, 0, 0xfffffff4u);
  z_aarch64_patch_cond19(text, stdout_patch, text->len);
  coff_a64_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_GET_STD_HANDLE);
  z_aarch64_emit_load_x_sp(text, 1, 0);
  z_aarch64_emit_load_w_sp(text, 2, 8);
  z_aarch64_emit_add_x_sp_imm(text, 3, 16);
  z_aarch64_emit_movz_x(text, 4, 0);
  coff_a64_emit_import_call(text, import_patches, import_patch_len, Z_COFF_IMPORT_WRITE_FILE);
  z_aarch64_emit_cmp_w(text, 0, 31);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 1);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  z_aarch64_emit_movz_w(text, 0, 0);
  z_aarch64_emit_load_x_sp(text, 30, 40);
  z_aarch64_emit_add_sp_imm(text, 48);
  z_aarch64_emit_ret(text);
  return offset;
}

bool z_emit_coff_aarch64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return coff_a64_diag(diag, "direct COFF AArch64 backend received no program", 1, 1, "missing MIR");
  if (!program->mir_valid) {
    bool ok = coff_a64_diag(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (program->function_len == 0) return coff_a64_diag(diag, "direct COFF AArch64 object backend requires at least one exported function", 1, 1, "empty program");

  ZBuf text;
  ZBuf rodata;
  ZBuf relocs;
  zbuf_init(&text);
  zbuf_init(&rodata);
  zbuf_init(&relocs);

  bool has_rodata = program->readonly_data_bytes > 0 || program->data_segment_len > 0;
  unsigned rodata_base_offset = z_aarch64_direct_rodata_base_offset(program);
  if (has_rodata) z_aarch64_direct_append_rodata(&rodata, program, rodata_base_offset);

  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  ZCoffSymbol *symbols = z_checked_calloc(program->function_len + program->external_function_len + A64_DIRECT_RUNTIME_HELPER_COUNT, sizeof(ZCoffSymbol));
  if (!offsets || !symbols) {
    free(offsets);
    free(symbols);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return coff_a64_diag(diag, "out of memory while emitting COFF AArch64 object", 1, 1, "allocation failed");
  }

  CoffA64EmitState state = {.allow_runtime_patches = true};
  ZAArch64DirectContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .patch_user = &state,
    .record_data_patch = coff_a64_record_data_patch,
    .record_call_patch = coff_a64_record_call_patch,
    .record_runtime_patch = coff_a64_record_runtime_patch
  };

  bool has_export = false;
  size_t symbol_len = 0;
  for (size_t i = 0; i < program->function_len; i++) {
    if (program->functions[i].is_exported) has_export = true;
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
    offsets[i] = text.len;
    if (!z_aarch64_direct_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      coff_a64_emit_state_free(&state);
      free(offsets);
      free(symbols);
      zbuf_free(&relocs);
      zbuf_free(&rodata);
      zbuf_free(&text);
      return false;
    }
    if (program->functions[i].is_exported) {
      symbols[symbol_len++] = (ZCoffSymbol){
        .name = program->functions[i].name ? program->functions[i].name : "zero_fn",
        .value = (uint32_t)offsets[i],
        .section_number = 1,
        .type = 0x20,
        .storage_class = Z_COFF_SYMBOL_EXTERNAL
      };
    }
  }
  if (!has_export) {
    coff_a64_emit_state_free(&state);
    free(offsets);
    free(symbols);
    zbuf_free(&relocs);
    zbuf_free(&rodata);
    zbuf_free(&text);
    return coff_a64_diag(diag, "direct COFF AArch64 object backend requires at least one exported function", 1, 1, "no exported function");
  }
  coff_a64_patch_call_branches(&text, &state.calls, offsets, program->function_len);
  if (has_rodata) coff_a64_append_object_rodata_relocations(&text, &relocs, &state.data, rodata_base_offset);
  uint32_t runtime_symbol_base = (has_rodata ? 2u : 1u) + (uint32_t)symbol_len;
  for (unsigned helper = 0; helper < A64_DIRECT_RUNTIME_HELPER_COUNT; helper++) {
    symbols[symbol_len++] = (ZCoffSymbol){
      .name = coff_a64_runtime_helper_symbol((ZAArch64DirectRuntimeHelper)helper),
      .section_number = 0,
      .type = 0x20,
      .storage_class = Z_COFF_SYMBOL_EXTERNAL
    };
  }
  coff_a64_append_runtime_relocations(&relocs, &state, runtime_symbol_base);
  uint32_t external_symbol_base = runtime_symbol_base + A64_DIRECT_RUNTIME_HELPER_COUNT;
  for (size_t i = 0; i < program->external_function_len; i++) {
    symbols[symbol_len++] = (ZCoffSymbol){
      .name = program->external_functions[i].symbol ? program->external_functions[i].symbol : "zero_external",
      .section_number = 0,
      .type = 0x20,
      .storage_class = Z_COFF_SYMBOL_EXTERNAL
    };
  }
  coff_a64_append_external_call_relocations(&relocs, &state.calls, external_symbol_base);

  ZCoffObjectImage image = {
    .machine = Z_COFF_MACHINE_ARM64,
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .text_relocs = &relocs,
    .text_reloc_count = (uint16_t)(state.data.len + coff_a64_runtime_relocation_count(&state) + coff_a64_external_call_relocation_count(&state.calls)),
    .symbols = symbols,
    .symbol_len = symbol_len
  };
  z_coff_write_object(out, &image);

  coff_a64_emit_state_free(&state);
  free(offsets);
  free(symbols);
  zbuf_free(&relocs);
  zbuf_free(&rodata);
  zbuf_free(&text);
  return true;
}

bool z_emit_coff_aarch64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) return coff_a64_diag(diag, "direct COFF AArch64 executable backend received no program", 1, 1, "missing MIR");
  if (!program->mir_valid) {
    bool ok = coff_a64_diag(diag, program->mir_message[0] ? program->mir_message : "direct backend lowering failed", program->mir_line, program->mir_column, program->mir_actual);
    z_diag_set_backend_blocker(diag, &program->backend_blocker);
    return ok;
  }
  if (!z_direct_exe_reject_c_import_calls(program, diag, "COFF AArch64")) return false;

  unsigned main_index = 0;
  if (!z_aarch64_direct_find_main(program, &main_index, diag)) return false;

  ZBuf text;
  ZBuf rdata;
  zbuf_init(&text);
  zbuf_init(&rdata);

  unsigned rodata_base_offset = z_aarch64_direct_rodata_base_offset(program);
  ZDirectTrapMessages trap_messages = {0};
  z_aarch64_direct_append_rodata(&rdata, program, rodata_base_offset);
  z_aarch64_direct_append_trap_messages(&rdata, rodata_base_offset, &trap_messages);

  size_t *offsets = z_checked_calloc(program->function_len, sizeof(size_t));
  if (!offsets) {
    zbuf_free(&rdata);
    zbuf_free(&text);
    return coff_a64_diag(diag, "out of memory while emitting COFF AArch64 executable", 1, 1, "allocation failed");
  }

  CoffA64EmitState state = {0};
  ZAArch64DirectContext ctx = {
    .program = program,
    .function_offsets = offsets,
    .function_count = program->function_len,
    .rodata_base_offset = rodata_base_offset,
    .patch_user = &state,
    .record_data_patch = coff_a64_record_data_patch,
    .record_call_patch = coff_a64_record_call_patch,
    .record_runtime_patch = coff_a64_record_runtime_patch,
    .emit_world_write = coff_a64_emit_world_write_call,
    .trap_messages = trap_messages
  };

  ZCoffImportPatch import_patches[8];
  size_t import_patch_len = 0;
  size_t start_main_patch = coff_a64_emit_exe_start_stub(&text, import_patches, &import_patch_len);
  z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
  for (size_t i = 0; i < program->function_len; i++) {
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
    offsets[i] = text.len;
    if (!z_aarch64_direct_emit_function_text(&text, &program->functions[i], &ctx, diag)) {
      z_direct_trap_branches_free(ctx.trap_branches, Z_DIRECT_TRAP_KIND_COUNT);
      coff_a64_emit_state_free(&state);
      free(offsets);
      zbuf_free(&rdata);
      zbuf_free(&text);
      return false;
    }
  }
  if (!z_aarch64_direct_emit_trap_stubs(&text, &ctx, diag)) {
    coff_a64_emit_state_free(&state);
    free(offsets);
    zbuf_free(&rdata);
    zbuf_free(&text);
    return false;
  }
  z_aarch64_patch_branch26(&text, start_main_patch, offsets[main_index]);
  coff_a64_patch_call_branches(&text, &state.calls, offsets, program->function_len);
  size_t world_write_offset = 0;
  if (state.world_write.len > 0) {
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
    world_write_offset = coff_a64_emit_exe_world_write(&text, import_patches, &import_patch_len);
    for (size_t i = 0; i < state.world_write.len; i++) {
      z_aarch64_patch_branch26(&text, state.world_write.items[i], world_write_offset);
    }
  }

  ZCoffExecutableImage image = {
    .machine = Z_COFF_MACHINE_ARM64,
    .image_base = 0x140000000ull,
    .section_alignment = 0x1000,
    .file_alignment = 0x200,
    .text = &text,
    .rdata = &rdata,
    .rodata_base_offset = rodata_base_offset,
    .rodata_patches = state.data.items,
    .rodata_patch_len = state.data.len,
    .import_patches = import_patches,
    .import_patch_len = import_patch_len
  };
  z_coff_write_pe64_executable(out, &image);

  coff_a64_emit_state_free(&state);
  free(offsets);
  zbuf_free(&rdata);
  zbuf_free(&text);
  return true;
}
