#ifndef ZERO_C_BUILDABILITY_INTERNAL_H
#define ZERO_C_BUILDABILITY_INTERNAL_H

#include "buildability.h"

typedef struct {
  const ZTargetInfo *target;
  const char *emit_kind;
  const char *backend_name;
  const char *expected;
  const char *help;
  ZDirectBackend backend;
  bool executable;
  bool freestanding;
} ZBuildability;

enum { BUILD_MACHO_SCRATCH_SLOT_COUNT = 32u, BUILD_AARCH64_SCRATCH_SLOT_COUNT = 32u };

const char *z_build_type_name(IrTypeKind type);
const char *z_build_value_kind_name(IrValueKind kind);
bool z_build_is_elf_scalar(IrTypeKind type);
bool z_build_is_scalar32(IrTypeKind type);
bool z_build_diag(const ZBuildability *ctx, ZDiag *diag, const char *message, int line, int column, const char *actual);
bool z_build_select(const IrProgram *ir, const ZTargetInfo *target, const char *emit_kind, bool freestanding, ZBuildability *ctx, ZDiag *diag);
bool z_build_backend_is_aarch64_direct(ZDirectBackend backend);
bool z_build_check_coff_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_coff_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_macho_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_macho_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_macho_x64_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_macho_x64_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_aarch64_byte_view_len(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_aarch64_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_aarch64_world_write_byte_view(const ZBuildability *ctx, const IrFunction *fun, const IrValue *view, ZDiag *diag);
bool z_build_check_aarch64_function_shape(const ZBuildability *ctx, const IrFunction *fun, ZDiag *diag);
bool z_build_check_value(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, bool local_set_value, unsigned scratch_slot, ZDiag *diag);
bool z_build_check_target_value(const ZBuildability *ctx, const IrFunction *fun, const IrValue *value, unsigned scratch_slot, bool *skip_left, unsigned *right_slot, ZDiag *diag);
unsigned z_build_target_call_arg_slot(const ZBuildability *ctx, const IrValue *value, unsigned scratch_slot);

#endif
