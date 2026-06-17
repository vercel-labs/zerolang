#include "zero.h"
#include "aarch64_direct.h"
#include "aarch64_emit.h"
#include "direct_emit.h"
#include "elf_format.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  R_AARCH64_ABS64 = 257u,
  R_AARCH64_CALL26 = 283u
};

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} A64ElfDataPatch;

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
  unsigned external_index;
  bool external_call;
} A64ElfCallPatch;

typedef struct {
  size_t patch_offset;
} A64ElfRuntimePatch;

typedef struct {
  A64ElfRuntimePatch *items;
  size_t len;
  size_t cap;
} A64ElfRuntimePatchList;

typedef struct {
  A64ElfDataPatch *data_patches;
  size_t data_patch_len;
  size_t data_patch_cap;
  A64ElfCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  A64ElfRuntimePatchList runtime_patches[A64_DIRECT_RUNTIME_HELPER_COUNT];
  bool allow_runtime_patches;
} A64ElfPatchContext;

static bool a64_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
  if (diag) {
    diag->code = 4004;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", message);
    snprintf(diag->expected, sizeof(diag->expected), "direct AArch64 ELF backend subset");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported construct");
    snprintf(diag->help, sizeof(diag->help), "choose a supported direct target or restrict this program to AArch64 ELF supported direct-backend constructs");
  }
  return false;
}

static bool a64_elf_record_data_patch(void *user, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  A64ElfPatchContext *ctx = user;
  if (!ctx) return a64_diag(diag, "direct AArch64 ELF readonly data patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->data_patch_len + 1 > ctx->data_patch_cap) {
    ctx->data_patch_cap = z_grow_capacity(ctx->data_patch_cap, ctx->data_patch_len + 1, 8);
    ctx->data_patches = z_checked_reallocarray(ctx->data_patches, ctx->data_patch_cap, sizeof(A64ElfDataPatch));
  }
  if (!ctx->data_patches) return a64_diag(diag, "direct AArch64 ELF backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  ctx->data_patches[ctx->data_patch_len++] = (A64ElfDataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

static bool a64_elf_record_call_patch(void *user, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  A64ElfPatchContext *ctx = user;
  if (!ctx) return a64_diag(diag, "direct AArch64 ELF call patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->call_patch_len + 1 > ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(A64ElfCallPatch));
  }
  if (!ctx->call_patches) return a64_diag(diag, "direct AArch64 ELF backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  ctx->call_patches[ctx->call_patch_len++] = (A64ElfCallPatch){
    .patch_offset = patch_offset,
    .callee_index = callee_index,
    .external_index = value ? value->external_index : 0,
    .external_call = value && value->external_call
  };
  return true;
}

static const char *a64_runtime_helper_symbol(ZAArch64DirectRuntimeHelper helper) {
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
    case A64_DIRECT_RUNTIME_TERM_OP: return "zero_term_op";
    case A64_DIRECT_RUNTIME_TERM_READ_INPUT: return "zero_term_read_input";
    case A64_DIRECT_RUNTIME_MATH_OP: return "zero_math_op";
    case A64_DIRECT_RUNTIME_MATH_USIZE_OP: return "zero_math_usize_op";
    case A64_DIRECT_RUNTIME_SEARCH_OP: return "zero_search_op";
    case A64_DIRECT_RUNTIME_SORT_OP: return "zero_sort_op";
    case A64_DIRECT_RUNTIME_SORT_IS_SORTED_OP: return "zero_sort_is_sorted_op";
    case A64_DIRECT_RUNTIME_HTTP_REQUEST_METHOD_NAME: return "zero_http_request_method_name";
    case A64_DIRECT_RUNTIME_HTTP_REQUEST_PATH: return "zero_http_request_path";
    case A64_DIRECT_RUNTIME_PROC_CAPTURE: return "zero_proc_capture";
    case A64_DIRECT_RUNTIME_PROC_CAPTURE_FILES: return "zero_proc_capture_files";
    case A64_DIRECT_RUNTIME_HELPER_COUNT: break;
  }
  return "";
}

static bool a64_runtime_helper_valid(ZAArch64DirectRuntimeHelper helper) {
  return helper >= 0 && helper < A64_DIRECT_RUNTIME_HELPER_COUNT;
}

static bool a64_elf_record_runtime_patch(void *user, size_t patch_offset, ZAArch64DirectRuntimeHelper helper, const IrValue *value, ZDiag *diag) {
  A64ElfPatchContext *ctx = user;
  if (!ctx || !a64_runtime_helper_valid(helper)) return a64_diag(diag, "direct AArch64 ELF runtime patch requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (!ctx->allow_runtime_patches) return a64_diag(diag, "direct AArch64 ELF executable runtime helpers require object emission and an explicit runtime link step", value ? value->line : 1, value ? value->column : 1, "use --emit obj and link zero_runtime.c");
  A64ElfRuntimePatchList *list = &ctx->runtime_patches[helper];
  if (list->len + 1 > list->cap) {
    list->cap = z_grow_capacity(list->cap, list->len + 1, 4);
    list->items = z_checked_reallocarray(list->items, list->cap, sizeof(A64ElfRuntimePatch));
  }
  if (!list->items) return a64_diag(diag, "direct AArch64 ELF backend ran out of memory", value ? value->line : 1, value ? value->column : 1, "allocation failed");
  list->items[list->len++] = (A64ElfRuntimePatch){.patch_offset = patch_offset};
  return true;
}

static void a64_elf_patch_context_free(A64ElfPatchContext *ctx) {
  if (!ctx) return;
  free(ctx->data_patches);
  free(ctx->call_patches);
  for (unsigned i = 0; i < A64_DIRECT_RUNTIME_HELPER_COUNT; i++) free(ctx->runtime_patches[i].items);
}

static bool a64_elf_emit_world_write(ZBuf *text, const IrInstr *instr, ZAArch64DirectContext *ctx, ZDiag *diag) {
  (void)ctx;
  z_aarch64_emit_movz_x(text, 8, 64); // Linux SYS_write(fd=x0, buf=x1, len=x2)
  z_aarch64_emit_svc(text, 0);
  z_aarch64_emit_cmp_x(text, 0, 31);
  size_t ok_patch = z_aarch64_emit_b_cond_placeholder(text, 10);
  z_aarch64_emit_brk(text);
  z_aarch64_patch_cond19(text, ok_patch, text->len);
  (void)instr;
  (void)diag;
  return true;
}

size_t z_elf_aarch64_stack_bytes_from_ir(const IrProgram *program) {
  return z_aarch64_direct_stack_bytes_from_ir(program);
}

size_t z_elf_aarch64_max_frame_bytes_from_ir(const IrProgram *program) {
  return z_aarch64_direct_max_frame_bytes_from_ir(program);
}

static void a64_patch_data_patches(ZBuf *text, const A64ElfPatchContext *patches, uint64_t rodata_addr, unsigned rodata_base_offset) {
  for (size_t i = 0; patches && i < patches->data_patch_len; i++) {
    const A64ElfDataPatch *patch = &patches->data_patches[i];
    uint64_t addr = rodata_addr + (patch->data_offset - rodata_base_offset);
    z_aarch64_patch_u64(text, patch->patch_offset, addr);
  }
}

static void a64_patch_call_patches(ZBuf *text, const A64ElfPatchContext *patches, const size_t *function_offsets, size_t function_count) {
  for (size_t i = 0; patches && i < patches->call_patch_len; i++) {
    const A64ElfCallPatch *patch = &patches->call_patches[i];
    if (patch->external_call) continue;
    if (patch->callee_index < function_count) z_aarch64_patch_branch26(text, patch->patch_offset, function_offsets[patch->callee_index]);
  }
}

static void a64_append_data_relocations(ZBuf *rela_text, const A64ElfPatchContext *patches, unsigned rodata_base_offset, uint32_t rodata_symbol) {
  for (size_t i = 0; patches && i < patches->data_patch_len; i++) {
    const A64ElfDataPatch *patch = &patches->data_patches[i];
    z_elf_append_rela(rela_text, patch->patch_offset, rodata_symbol, R_AARCH64_ABS64, patch->data_offset - rodata_base_offset);
  }
}

static void a64_append_external_call_relocations(ZBuf *rela_text, const A64ElfPatchContext *patches, uint32_t external_symbol_base) {
  for (size_t i = 0; patches && i < patches->call_patch_len; i++) {
    const A64ElfCallPatch *patch = &patches->call_patches[i];
    if (!patch->external_call) continue;
    z_elf_append_rela(rela_text, patch->patch_offset, external_symbol_base + patch->external_index, R_AARCH64_CALL26, 0);
  }
}

static void a64_append_runtime_relocations(ZBuf *rela_text, const A64ElfPatchContext *patches, ZAArch64DirectRuntimeHelper helper, uint32_t runtime_symbol) {
  if (!patches || !a64_runtime_helper_valid(helper)) return;
  const A64ElfRuntimePatchList *list = &patches->runtime_patches[helper];
  for (size_t i = 0; i < list->len; i++) {
    z_elf_append_rela(rela_text, list->items[i].patch_offset, runtime_symbol, R_AARCH64_CALL26, 0);
  }
}

static uint32_t a64_append_exported_function_symbols(ZBuf *symtab, const IrProgram *ir, const size_t *function_offsets, const size_t *function_sizes, const uint32_t *symbol_names, bool has_rodata) {
  uint32_t external_symbol_base = has_rodata ? 2u : 1u;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (function_sizes[i] > 0 && ir->functions[i].is_exported) {
      z_elf_append_symbol(symtab, symbol_names[i], 0x12, 1, function_offsets[i], function_sizes[i]);
      external_symbol_base++;
    }
  }
  return external_symbol_base;
}

static uint32_t a64_append_runtime_symbols(ZBuf *strtab, ZBuf *symtab, ZBuf *rela_text, const A64ElfPatchContext *patches, uint32_t next_symbol) {
  for (unsigned helper = 0; helper < A64_DIRECT_RUNTIME_HELPER_COUNT; helper++) {
    ZAArch64DirectRuntimeHelper runtime_helper = (ZAArch64DirectRuntimeHelper)helper;
    const A64ElfRuntimePatchList *list = patches ? &patches->runtime_patches[helper] : NULL;
    if (!list || list->len == 0) continue;
    uint32_t name = (uint32_t)strtab->len;
    zbuf_append(strtab, a64_runtime_helper_symbol(runtime_helper));
    z_elf_append_u8(strtab, 0);
    z_elf_append_symbol(symtab, name, 0x12, 0, 0, 0);
    a64_append_runtime_relocations(rela_text, patches, runtime_helper, next_symbol);
    next_symbol++;
  }
  return next_symbol;
}

static void a64_append_external_symbols(ZBuf *strtab, ZBuf *symtab, const IrProgram *ir, uint32_t *external_names) {
  for (size_t i = 0; i < ir->external_function_len; i++) {
    external_names[i] = (uint32_t)strtab->len;
    zbuf_append(strtab, ir->external_functions[i].symbol ? ir->external_functions[i].symbol : "zero_external");
    z_elf_append_u8(strtab, 0);
    z_elf_append_symbol(symtab, external_names[i], 0x12, 0, 0, 0);
  }
}

static void a64_write_object_image(ZBuf *out, ZBuf *text, ZBuf *rodata, ZBuf *rela_text, ZBuf *symtab, ZBuf *strtab, bool has_rodata) {
  ZElfObjectImage image = {
    .machine = Z_ELF_MACHINE_AARCH64,
    .text = text,
    .text_align = 16,
    .rodata = has_rodata ? rodata : NULL,
    .rodata_align = 8,
    .rela_text = rela_text->len > 0 ? rela_text : NULL,
    .symtab = symtab,
    .strtab = strtab,
    .local_symbol_count = has_rodata ? 2 : 1
  };
  z_elf_write_object64(out, &image);
}

bool z_emit_elf_aarch64_object_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return a64_diag(diag, "direct AArch64 ELF object backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) {
    bool ok = a64_diag(diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
    z_diag_set_backend_blocker(diag, &ir->backend_blocker);
    return ok;
  }

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
  unsigned rodata_base_offset = z_aarch64_direct_rodata_base_offset(ir);
  if (has_rodata) {
    z_aarch64_direct_append_rodata(&rodata, ir, rodata_base_offset);
    z_elf_append_symbol(&symtab, 0, 0x03, 2, 0, 0);
  }

  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  size_t *function_sizes = z_checked_calloc(ir->function_len, sizeof(size_t));
  uint32_t *symbol_names = z_checked_calloc(ir->function_len, sizeof(uint32_t));
  uint32_t *external_names = ir->external_function_len > 0 ? z_checked_calloc(ir->external_function_len, sizeof(uint32_t)) : NULL;
  if (!function_offsets || !function_sizes || !symbol_names || (ir->external_function_len > 0 && !external_names)) {
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    free(external_names);
    zbuf_free(&text);
    zbuf_free(&rodata);
    zbuf_free(&rela_text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    return a64_diag(diag, "direct AArch64 ELF object backend ran out of memory", 1, 1, "allocation failed");
  }
  A64ElfPatchContext patch_ctx = {.allow_runtime_patches = true};
  ZAArch64DirectContext ctx = {
    .program = ir,
    .function_offsets = function_offsets,
    .function_count = ir->function_len,
    .rodata_base_offset = rodata_base_offset,
    .patch_user = &patch_ctx,
    .record_data_patch = a64_elf_record_data_patch,
    .record_call_patch = a64_elf_record_call_patch,
    .record_runtime_patch = a64_elf_record_runtime_patch
  };

  bool has_export = false;
  for (size_t i = 0; i < ir->function_len; i++) {
    if (ir->functions[i].is_exported) has_export = true;
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!z_aarch64_direct_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      a64_elf_patch_context_free(&patch_ctx);
      free(function_offsets);
      free(function_sizes);
      free(symbol_names);
      free(external_names);
      zbuf_free(&text);
      zbuf_free(&rodata);
      zbuf_free(&rela_text);
      zbuf_free(&strtab);
      zbuf_free(&symtab);
      return false;
    }
    function_sizes[i] = text.len - function_offsets[i];
    if (ir->functions[i].is_exported) {
      symbol_names[i] = (uint32_t)strtab.len;
      zbuf_append(&strtab, ir->functions[i].name ? ir->functions[i].name : "zero_fn");
      z_elf_append_u8(&strtab, 0);
    }
  }
  if (!has_export) {
    a64_elf_patch_context_free(&patch_ctx);
    free(function_offsets);
    free(function_sizes);
    free(symbol_names);
    free(external_names);
    zbuf_free(&text);
    zbuf_free(&rodata);
    zbuf_free(&rela_text);
    zbuf_free(&strtab);
    zbuf_free(&symtab);
    return a64_diag(diag, "direct AArch64 ELF object backend requires at least one exported function", 1, 1, "no exported function");
  }

  a64_patch_call_patches(&text, &patch_ctx, function_offsets, ir->function_len);
  if (has_rodata) a64_append_data_relocations(&rela_text, &patch_ctx, rodata_base_offset, 1);
  uint32_t external_symbol_base = a64_append_exported_function_symbols(&symtab, ir, function_offsets, function_sizes, symbol_names, has_rodata);
  external_symbol_base = a64_append_runtime_symbols(&strtab, &symtab, &rela_text, &patch_ctx, external_symbol_base);
  a64_append_external_symbols(&strtab, &symtab, ir, external_names);
  a64_append_external_call_relocations(&rela_text, &patch_ctx, external_symbol_base);

  a64_write_object_image(out, &text, &rodata, &rela_text, &symtab, &strtab, has_rodata);

  a64_elf_patch_context_free(&patch_ctx);
  free(function_offsets);
  free(function_sizes);
  free(symbol_names);
  free(external_names);
  zbuf_free(&text);
  zbuf_free(&rodata);
  zbuf_free(&rela_text);
  zbuf_free(&strtab);
  zbuf_free(&symtab);
  return true;
}

bool z_emit_elf_aarch64_exe_from_ir(const IrProgram *ir, ZBuf *out, ZDiag *diag) {
  if (!ir) return a64_diag(diag, "direct AArch64 ELF executable backend requires MIR", 1, 1, "missing MIR");
  if (!ir->mir_valid) {
    bool ok = a64_diag(diag, ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed", ir->mir_line, ir->mir_column, ir->mir_actual);
    z_diag_set_backend_blocker(diag, &ir->backend_blocker);
    return ok;
  }
  if (!z_direct_exe_reject_c_import_calls(ir, diag, "AArch64 ELF")) return false;
  unsigned main_index = 0;
  if (!z_aarch64_direct_find_main(ir, &main_index, diag)) return false;

  ZBuf text;
  ZBuf rodata;
  zbuf_init(&text);
  zbuf_init(&rodata);
  bool has_rodata = true;
  unsigned rodata_base_offset = z_aarch64_direct_rodata_base_offset(ir);
  ZDirectTrapMessages trap_messages = {0};
  z_aarch64_direct_append_rodata(&rodata, ir, rodata_base_offset);
  z_aarch64_direct_append_trap_messages(&rodata, rodata_base_offset, &trap_messages);

  size_t start_call_patch = z_aarch64_emit_bl_placeholder(&text);
  z_aarch64_emit_movz_x(&text, 8, 93);
  z_aarch64_emit_svc(&text, 0);
  z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));

  size_t *function_offsets = z_checked_calloc(ir->function_len, sizeof(size_t));
  if (!function_offsets) {
    zbuf_free(&text);
    zbuf_free(&rodata);
    return a64_diag(diag, "direct AArch64 ELF executable backend ran out of memory", 1, 1, "allocation failed");
  }
  A64ElfPatchContext patch_ctx = {0};
  ZAArch64DirectContext ctx = {
    .program = ir,
    .function_offsets = function_offsets,
    .function_count = ir->function_len,
    .rodata_base_offset = rodata_base_offset,
    .patch_user = &patch_ctx,
    .record_data_patch = a64_elf_record_data_patch,
    .record_call_patch = a64_elf_record_call_patch,
    .record_runtime_patch = a64_elf_record_runtime_patch,
    .emit_world_write = a64_elf_emit_world_write,
    .trap_messages = trap_messages
  };
  for (size_t i = 0; i < ir->function_len; i++) {
    z_aarch64_pad_to(&text, z_aarch64_align(text.len, 16));
    function_offsets[i] = text.len;
    if (!z_aarch64_direct_emit_function_text(&text, &ir->functions[i], &ctx, diag)) {
      z_direct_trap_branches_free(ctx.trap_branches, Z_DIRECT_TRAP_KIND_COUNT);
      a64_elf_patch_context_free(&patch_ctx);
      free(function_offsets);
      zbuf_free(&text);
      zbuf_free(&rodata);
      return false;
    }
  }
  if (!z_aarch64_direct_emit_trap_stubs(&text, &ctx, diag)) {
    a64_elf_patch_context_free(&patch_ctx);
    free(function_offsets);
    zbuf_free(&text);
    zbuf_free(&rodata);
    return false;
  }
  z_aarch64_patch_branch26(&text, start_call_patch, function_offsets[main_index]);
  a64_patch_call_patches(&text, &patch_ctx, function_offsets, ir->function_len);

  const uint64_t base_addr = 0x400000;
  const size_t ehdr_size = 64;
  const size_t phdr_size = 56;
  const size_t text_offset = ehdr_size + phdr_size;
  const uint64_t entry_addr = base_addr + text_offset;
  size_t rodata_offset = has_rodata ? z_elf_align(text_offset + text.len, 8) : 0;
  uint64_t rodata_addr = has_rodata ? base_addr + rodata_offset : 0;
  a64_patch_data_patches(&text, &patch_ctx, rodata_addr, rodata_base_offset);
  ZElfExecutableImage image = {
    .machine = Z_ELF_MACHINE_AARCH64,
    .base_addr = base_addr,
    .entry_addr = entry_addr,
    .text_offset = text_offset,
    .text = &text,
    .rodata = has_rodata ? &rodata : NULL,
    .rodata_offset = rodata_offset,
    .segment_align = 0x1000
  };
  z_elf_write_executable64(out, &image);
  a64_elf_patch_context_free(&patch_ctx);
  free(function_offsets);
  zbuf_free(&text);
  zbuf_free(&rodata);
  return true;
}
