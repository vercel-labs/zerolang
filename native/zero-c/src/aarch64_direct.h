#ifndef ZERO_C_AARCH64_DIRECT_H
#define ZERO_C_AARCH64_DIRECT_H

#include "zero.h"

typedef struct ZAArch64DirectContext ZAArch64DirectContext;

typedef enum {
  A64_DIRECT_RUNTIME_JSON_PARSE_BYTES,
  A64_DIRECT_RUNTIME_JSON_DIAGNOSTIC,
  A64_DIRECT_RUNTIME_JSON_FIELD,
  A64_DIRECT_RUNTIME_JSON_LOOKUP_SCALAR,
  A64_DIRECT_RUNTIME_JSON_STRING_DECODE,
  A64_DIRECT_RUNTIME_JSON_STRING_FIELD,
  A64_DIRECT_RUNTIME_JSON_WRITE_STRING,
  A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_RAW,
  A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_STRING,
  A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_U32,
  A64_DIRECT_RUNTIME_JSON_WRITE_FIELD_BOOL,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_STRING,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_U32,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT1_BOOL,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_FIELDS,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_STRING_FIELD,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_U32_FIELD,
  A64_DIRECT_RUNTIME_JSON_WRITE_OBJECT2_BOOL_FIELD,
  A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_STRINGS,
  A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_U32,
  A64_DIRECT_RUNTIME_JSON_WRITE_ARRAY2_BOOLS,
  A64_DIRECT_RUNTIME_STR_BUFFER_OP,
  A64_DIRECT_RUNTIME_STR_CONCAT,
  A64_DIRECT_RUNTIME_STR_REPEAT,
  A64_DIRECT_RUNTIME_STR_TRIM_OP,
  A64_DIRECT_RUNTIME_STR_PAIR_OP,
  A64_DIRECT_RUNTIME_STR_COUNT_BYTE,
  A64_DIRECT_RUNTIME_STR_WORD_COUNT_ASCII,
  A64_DIRECT_RUNTIME_CRYPTO_DIGEST,
  A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256,
  A64_DIRECT_RUNTIME_CRYPTO_HMAC_SHA256_HEX,
  A64_DIRECT_RUNTIME_ASCII_OP,
  A64_DIRECT_RUNTIME_TEXT_OP,
  A64_DIRECT_RUNTIME_PARSE_OP,
  A64_DIRECT_RUNTIME_PARSE_USIZE,
  A64_DIRECT_RUNTIME_PARSE_I32,
  A64_DIRECT_RUNTIME_PARSE_U32,
  A64_DIRECT_RUNTIME_FMT_BOOL,
  A64_DIRECT_RUNTIME_FMT_HEX_U32,
  A64_DIRECT_RUNTIME_FMT_I32,
  A64_DIRECT_RUNTIME_FMT_U32,
  A64_DIRECT_RUNTIME_FMT_USIZE,
  A64_DIRECT_RUNTIME_TIME_OP,
  A64_DIRECT_RUNTIME_MATH_OP,
  A64_DIRECT_RUNTIME_MATH_USIZE_OP,
  A64_DIRECT_RUNTIME_SEARCH_OP,
  A64_DIRECT_RUNTIME_SORT_OP,
  A64_DIRECT_RUNTIME_SORT_IS_SORTED_OP,
  A64_DIRECT_RUNTIME_HTTP_REQUEST_METHOD_NAME,
  A64_DIRECT_RUNTIME_HTTP_REQUEST_PATH,
  A64_DIRECT_RUNTIME_HELPER_COUNT
} ZAArch64DirectRuntimeHelper;

typedef bool (*ZAArch64DirectDataPatchFn)(void *user, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag);
typedef bool (*ZAArch64DirectCallPatchFn)(void *user, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag);
typedef bool (*ZAArch64DirectRuntimePatchFn)(void *user, size_t patch_offset, ZAArch64DirectRuntimeHelper helper, const IrValue *value, ZDiag *diag);
typedef bool (*ZAArch64DirectWorldWriteFn)(ZBuf *text, const IrInstr *instr, ZAArch64DirectContext *ctx, ZDiag *diag);

struct ZAArch64DirectContext {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  unsigned rodata_base_offset;
  void *patch_user;
  ZAArch64DirectDataPatchFn record_data_patch;
  ZAArch64DirectCallPatchFn record_call_patch;
  ZAArch64DirectRuntimePatchFn record_runtime_patch;
  ZAArch64DirectWorldWriteFn emit_world_write;
  ZDirectTrapMessages trap_messages;
  ZDirectTrapBranchList trap_branches[Z_DIRECT_TRAP_KIND_COUNT];
  ZDirectLoopFrame *loop;
};

bool z_aarch64_direct_emit_function_text(ZBuf *text, const IrFunction *fun, ZAArch64DirectContext *ctx, ZDiag *diag);
bool z_aarch64_direct_validate_function(const IrFunction *fun, ZDiag *diag);
const IrFunction *z_aarch64_direct_find_main(const IrProgram *program, unsigned *out_index, ZDiag *diag);
unsigned z_aarch64_direct_rodata_base_offset(const IrProgram *program);
void z_aarch64_direct_append_trap_messages(ZBuf *rodata, unsigned base_offset, ZDirectTrapMessages *messages);
bool z_aarch64_direct_emit_trap_stubs(ZBuf *text, ZAArch64DirectContext *ctx, ZDiag *diag);
void z_aarch64_direct_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset);
size_t z_aarch64_direct_stack_bytes_from_ir(const IrProgram *program);
size_t z_aarch64_direct_max_frame_bytes_from_ir(const IrProgram *program);

#endif
