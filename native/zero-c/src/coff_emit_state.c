#include "coff_emit_state.h"
#include "x64_emit.h"

#include <stdio.h>
#include <stdlib.h>

static const char *const runtime_helper_symbols[COFF_RUNTIME_HELPER_COUNT] = {
  "zero_world_write",
};

static bool coff_emit_state_diag(ZDiag *diag, const char *message, int line, int column, const char *actual) {
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

static bool coff_runtime_helper_valid(CoffRuntimeHelper helper) {
  return helper >= 0 && helper < COFF_RUNTIME_HELPER_COUNT;
}

const char *z_coff_runtime_helper_symbol(CoffRuntimeHelper helper) {
  if (!coff_runtime_helper_valid(helper)) return "";
  return runtime_helper_symbols[helper];
}

void z_coff_emit_context_free(CoffEmitContext *ctx) {
  if (!ctx) return;
  free(ctx->call_patches);
  free(ctx->rodata_patches);
  for (unsigned i = 0; i < COFF_RUNTIME_HELPER_COUNT; i++) {
    free(ctx->runtime_patches[i].items);
  }
}

bool z_coff_record_call_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag) {
  if (!ctx || (((!value) || !value->external_call) && callee_index >= ctx->function_count)) {
    return coff_emit_state_diag(diag, "direct COFF call target index is out of range", value ? value->line : 1, value ? value->column : 1, "invalid callee");
  }
  if (ctx->call_patch_len == ctx->call_patch_cap) {
    ctx->call_patch_cap = z_grow_capacity(ctx->call_patch_cap, ctx->call_patch_len + 1, 8);
    ctx->call_patches = z_checked_reallocarray(ctx->call_patches, ctx->call_patch_cap, sizeof(CoffCallPatch));
  }
  ctx->call_patches[ctx->call_patch_len++] = (CoffCallPatch){
    .patch_offset = patch_offset,
    .callee_index = callee_index,
    .external_index = value ? value->external_index : 0,
    .external_call = value && value->external_call
  };
  return true;
}

bool z_coff_record_rodata_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag) {
  if (!ctx) return coff_emit_state_diag(diag, "direct COFF readonly data relocation requires an emit context", value ? value->line : 1, value ? value->column : 1, "missing context");
  if (ctx->rodata_patch_len == ctx->rodata_patch_cap) {
    ctx->rodata_patch_cap = z_grow_capacity(ctx->rodata_patch_cap, ctx->rodata_patch_len + 1, 8);
    ctx->rodata_patches = z_checked_reallocarray(ctx->rodata_patches, ctx->rodata_patch_cap, sizeof(ZCoffImageDataPatch));
  }
  ctx->rodata_patches[ctx->rodata_patch_len++] = (ZCoffImageDataPatch){.patch_offset = patch_offset, .data_offset = data_offset};
  return true;
}

bool z_coff_record_instr_runtime_patch(CoffEmitContext *ctx, CoffRuntimeHelper helper, size_t patch_offset, const IrInstr *instr, ZDiag *diag) {
  if (!ctx || !coff_runtime_helper_valid(helper)) {
    return coff_emit_state_diag(diag, "direct COFF runtime relocation requires an emit context", instr ? instr->line : 1, instr ? instr->column : 1, "missing context");
  }
  CoffPatchList *list = &ctx->runtime_patches[helper];
  if (list->len == list->cap) {
    list->cap = z_grow_capacity(list->cap, list->len + 1, 4);
    list->items = z_checked_reallocarray(list->items, list->cap, sizeof(CoffPatch));
  }
  list->items[list->len++] = (CoffPatch){.patch_offset = patch_offset};
  return true;
}

size_t z_coff_runtime_patch_count(const CoffEmitContext *ctx, CoffRuntimeHelper helper) {
  if (!ctx || !coff_runtime_helper_valid(helper)) return 0;
  return ctx->runtime_patches[helper].len;
}

size_t z_coff_text_relocation_count(const CoffEmitContext *ctx) {
  if (!ctx) return 0;
  size_t count = ctx->call_patch_len + ctx->rodata_patch_len;
  for (unsigned i = 0; i < COFF_RUNTIME_HELPER_COUNT; i++) {
    count += ctx->runtime_patches[i].len;
  }
  return count;
}

void z_coff_patch_call_patches(ZBuf *text, const CoffEmitContext *ctx) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const CoffCallPatch *patch = &ctx->call_patches[i];
    if (patch->external_call) continue;
    z_x64_patch_rel32(text, patch->patch_offset, ctx->function_offsets[patch->callee_index]);
  }
}

void z_coff_patch_runtime_patches(ZBuf *text, const CoffEmitContext *ctx, CoffRuntimeHelper helper, size_t target_offset) {
  if (!ctx || !coff_runtime_helper_valid(helper)) return;
  const CoffPatchList *patches = &ctx->runtime_patches[helper];
  for (size_t i = 0; i < patches->len; i++) {
    z_x64_patch_rel32(text, patches->items[i].patch_offset, target_offset);
  }
}

void z_coff_append_call_relocations(ZBuf *relocs, const CoffEmitContext *ctx, uint32_t function_symbol_base, uint32_t external_symbol_base) {
  for (size_t i = 0; ctx && i < ctx->call_patch_len; i++) {
    const CoffCallPatch *patch = &ctx->call_patches[i];
    uint32_t symbol_index = patch->external_call ? external_symbol_base + patch->external_index : function_symbol_base + patch->callee_index;
    z_coff_append_reloc_amd64(relocs, (uint32_t)patch->patch_offset, symbol_index, Z_COFF_RELOC_AMD64_REL32);
  }
}

void z_coff_append_rodata_relocations(ZBuf *relocs, const CoffEmitContext *ctx, uint32_t rodata_symbol) {
  for (size_t i = 0; ctx && i < ctx->rodata_patch_len; i++) {
    z_coff_append_reloc_amd64(relocs, (uint32_t)ctx->rodata_patches[i].patch_offset, rodata_symbol, Z_COFF_RELOC_AMD64_ADDR64);
  }
}

void z_coff_append_runtime_relocations(ZBuf *relocs, const CoffEmitContext *ctx, CoffRuntimeHelper helper, uint32_t runtime_symbol) {
  if (!ctx || !coff_runtime_helper_valid(helper)) return;
  const CoffPatchList *patches = &ctx->runtime_patches[helper];
  for (size_t i = 0; i < patches->len; i++) {
    z_coff_append_reloc_amd64(relocs, (uint32_t)patches->items[i].patch_offset, runtime_symbol, Z_COFF_RELOC_AMD64_REL32);
  }
}
