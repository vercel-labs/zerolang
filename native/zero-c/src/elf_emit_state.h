#ifndef ZERO_C_ELF_EMIT_STATE_H
#define ZERO_C_ELF_EMIT_STATE_H

#include "zero.h"

#include <stdint.h>

typedef enum {
  ELF_RUNTIME_JSON_PARSE_BYTES,
  ELF_RUNTIME_JSON_DIAGNOSTIC,
  ELF_RUNTIME_JSON_FIELD,
  ELF_RUNTIME_JSON_LOOKUP_SCALAR,
  ELF_RUNTIME_JSON_STRING_DECODE,
  ELF_RUNTIME_JSON_STRING_FIELD,
  ELF_RUNTIME_JSON_WRITE_STRING,
  ELF_RUNTIME_JSON_WRITE_FIELD_RAW,
  ELF_RUNTIME_JSON_WRITE_FIELD_STRING,
  ELF_RUNTIME_JSON_WRITE_FIELD_U32,
  ELF_RUNTIME_JSON_WRITE_FIELD_BOOL,
  ELF_RUNTIME_JSON_WRITE_OBJECT1_STRING,
  ELF_RUNTIME_JSON_WRITE_OBJECT1_U32,
  ELF_RUNTIME_JSON_WRITE_OBJECT1_BOOL,
  ELF_RUNTIME_JSON_WRITE_OBJECT2_FIELDS,
  ELF_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD,
  ELF_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD,
  ELF_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD,
  ELF_RUNTIME_JSON_WRITE_ARRAY2_STRINGS,
  ELF_RUNTIME_JSON_WRITE_ARRAY2_U32,
  ELF_RUNTIME_JSON_WRITE_ARRAY2_BOOLS,
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
  ELF_RUNTIME_HTTP_WRITE_JSON_RESPONSE,
  ELF_RUNTIME_HTTP_REQUEST_METHOD_NAME,
  ELF_RUNTIME_HTTP_REQUEST_PATH,
  ELF_RUNTIME_HTTP_REQUEST_MATCHES,
  ELF_RUNTIME_HTTP_REQUEST_BODY_WITHIN,
  ELF_RUNTIME_ASCII_OP,
  ELF_RUNTIME_TEXT_OP,
  ELF_RUNTIME_PARSE_OP,
  ELF_RUNTIME_PARSE_USIZE,
  ELF_RUNTIME_TIME_OP,
  ELF_RUNTIME_TERM_OP,
  ELF_RUNTIME_MATH_OP,
  ELF_RUNTIME_MATH_USIZE_OP,
  ELF_RUNTIME_SEARCH_OP,
  ELF_RUNTIME_SORT_OP,
  ELF_RUNTIME_SORT_IS_SORTED_OP,
  ELF_RUNTIME_STR_CONTAINS,
  ELF_RUNTIME_STR_BUFFER_OP,
  ELF_RUNTIME_STR_CONCAT,
  ELF_RUNTIME_STR_REPEAT,
  ELF_RUNTIME_STR_TRIM_OP,
  ELF_RUNTIME_STR_PAIR_OP,
  ELF_RUNTIME_STR_COUNT_BYTE,
  ELF_RUNTIME_STR_WORD_COUNT_ASCII,
  ELF_RUNTIME_CRYPTO_DIGEST,
  ELF_RUNTIME_CRYPTO_HMAC_SHA256,
  ELF_RUNTIME_CRYPTO_HMAC_SHA256_HEX,
  ELF_RUNTIME_ARGS_FIND,
  ELF_RUNTIME_PARSE_I32,
  ELF_RUNTIME_PARSE_U32,
  ELF_RUNTIME_FMT_BOOL,
  ELF_RUNTIME_FMT_HEX_LOWER_U32,
  ELF_RUNTIME_FMT_I32,
  ELF_RUNTIME_FMT_U32,
  ELF_RUNTIME_FMT_USIZE,
  ELF_RUNTIME_PROC_CAPTURE,
  ELF_RUNTIME_PROC_CAPTURE_FILES,
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
  unsigned external_index;
  bool external_call;
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
  ZDirectTrapMessages trap_messages;
  ZDirectTrapBranchList trap_branches[Z_DIRECT_TRAP_KIND_COUNT];
  ZDirectLoopFrame *loop;
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
void z_elf_append_external_call_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, uint32_t external_symbol_base);
void z_elf_append_runtime_relocations(ZBuf *rela_text, const ElfEmitContext *ctx, ElfRuntimeHelper helper, uint32_t runtime_symbol);

#endif
