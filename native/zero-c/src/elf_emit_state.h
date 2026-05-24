#ifndef ZERO_C_ELF_EMIT_STATE_H
#define ZERO_C_ELF_EMIT_STATE_H

#include "zero.h"

#include <stdint.h>

typedef enum {
  ELF_RUNTIME_JSON_PARSE_BYTES,
  ELF_RUNTIME_HTTP_FETCH,
  ELF_RUNTIME_HTTP_RESULT_OK,
  ELF_RUNTIME_HTTP_RESULT_STATUS,
  ELF_RUNTIME_HTTP_RESULT_BODY_LEN,
  ELF_RUNTIME_HTTP_RESULT_ERROR,
  ELF_RUNTIME_HTTP_RESPONSE_LEN,
  ELF_RUNTIME_HTTP_RESPONSE_HEADERS_LEN,
  ELF_RUNTIME_HTTP_RESPONSE_BODY_OFFSET,
  ELF_RUNTIME_HTTP_HEADER_VALUE,
  ELF_RUNTIME_HTTP_HEADER_FOUND,
  ELF_RUNTIME_HTTP_HEADER_OFFSET,
  ELF_RUNTIME_HTTP_HEADER_LEN,
  ELF_RUNTIME_HEX_ENCODE,
  ELF_RUNTIME_UTF8_VALID,
  ELF_RUNTIME_HELPER_COUNT
} ElfRuntimeHelper;

typedef struct {
  size_t patch_offset;
} ElfPatch;

typedef struct {
  ElfPatch *items;
  size_t len;
  size_t cap;
} ElfPatchList;

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
} ElfCallPatch;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} ElfRodataPatch;

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
  ElfPatchList runtime_patches[ELF_RUNTIME_HELPER_COUNT];
  bool emit_rodata_relocations;
  bool seed_main_process_args;
  unsigned rodata_base_offset;
  uint64_t rodata_addr;
} ElfEmitContext;

const char *z_elf_runtime_helper_symbol(ElfRuntimeHelper helper);
void z_elf_emit_context_free(ElfEmitContext *ctx);
bool z_elf_record_call_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned callee_index, ZDiag *diag, const IrValue *value);
bool z_elf_record_rodata_patch(ElfEmitContext *ctx, size_t patch_offset, unsigned data_offset, ZDiag *diag, const IrValue *value);
bool z_elf_record_value_runtime_patch(ElfEmitContext *ctx, ElfRuntimeHelper helper, size_t patch_offset, ZDiag *diag, const IrValue *value);
size_t z_elf_runtime_patch_count(const ElfEmitContext *ctx, ElfRuntimeHelper helper);
bool z_elf_has_runtime_patches(const ElfEmitContext *ctx);
void z_elf_patch_call_patches(ZBuf *code, const ElfEmitContext *ctx);
void z_elf_patch_rodata_patches(ZBuf *code, const ElfEmitContext *ctx);
void z_elf_append_rodata_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, uint32_t rodata_symbol);
void z_elf_append_runtime_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, ElfRuntimeHelper helper, uint32_t runtime_symbol);

#endif
