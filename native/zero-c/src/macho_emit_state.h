#ifndef ZERO_C_MACHO_EMIT_STATE_H
#define ZERO_C_MACHO_EMIT_STATE_H

#include "zero.h"

#include <stdint.h>

typedef enum {
  MACHO_RUNTIME_WORLD_WRITE,
  MACHO_RUNTIME_JSON_PARSE_BYTES,
  MACHO_RUNTIME_JSON_DIAGNOSTIC,
  MACHO_RUNTIME_JSON_FIELD,
  MACHO_RUNTIME_JSON_LOOKUP_SCALAR,
  MACHO_RUNTIME_JSON_STRING_DECODE,
  MACHO_RUNTIME_JSON_STRING_FIELD,
  MACHO_RUNTIME_JSON_WRITE_STRING,
  MACHO_RUNTIME_JSON_WRITE_FIELD_RAW,
  MACHO_RUNTIME_JSON_WRITE_FIELD_STRING,
  MACHO_RUNTIME_JSON_WRITE_FIELD_U32,
  MACHO_RUNTIME_JSON_WRITE_FIELD_BOOL,
  MACHO_RUNTIME_JSON_WRITE_OBJECT1_STRING,
  MACHO_RUNTIME_JSON_WRITE_OBJECT1_U32,
  MACHO_RUNTIME_JSON_WRITE_OBJECT1_BOOL,
  MACHO_RUNTIME_JSON_WRITE_OBJECT2_FIELDS,
  MACHO_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD,
  MACHO_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD,
  MACHO_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD,
  MACHO_RUNTIME_JSON_WRITE_ARRAY2_STRINGS,
  MACHO_RUNTIME_JSON_WRITE_ARRAY2_U32,
  MACHO_RUNTIME_JSON_WRITE_ARRAY2_BOOLS,
  MACHO_RUNTIME_HTTP_FETCH,
  MACHO_RUNTIME_HTTP_RESULT_OK,
  MACHO_RUNTIME_HTTP_RESULT_STATUS,
  MACHO_RUNTIME_HTTP_RESULT_BODY_LEN,
  MACHO_RUNTIME_HTTP_RESULT_ERROR,
  MACHO_RUNTIME_HTTP_RESPONSE_LEN,
  MACHO_RUNTIME_HTTP_RESPONSE_HEADERS_LEN,
  MACHO_RUNTIME_HTTP_RESPONSE_BODY_OFFSET,
  MACHO_RUNTIME_HTTP_HEADER_VALUE,
  MACHO_RUNTIME_HTTP_HEADER_FOUND,
  MACHO_RUNTIME_HTTP_HEADER_OFFSET,
  MACHO_RUNTIME_HTTP_HEADER_LEN,
  MACHO_RUNTIME_HTTP_WRITE_JSON_RESPONSE,
  MACHO_RUNTIME_HTTP_REQUEST_METHOD_NAME,
  MACHO_RUNTIME_HTTP_REQUEST_PATH,
  MACHO_RUNTIME_HTTP_REQUEST_MATCHES,
  MACHO_RUNTIME_HTTP_REQUEST_BODY_WITHIN,
  MACHO_RUNTIME_ASCII_OP,
  MACHO_RUNTIME_TEXT_OP,
  MACHO_RUNTIME_PARSE_OP,
  MACHO_RUNTIME_PARSE_USIZE,
  MACHO_RUNTIME_TIME_OP,
  MACHO_RUNTIME_TERM_OP,
  MACHO_RUNTIME_MATH_OP,
  MACHO_RUNTIME_MATH_USIZE_OP,
  MACHO_RUNTIME_SEARCH_OP,
  MACHO_RUNTIME_SORT_OP,
  MACHO_RUNTIME_SORT_IS_SORTED_OP,
  MACHO_RUNTIME_STR_CONTAINS,
  MACHO_RUNTIME_STR_BUFFER_OP,
  MACHO_RUNTIME_STR_CONCAT,
  MACHO_RUNTIME_STR_REPEAT,
  MACHO_RUNTIME_STR_TRIM_OP,
  MACHO_RUNTIME_STR_PAIR_OP,
  MACHO_RUNTIME_STR_COUNT_BYTE,
  MACHO_RUNTIME_STR_WORD_COUNT_ASCII,
  MACHO_RUNTIME_CRYPTO_DIGEST,
  MACHO_RUNTIME_CRYPTO_HMAC_SHA256,
  MACHO_RUNTIME_CRYPTO_HMAC_SHA256_HEX,
  MACHO_RUNTIME_FS_READ_BYTES,
  MACHO_RUNTIME_FS_READ_BYTES_AT,
  MACHO_RUNTIME_ARGS_FIND,
  MACHO_RUNTIME_PARSE_I32,
  MACHO_RUNTIME_PARSE_U32,
  MACHO_RUNTIME_FMT_BOOL,
  MACHO_RUNTIME_FMT_HEX_LOWER_U32,
  MACHO_RUNTIME_FMT_I32,
  MACHO_RUNTIME_FMT_U32,
  MACHO_RUNTIME_FMT_USIZE,
  MACHO_RUNTIME_RAND_ENTROPY_U32,
  MACHO_RUNTIME_PROC_CAPTURE,
  MACHO_RUNTIME_PROC_CAPTURE_FILES,
  MACHO_RUNTIME_HELPER_COUNT
} MachORuntimeHelper;

typedef struct {
  size_t patch_offset;
} MachOPatch;

typedef struct {
  MachOPatch *items;
  size_t len;
  size_t cap;
} MachOPatchList;

typedef struct {
  size_t patch_offset;
  unsigned callee_index;
  unsigned external_index;
  int line;
  int column;
  bool external_call;
} MachOCallPatch;

typedef struct {
  size_t patch_offset;
  unsigned data_offset;
} MachODataPatch;

typedef struct {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  MachOCallPatch *call_patches;
  size_t call_patch_len;
  size_t call_patch_cap;
  MachODataPatch *data_patches;
  size_t data_patch_len;
  size_t data_patch_cap;
  MachOPatchList runtime_patches[MACHO_RUNTIME_HELPER_COUNT];
  unsigned rodata_base_offset;
  bool pie_relative_data;
  bool seed_main_process_args;
  ZDirectTrapMessages trap_messages;
  ZDirectTrapBranchList trap_branches[Z_DIRECT_TRAP_KIND_COUNT];
  ZDirectLoopFrame *loop;
} MachOEmitContext;

const char *z_macho_runtime_helper_symbol(MachORuntimeHelper helper);
void z_macho_emit_context_free(MachOEmitContext *ctx);
bool z_macho_record_call_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag);
bool z_macho_record_data_patch(MachOEmitContext *ctx, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag);
bool z_macho_record_value_runtime_patch(MachOEmitContext *ctx, MachORuntimeHelper helper, size_t patch_offset, const IrValue *value, ZDiag *diag);
bool z_macho_record_instr_runtime_patch(MachOEmitContext *ctx, MachORuntimeHelper helper, size_t patch_offset, const IrInstr *instr, ZDiag *diag);
size_t z_macho_runtime_patch_count(const MachOEmitContext *ctx, MachORuntimeHelper helper);
const MachOPatchList *z_macho_runtime_patch_list(const MachOEmitContext *ctx, MachORuntimeHelper helper);
bool z_macho_has_unsupported_exe_runtime_patches(const MachOEmitContext *ctx);
void z_macho_append_call_relocations(ZBuf *relocs, const MachOEmitContext *ctx, uint32_t external_symbol_base);
void z_macho_append_runtime_relocations(ZBuf *relocs, const MachOEmitContext *ctx, MachORuntimeHelper helper, unsigned symbol_index);
size_t z_macho_data_relocation_count(const MachOEmitContext *ctx);
size_t z_macho_text_relocation_count(const MachOEmitContext *ctx);
void z_macho_append_data_relocations(ZBuf *relocs, const MachOEmitContext *ctx, unsigned data_symbol_index);

#endif
