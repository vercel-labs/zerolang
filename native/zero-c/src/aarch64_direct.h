#ifndef ZERO_C_AARCH64_DIRECT_H
#define ZERO_C_AARCH64_DIRECT_H

#include "zero.h"

typedef struct ZAArch64DirectContext ZAArch64DirectContext;

typedef bool (*ZAArch64DirectDataPatchFn)(void *user, size_t patch_offset, unsigned data_offset, const IrValue *value, ZDiag *diag);
typedef bool (*ZAArch64DirectCallPatchFn)(void *user, size_t patch_offset, unsigned callee_index, const IrValue *value, ZDiag *diag);
typedef bool (*ZAArch64DirectWorldWriteFn)(ZBuf *text, const IrInstr *instr, ZAArch64DirectContext *ctx, ZDiag *diag);

struct ZAArch64DirectContext {
  const IrProgram *program;
  size_t *function_offsets;
  size_t function_count;
  unsigned rodata_base_offset;
  void *patch_user;
  ZAArch64DirectDataPatchFn record_data_patch;
  ZAArch64DirectCallPatchFn record_call_patch;
  ZAArch64DirectWorldWriteFn emit_world_write;
};

bool z_aarch64_direct_emit_function_text(ZBuf *text, const IrFunction *fun, ZAArch64DirectContext *ctx, ZDiag *diag);
bool z_aarch64_direct_validate_function(const IrFunction *fun, ZDiag *diag);
const IrFunction *z_aarch64_direct_find_main(const IrProgram *program, unsigned *out_index, ZDiag *diag);
unsigned z_aarch64_direct_rodata_base_offset(const IrProgram *program);
void z_aarch64_direct_append_rodata(ZBuf *rodata, const IrProgram *program, unsigned base_offset);
size_t z_aarch64_direct_stack_bytes_from_ir(const IrProgram *program);
size_t z_aarch64_direct_max_frame_bytes_from_ir(const IrProgram *program);

#endif
