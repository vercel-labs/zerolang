#ifndef ZERO_C_COFF_EMIT_STATE_H
#define ZERO_C_COFF_EMIT_STATE_H

#include "zero.h"
#include "coff_format.h"

typedef enum {
  COFF_RUNTIME_WORLD_WRITE,
  COFF_RUNTIME_STR_BUFFER_OP,
  COFF_RUNTIME_STR_CONCAT,
  COFF_RUNTIME_STR_REPEAT,
  COFF_RUNTIME_STR_TRIM_OP,
  COFF_RUNTIME_STR_PAIR_OP,
  COFF_RUNTIME_STR_COUNT_BYTE,
  COFF_RUNTIME_STR_WORD_COUNT_ASCII,
  COFF_RUNTIME_ASCII_OP,
  COFF_RUNTIME_TEXT_OP,
  COFF_RUNTIME_PARSE_OP,
  COFF_RUNTIME_PARSE_USIZE,
  COFF_RUNTIME_PARSE_I32,
  COFF_RUNTIME_PARSE_U32,
  COFF_RUNTIME_FMT_BOOL,
  COFF_RUNTIME_FMT_HEX_U32,
  COFF_RUNTIME_FMT_I32,
  COFF_RUNTIME_FMT_U32,
  COFF_RUNTIME_FMT_USIZE,
  COFF_RUNTIME_TIME_OP,
  COFF_RUNTIME_MATH_OP,
  COFF_RUNTIME_MATH_USIZE_OP,
  COFF_RUNTIME_HELPER_COUNT
} CoffRuntimeHelper;

typedef struct {
  size_t patch_offset;
} CoffPatch;

typedef struct {
  CoffPatch *items;
  size_t len;
  size_t cap;
} CoffPatchList;

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
  unsigned external_index;
  bool external_call;
} CoffCallPatch;

typedef struct {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  CoffCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  ZCoffImageDataPatch *rodata_patches;
  size_t rodata_patch_len;
  size_t rodata_patch_cap;
  CoffPatchList runtime_patches[COFF_RUNTIME_HELPER_COUNT];
  unsigned rodata_base_offset;
} CoffEmitContext;

const char *z_coff_runtime_helper_symbol(CoffRuntimeHelper helper);
void z_coff_emit_context_free(CoffEmitContext *ctx);
bool z_coff_record_call_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag);
bool z_coff_record_rodata_patch(CoffEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag);
bool z_coff_record_value_runtime_patch(CoffEmitContext *ctx, CoffRuntimeHelper helper, size_t patch_offset, const IrValue *value, ZDiag *diag);
bool z_coff_record_instr_runtime_patch(CoffEmitContext *ctx, CoffRuntimeHelper helper, size_t patch_offset, const IrInstr *instr, ZDiag *diag);
size_t z_coff_runtime_patch_count(const CoffEmitContext *ctx, CoffRuntimeHelper helper);
size_t z_coff_text_relocation_count(const CoffEmitContext *ctx);
void z_coff_patch_call_patches(ZBuf *text, const CoffEmitContext *ctx);
void z_coff_patch_runtime_patches(ZBuf *text, const CoffEmitContext *ctx, CoffRuntimeHelper helper, size_t target_offset);
void z_coff_append_call_relocations(ZBuf *relocs, const CoffEmitContext *ctx, uint32_t function_symbol_base, uint32_t external_symbol_base);
void z_coff_append_rodata_relocations(ZBuf *relocs, const CoffEmitContext *ctx, uint32_t rodata_symbol);
void z_coff_append_runtime_relocations(ZBuf *relocs, const CoffEmitContext *ctx, CoffRuntimeHelper helper, uint32_t runtime_symbol);

#endif
